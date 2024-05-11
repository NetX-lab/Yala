/*
 * Copyright (c) 2021-2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of NVIDIA CORPORATION &
 * AFFILIATES (the "Company") and all right, title, and interest in and to the
 * software product, including all associated intellectual property rights, are
 * and shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_timer.h>

#include "regex_dev.h"
#include "rules_file_utils.h"
#include "rxpb_log.h"
#include "utils.h"

#define FREE_CONST_PTR(x) free((void *)(long)(x))

#ifndef USE_HYPERSCAN
int
regex_dev_hyperscan_reg(acc_func_t *funcs __rte_unused)
{
	RXPB_LOG_ERR("Hyperscan has not been compiled into rxpbench.");

	return -ENOTSUP;
}
#else
#include <hs.h>

static hs_database_t *compiled_rules;
static bool verbose;

struct per_core_globals {
	union {
		struct {
			uint64_t buf_id;
			hs_scratch_t *scratch;
			int batch_cnt;
			uint64_t batch_start_time;
		};
		unsigned char cache_align[CACHE_LINE_SIZE];
	};
};

static struct per_core_globals *core_vars;

struct regex_dev_hs_match_ctx {
	regex_stats_t *stats;
	char *buf;
	uint64_t id;
	int qid;
};

static void regex_dev_hyperscan_clean(rb_conf *run_conf);

/* Convert HS flags from string to bitmask. */
static int
regex_dev_hyperscan_parse_flags(char *flag_str, unsigned int *flag_bitmask, bool leftmost, bool singlematch,
				bool single_line, bool caseless, bool multi_line)
{
	unsigned int flags = leftmost ? HS_FLAG_SOM_LEFTMOST : 0;
	size_t i;

	/* Set any global flags before checking the rule specific ones. */
	if (singlematch)
		flags |= HS_FLAG_SINGLEMATCH;
	if (single_line)
		flags |= HS_FLAG_DOTALL;
	if (caseless)
		flags |= HS_FLAG_CASELESS;
	if (multi_line)
		flags |= HS_FLAG_MULTILINE;

	for (i = 0; i < strlen(flag_str); i++) {
		switch (flag_str[i]) {
		case 'i':
			flags |= HS_FLAG_CASELESS;
			break;
		case 'm':
			flags |= HS_FLAG_MULTILINE;
			break;
		case 's':
			flags |= HS_FLAG_DOTALL;
			break;
		case 'H':
			flags |= HS_FLAG_SINGLEMATCH;
			break;
		case 'V':
			flags |= HS_FLAG_ALLOWEMPTY;
			break;
		case '8':
			flags |= HS_FLAG_UTF8;
			break;
		case 'W':
			flags |= HS_FLAG_UCP;
			break;
		case '\r':
			/* ignore a carriage return. */
			break;
		default:
			return -ENOTSUP;
		}
	}

	*flag_bitmask = flags;

	return 0;
}

static int
regex_dev_hyperscan_read_rules_file(rb_conf *run_conf, const char ***exps_arr, unsigned int **flags_arr,
				    unsigned int **ids_arr, unsigned int *elems)
{
	const char *rules_file = run_conf->compiled_rules_file;
	char *exp, *id, *hs_rule, *rules, *rule_cpy;
	unsigned int flag_bm, rule_count;
	uint64_t rules_len, i;
	long tmp_id;
	int lines;
	int ret;

	if (!rules_file) {
		RXPB_LOG_ERR("No Hypserscan rules file detected.");
		return -EINVAL;
	}

	ret = util_load_file_to_buffer(rules_file, &rules, &rules_len, 0);
	if (ret) {
		RXPB_LOG_ERR("Failed to read in rules file.");
		return ret;
	}

	lines = 0;
	for (i = 0; i < rules_len; i++)
		if (rules[i] == '\n')
			lines++;

	if (!lines) {
		RXPB_LOG_ERR("Hyperscan rules file is empty.");
		ret = -EINVAL;
		goto err_free_rules;
	}

	/* Number of lines gives max number of rules. */
	*exps_arr = rte_zmalloc(NULL, sizeof(char *) * lines, 4096);
	if (!*exps_arr) {
		RXPB_LOG_ERR("Memory failure in HS expressions.");
		ret = -ENOMEM;
		goto err_free_rules;
	}

	*flags_arr = rte_malloc(NULL, sizeof(unsigned int) * lines, 4096);
	if (!*flags_arr) {
		RXPB_LOG_ERR("Memory failure in HS flags.");
		ret = -ENOMEM;
		goto err_free_exps;
	}

	*ids_arr = rte_malloc(NULL, sizeof(unsigned int) * lines, 4096);
	if (!*ids_arr) {
		RXPB_LOG_ERR("Memory failure in HS ids.");
		ret = -ENOMEM;
		goto err_free_flags;
	}

	rule_count = 0;
	hs_rule = strtok(rules, "\n");
	while (hs_rule != NULL) {
		char *flags = NULL;

		if (hs_rule[0] == '#' || !strlen(hs_rule)) {
			hs_rule = strtok(NULL, "\n");
			continue;
		}

		/* Rule copy needed for error printing. */
		rule_cpy = strdup(hs_rule);
		if (!rule_cpy) {
			RXPB_LOG_ERR("Memory failure copying a hyperscan rule.");
			goto err_free_expr_arr;
		}

		ret = rules_file_utils_parse_rule(hs_rule, &exp, &id, &flags, RULES_FILE_HS);
		if (ret) {
			RXPB_LOG_ERR("HS syntax error detected in line: %s.", rule_cpy);
			goto err_free_expr_arr;
		}

		ret = util_str_to_dec(id, &tmp_id, sizeof(unsigned int));
		if (ret) {
			RXPB_LOG_ERR("Rule ID '%s' is invalid - rule: %s.", id, rule_cpy);
			goto err_free_expr_arr;
		}
		(*ids_arr)[rule_count] = tmp_id;

		(*exps_arr)[rule_count] = strdup(exp);
		if (!(*exps_arr)[rule_count]) {
			RXPB_LOG_ERR("Memory failure in  HS expression.");
			ret = -ENOMEM;
			goto err_free_expr_arr;
		}

		ret = regex_dev_hyperscan_parse_flags(flags, &flag_bm, run_conf->hs_leftmost, run_conf->hs_singlematch,
						      run_conf->single_line, run_conf->caseless, run_conf->multi_line);
		if (ret) {
			RXPB_LOG_ERR("Invalid flags detected - rule: %s.", rule_cpy);
			goto err_free_expr_arr;
		}
		(*flags_arr)[rule_count] = flag_bm;

		hs_rule = strtok(NULL, "\n");
		rule_count++;
		free(rule_cpy);
	}

	*elems = rule_count;

	rte_free(rules);

	return 0;

err_free_expr_arr:
	for (i = 0; i < rule_count; i++)
		if ((*exps_arr)[i])
			FREE_CONST_PTR((*exps_arr)[i]);
	free(rule_cpy);
	rte_free(*ids_arr);
err_free_flags:
	rte_free(*flags_arr);
err_free_exps:
	rte_free(*exps_arr);
err_free_rules:
	rte_free(rules);

	return ret;
}

static int
regex_dev_hyperscan_init(rb_conf *run_conf)
{
	const uint32_t num_cores = run_conf->cores;
	hs_compile_error_t *hs_comp_err;
	unsigned int *flags_arr;
	unsigned int *ids_arr;
	const char **exps_arr;
	unsigned int elems;
	hs_stats_t *stats;
	hs_error_t hs_err;
	int skipped_rules;
	int err_exp;
	uint32_t i;
	int ret;

	/* Set to NULL to prevent clean freeing unallocated memory. */
	compiled_rules = NULL;
	core_vars = NULL;
	verbose = false;

	core_vars = rte_zmalloc(NULL, sizeof(*core_vars) * num_cores, 64);
	if (!core_vars) {
		RXPB_LOG_ERR("Memory failure in HS init.");
		return -ENOMEM;
	}

	/* Convert HS rules to arrays for compilation. */
	ret = regex_dev_hyperscan_read_rules_file(run_conf, &exps_arr, &flags_arr, &ids_arr, &elems);
	if (ret)
		goto err_clean;

	/* Counter for any rules that fail to compile and are skipped. */
	skipped_rules = 0;

	RXPB_LOG_INFO("Preparing Hyperscan Rules.");
recompile:
	/* NOTE: only supporting block mode. */
	hs_err = hs_compile_multi(exps_arr, flags_arr, ids_arr, elems, HS_MODE_BLOCK, NULL, &compiled_rules,
				  &hs_comp_err);

	if (hs_err != HS_SUCCESS) {
		err_exp = hs_comp_err->expression;
		if (err_exp < 0) {
			/* Error does not relate to a specific expression. */
			RXPB_LOG_ERR("HS compilation failure: %s.", hs_comp_err->message);
		} else {
			/* Specific expression has failed. */
			if (run_conf->force_compile) {
				RXPB_LOG_INFO("- cannot compile rule: %d.", ids_arr[err_exp]);

				/* Remove uncompiled rule and retry. */
				FREE_CONST_PTR(exps_arr[err_exp]);
				for (i = err_exp; i < elems - 1; i++) {
					exps_arr[i] = exps_arr[i + 1];
					ids_arr[i] = ids_arr[i + 1];
					flags_arr[i] = flags_arr[i + 1];
				}
				elems--;
				skipped_rules++;
				hs_free_compile_error(hs_comp_err);
				goto recompile;
			}

			RXPB_LOG_ERR("HS failed on rule: '%s' - error: %s.", exps_arr[hs_comp_err->expression],
				     hs_comp_err->message);
		}

		hs_free_compile_error(hs_comp_err);
		ret = -EINVAL;
		goto err_clean_local;
	}

	for (i = 0; i < num_cores; i++) {
		hs_err = hs_alloc_scratch(compiled_rules, &core_vars[i].scratch);
		if (hs_err != HS_SUCCESS) {
			RXPB_LOG_ERR("Failed to allocate HS scratch.");
			ret = -ENOMEM;
			goto err_clean_local;
		}
	}

	if (skipped_rules)
		RXPB_LOG_WARN_REC(run_conf, "%d rules skipped in forced compilation.", skipped_rules);
	RXPB_LOG_INFO("Rules Loaded.");

	verbose = run_conf->verbose;
	if (verbose) {
		ret = regex_dev_open_match_file(run_conf);
		if (ret)
			goto err_clean_local;
	}

	/* Init min latency stats to large value. */
	for (i = 0; i < num_cores; i++) {
		stats = (hs_stats_t *)(run_conf->stats->regex_stats[i].custom);
		stats->min_lat = UINT64_MAX;
	}

	/* Expressions are allocated with strdup so don't use rte_free. */
	for (i = 0; i < elems; i++)
		FREE_CONST_PTR(exps_arr[i]);
	rte_free(exps_arr);
	rte_free(flags_arr);
	rte_free(ids_arr);

	return 0;

err_clean_local:
	for (i = 0; i < elems; i++)
		FREE_CONST_PTR(exps_arr[i]);
	rte_free(exps_arr);
	rte_free(flags_arr);
	rte_free(ids_arr);
err_clean:
	regex_dev_hyperscan_clean(run_conf);

	return ret;
}

static int
regex_dev_hyperscan_match_cb(unsigned int id, unsigned long long from, unsigned long long to,
			     unsigned int flags __rte_unused, void *ctx)
{
	struct regex_dev_hs_match_ctx *match_ctx = ctx;

	match_ctx->stats->rx_total_match++;
	if (verbose)
		regex_dev_write_to_match_file(match_ctx->qid, match_ctx->id, id, from, (to - from),
					      &match_ctx->buf[from]);

	return 0;
}

static void
regex_dev_hyperscan_batch_latency(hs_stats_t *hs_stats, uint64_t start_time, uint64_t end_time, int batch_cnt)
{
	uint64_t time_diff;

	time_diff = end_time - start_time;
	hs_stats->tot_lat += (time_diff * batch_cnt);
	if (time_diff < hs_stats->min_lat)
		hs_stats->min_lat = time_diff;
	if (time_diff > hs_stats->max_lat)
		hs_stats->max_lat = time_diff;
}

static int
regex_dev_hyperscan_search(int qid, char *buf, int buf_len, bool push_batch, regex_stats_t *stats)
{
	struct per_core_globals *core_var = &core_vars[qid];
	uint64_t start_match = stats->rx_total_match;
	struct regex_dev_hs_match_ctx ctx;
	hs_error_t err;

	/* Context will be passed to the match callback. */
	ctx.stats = stats;
	ctx.buf = buf;
	ctx.id = ++(core_var->buf_id);
	ctx.qid = qid;

	/* Record start time for first packet of batch. */
	if (!core_var->batch_cnt)
		core_var->batch_start_time = rte_get_timer_cycles();
	core_var->batch_cnt++;

	err = hs_scan(compiled_rules, buf, buf_len, 0, core_var->scratch, regex_dev_hyperscan_match_cb, &ctx);
	if (err != HS_SUCCESS) {
		RXPB_LOG_ERR("Hyperscan search failed.");
		return -EINVAL;
	}

	if (push_batch) {
		regex_dev_hyperscan_batch_latency((hs_stats_t *)stats->custom, core_var->batch_start_time,
						  rte_get_timer_cycles(), core_var->batch_cnt);
		core_var->batch_cnt = 0;
	}

	/* Check for any match in the job. */
	if (start_match != stats->rx_total_match)
		stats->rx_buf_match_cnt++;

	stats->rx_valid++;

	return 0;
}

static int
regex_dev_hyperscan_search_live(int qid, struct rte_mbuf *mbuf, int pay_off, uint16_t rx_port __rte_unused,
				uint16_t tx_port, dpdk_egress_t *dpdk_tx, regex_stats_t *stats)
{
	char *data;
	int ret;

	data = rte_pktmbuf_mtod_offset(mbuf, char *, pay_off);
	ret = regex_dev_hyperscan_search(qid, data, rte_pktmbuf_data_len(mbuf), false, stats);
	if (ret)
		return ret;

	/* Attach packet to its required egress queue. */
	dpdk_live_add_to_tx(dpdk_tx, tx_port, mbuf);

	return 0;
}

static void
regex_dev_hyperscan_force_batch_push(int qid, uint16_t rx_port __rte_unused, dpdk_egress_t *dpdk_tx __rte_unused,
				     regex_stats_t *stats)
{
	struct per_core_globals *core_var = &core_vars[qid];

	/* Calculate batch latency for processed packets. */
	regex_dev_hyperscan_batch_latency((hs_stats_t *)stats->custom, core_var->batch_start_time,
					  rte_get_timer_cycles(), core_var->batch_cnt);
	core_var->batch_cnt = 0;
}

static void
regex_dev_hyperscan_clean(rb_conf *run_conf)
{
	const uint32_t num_cores = run_conf->cores;
	uint32_t i;

	if (verbose)
		regex_dev_close_match_file(run_conf);

	if (compiled_rules)
		hs_free_database(compiled_rules);

	if (core_vars)
		for (i = 0; i < num_cores; i++)
			hs_free_scratch(core_vars[i].scratch);

	rte_free(core_vars);
}

static int
regex_dev_hyperscan_compile(rb_conf *run_conf)
{
	const char *converted_rules;
	int ret;

	if (run_conf->compiled_rules_file)
		return 0;

	if (!run_conf->raw_rules_file)
		return -EINVAL;

	/*
	 * Actual compilation takes place in init.
	 * This function converts the raw rules to Hyperscan format if required.
	 */

	/* Create new directory for rule conversion if one does not exist. */
	ret = mkdir("hs_tmp", 0755);
	if (ret && errno != EEXIST) {
		RXPB_LOG_ERR("Failed creating hyperscan temp folder.");
		return -errno;
	}

	converted_rules = "hs_tmp/hs_tmp.hs";

	/* Convert to hyperscan rules if in rxp format. */
	ret = rules_file_utils_convert_rules(run_conf, run_conf->raw_rules_file, &converted_rules, RULES_FILE_HS);
	if (ret)
		return ret;

	/* Will be set to NULL if new file was not required. */
	if (converted_rules)
		run_conf->compiled_rules_file = strdup(converted_rules);
	else
		run_conf->compiled_rules_file = strdup(run_conf->raw_rules_file);

	return 0;
}

int
regex_dev_hyperscan_reg(acc_func_t *funcs)
{
	funcs->init_acc_dev = regex_dev_hyperscan_init;
	funcs->search_regex = regex_dev_hyperscan_search;
	funcs->search_regex_live = regex_dev_hyperscan_search_live;
	funcs->force_batch_push = regex_dev_hyperscan_force_batch_push;
	funcs->clean_acc_dev = regex_dev_hyperscan_clean;
	funcs->compile_regex_rules = regex_dev_hyperscan_compile;

	return 0;
}
#endif /* USE_HYPERSCAN */
