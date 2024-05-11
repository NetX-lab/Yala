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

#ifndef _INCLUDE_REGEX_DEV_H_
#define _INCLUDE_REGEX_DEV_H_

#include <stdio.h>

#include <rte_cycles.h>
#include <rte_malloc.h>
#include <rte_mbuf_core.h>

#include <click/dpdkbfregex_conf.h>
#include <click/dpdkbfregex_dpdk_live_shared.h>
#include <click/dpdkbfregex_rxpb_log.h>
#include <click/dpdkbfregex_stats.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_POST_SEARCH_DEQUEUE_SECS	10
#define MAX_POST_SEARCH_DEQUEUE_CYCLES	MAX_POST_SEARCH_DEQUEUE_SECS * rte_get_timer_hz()

enum regex_dev_verbose {
	REGEX_DEV_VERBOSE_NO_MATCH,
	REGEX_DEV_VERBOSE_HEX,
	REGEX_DEV_VERBOSE_ASCII
};

/* Allow input modes to specify expected matches per job. */
typedef struct exp_match {
	uint32_t rule_id;
	uint16_t start_ptr;
	uint16_t length;
} exp_match_t;

typedef struct exp_matches {
	uint32_t num_matches;
	exp_match_t *matches;
} exp_matches_t;

/* Files to record regex matches from any registered device. */
static FILE *regex_matches[RTE_MAX_LCORE];
static enum regex_dev_verbose regex_dev_verbose;

/* Function pointers each regex dev should implement. */
typedef struct acc_func {
	int (*compile_regex_rules)(rb_conf *run_conf);
	int (*init_acc_dev)(rb_conf *run_conf);
	int (*search_regex)(int qid, char *buf, int buf_len, bool push_batch, regex_stats_t *stats);
	int (*search_regex_live)(int qid, struct rte_mbuf *mbuf, int pay_off, uint16_t rx_port, uint16_t tx_port,
				 dpdk_egress_t *dpdk_tx, regex_stats_t *stats);
	void (*force_batch_push)(int qid, uint16_t rx_port, dpdk_egress_t *dpdk_tx, regex_stats_t *stats, int *nb_dequeued_op, struct rte_mbuf **out_bufs);
	void (*force_batch_pull)(int qid, dpdk_egress_t *dpdk_tx, regex_stats_t *stats, int *nb_dequeued_op, struct rte_mbuf **out_bufs);
	void (*post_search_regex)(int qid, regex_stats_t *stats);
	void (*clean_acc_dev)(rb_conf *run_conf);
} acc_func_t;

int regex_dev_dpdk_bf_reg(acc_func_t *funcs, rb_conf *run_conf);

//int regex_dev_hyperscan_reg(acc_func_t *funcs);

//int regex_dev_doca_regex_reg(acc_func_t *funcs);

int comp_dev_dpdk_comp_reg(acc_func_t *funcs, rb_conf *run_conf);

/* Register selected devices function pointers. */
static inline int
acc_dev_register(rb_conf *run_conf)
{
	acc_func_t *funcs;
	int ret;

	funcs = (acc_func_t *)rte_zmalloc(NULL, sizeof(acc_func_t), 0);
	if (!funcs) {
		RXPB_LOG_ERR("Memory failure in dev register.");
		return -ENOMEM;
	}

	switch (run_conf->regex_dev_type) {
	case REGEX_DEV_DPDK_REGEX:
		ret = regex_dev_dpdk_bf_reg(funcs,run_conf);
		if (ret)
			return ret;
		break;
	
	/*
	case REGEX_DEV_HYPERSCAN:
		ret = regex_dev_hyperscan_reg(funcs);
		if (ret)
			return ret;
		break;

	case REGEX_DEV_DOCA_REGEX:
		ret = regex_dev_doca_regex_reg(funcs);
		if (ret)
			return ret;
		break;
	*/

	case COMP_DEV_DPDK_COMP:
		ret = comp_dev_dpdk_comp_reg(funcs,run_conf);
		if (ret)
			return ret;
		break;
		
	default:
		rte_free(funcs);
		return -ENOTSUP;
	}

	run_conf->regex_dev_funcs = funcs;

	return 0;
}

/*
 * Wrapper functions for each devices function pointers.
 * Called in 'main' for setup or in run modes for data path actions.
 */

static inline int
regex_dev_compile_rules(rb_conf *run_conf)
{
	acc_func_t *funcs = run_conf->regex_dev_funcs;

	if (run_conf->compiled_rules_file)
		return 0;

	if (run_conf->raw_rules_file)
		return funcs->compile_regex_rules(run_conf);

	return -EINVAL;
}

static inline int
acc_dev_init(rb_conf *run_conf)
{
	acc_func_t *funcs = run_conf->regex_dev_funcs;

	if (funcs->init_acc_dev)
		return funcs->init_acc_dev(run_conf);

	return -EINVAL;
}

static inline int
regex_dev_search(rb_conf *run_conf, int qid, char *buf, int buf_len, bool push_batch, regex_stats_t *stats)
{
	acc_func_t *funcs = run_conf->regex_dev_funcs;

	if (funcs->search_regex)
		return funcs->search_regex(qid, buf, buf_len, push_batch, stats);

	return -EINVAL;
}

static inline int
regex_dev_search_live(rb_conf *run_conf, int qid, struct rte_mbuf *mbuf, int pay_off, uint16_t rx_port,
		      uint16_t tx_port, dpdk_egress_t *dpdk_tx, regex_stats_t *stats)
{
	acc_func_t *funcs = run_conf->regex_dev_funcs;

	if (funcs->search_regex_live)
		return funcs->search_regex_live(qid, mbuf, pay_off, rx_port, tx_port, dpdk_tx, stats);

	return -EINVAL;
}

static inline void
regex_dev_post_search(rb_conf *run_conf, int qid, regex_stats_t *stats)
{
	acc_func_t *funcs = run_conf->regex_dev_funcs;

	if (funcs->post_search_regex)
		funcs->post_search_regex(qid, stats);
}

static inline void
dev_force_batch_push(rb_conf *run_conf, uint16_t rx_port, int qid, dpdk_egress_t *dpdk_tx, regex_stats_t *stats, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	acc_func_t *funcs = run_conf->regex_dev_funcs;

	if (funcs->force_batch_push)
		funcs->force_batch_push(qid, rx_port, dpdk_tx, stats, nb_dequeued_op, out_bufs);
}

static inline void
dev_force_batch_pull(rb_conf *run_conf, int qid, dpdk_egress_t *dpdk_tx, regex_stats_t *stats, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	acc_func_t *funcs = run_conf->regex_dev_funcs;

	if (funcs->force_batch_pull)
		funcs->force_batch_pull(qid, dpdk_tx, stats, nb_dequeued_op, out_bufs);
}

static inline void
regex_dev_clean_regex(rb_conf *run_conf)
{
	acc_func_t *funcs = run_conf->regex_dev_funcs;

	if (funcs->clean_acc_dev)
		funcs->clean_acc_dev(run_conf);

	rte_free(funcs);
}

/*
 * Common device functions for writing regex matches to files.
 */

static inline int
regex_dev_open_match_file(rb_conf *run_conf)
{
	unsigned int lcore_id;
	char file_name[64];
	uint32_t queue_id;

	if (run_conf->verbose == 1)
		regex_dev_verbose = REGEX_DEV_VERBOSE_NO_MATCH;
	else if (run_conf->verbose == 2)
		regex_dev_verbose = REGEX_DEV_VERBOSE_HEX;
	else if (run_conf->verbose == 3)
		regex_dev_verbose = REGEX_DEV_VERBOSE_ASCII;

	/* Open a file for main lcore. */
	snprintf(file_name, sizeof(file_name), "rxpbench_matches_main_core_%u.csv", rte_get_main_lcore());

	queue_id = 0;
	regex_matches[queue_id] = fopen(file_name, "w");
	if (!regex_matches[queue_id])
		return -ENOTSUP;

	if (regex_dev_verbose == REGEX_DEV_VERBOSE_NO_MATCH)
		fprintf(regex_matches[queue_id], "# User ID, Rule ID, Start offset, Length\n");
	else
		fprintf(regex_matches[queue_id], "# User ID, Rule ID, Start offset, Length, Match\n");

	queue_id++;
	RTE_LCORE_FOREACH_WORKER(lcore_id)
	{
		if (queue_id >= run_conf->cores)
			break;

		snprintf(file_name, sizeof(file_name), "rxpbench_matches_core_%u.csv", lcore_id);

		regex_matches[queue_id] = fopen(file_name, "w");
		if (!regex_matches[queue_id])
			return -ENOTSUP;

		fprintf(regex_matches[queue_id], "# User ID, Rule ID, Start offset, Length, Match\n");
		queue_id++;
	}

	return 0;
}

static inline void
regex_dev_write_to_match_file(int qid, uint64_t user_id, uint32_t rule_id, uint16_t start_off, uint16_t length,
			      char *match)
{
	char match_str[length + 1];
	int i;

	if (regex_dev_verbose == REGEX_DEV_VERBOSE_ASCII) {
		strncpy(match_str, match, length);
		match_str[length] = '\0';
		fprintf(regex_matches[qid], "%lu, %u, %u, %u, %s\n", user_id, rule_id, start_off, length, match_str);
	} else if (regex_dev_verbose == REGEX_DEV_VERBOSE_HEX) {
		fprintf(regex_matches[qid], "%lu, %u, %u, %u, ", user_id, rule_id, start_off, length);
		for (i = 0; i < length; i++)
			fprintf(regex_matches[qid], "\\x%02x", match[i]);
		fprintf(regex_matches[qid], "\n");
	} else {
		fprintf(regex_matches[qid], "%lu, %u, %u, %u\n", user_id, rule_id, start_off, length);
	}
}

static inline void
regex_dev_close_match_file(rb_conf *run_conf)
{
	uint32_t num_cores = run_conf->cores;
	uint32_t i;

	for (i = 0; i < num_cores; i++)
		fclose(regex_matches[i]);
}

static inline void
regex_dev_verify_exp_matches(exp_matches_t *exp_matches, exp_matches_t *act_matches, rxp_exp_match_stats_t *stats)
{
	const uint32_t exp_match_cnt = exp_matches->num_matches;
	const uint32_t act_match_cnt = act_matches->num_matches;
	uint8_t exp_scratch[exp_match_cnt];
	uint8_t act_scratch[act_match_cnt];
	bool another_pass, exp_done;
	exp_match_t *exp_match;
	uint32_t i, j;

	memset(exp_scratch, 0, exp_match_cnt);
	memset(act_scratch, 0, act_match_cnt);

	/*
	 * Score 7:	Actual match exists with same rule_id, start_ptr, and length as expected matched
	 * Score 6:	Actual match exists with same rule_id and start_ptr as expected match
	 * Score 4:	Actual match exists with same rule_id as expected match
	 * Score 0:	No actual matches exists for an expected match
	 * False Pos:	Actual match exist that is not reported in expected matches
	 *
	 * To calculate the above we sway towards accuracy as opposed to performance.
	 * Hence, 3 passes of the exp_matches are carried out to first filter score 7, then score 6, then 4 and 0.
	 * Attempting to do this in 1 pass can lead to mismatches.
	 * (e.g. a detected score 4 or 6 may actually be a score 6 or 7 for a different match)
	 */
	another_pass = false;
	for (i = 0; i < exp_match_cnt; i++) {
		exp_done = false;
		exp_match = &exp_matches->matches[i];
		for (j = 0; j < act_match_cnt; j++) {
			if (act_scratch[j])
				continue;

			if (exp_match->rule_id == act_matches->matches[j].rule_id &&
			    exp_match->start_ptr == act_matches->matches[j].start_ptr &&
			    exp_match->length == act_matches->matches[j].length) {
				exp_scratch[i] = 7;
				act_scratch[j] = 7;
				stats->score7++;
				exp_done = true;
				break;
			}
		}
		/* If exp match is not verified we need another pass. */
		if (!exp_done)
			another_pass = true;
	}

	if (!another_pass)
		goto get_false_pos;

	another_pass = false;
	for (i = 0; i < exp_match_cnt; i++) {
		if (exp_scratch[i])
			continue;

		exp_done = false;
		exp_match = &exp_matches->matches[i];
		for (j = 0; j < act_match_cnt; j++) {
			if (act_scratch[j])
				continue;

			if (exp_match->rule_id == act_matches->matches[j].rule_id &&
			    exp_match->start_ptr == act_matches->matches[j].start_ptr) {
				exp_scratch[i] = 6;
				act_scratch[j] = 6;
				stats->score6++;
				exp_done = true;
				break;
			}
		}
		/* If exp match is not verified we need another pass. */
		if (!exp_done)
			another_pass = true;
	}

	if (!another_pass)
		goto get_false_pos;

	for (i = 0; i < exp_match_cnt; i++) {
		if (exp_scratch[i])
			continue;

		exp_done = false;
		exp_match = &exp_matches->matches[i];
		for (j = 0; j < act_match_cnt; j++) {
			if (act_scratch[j])
				continue;

			if (exp_match->rule_id == act_matches->matches[j].rule_id) {
				exp_scratch[i] = 4;
				act_scratch[j] = 4;
				stats->score4++;
				exp_done = true;
				break;
			}
		}
		/* No actual match exists for expected match so mark is score 0. */
		if (!exp_done)
			stats->score0++;
	}

get_false_pos:
	/* Any actual matches not yet associated with an exp match are false positives. */
	for (i = 0; i < act_match_cnt; i++)
		if (!act_scratch[i])
			stats->false_positives++;
}

#ifdef __cplusplus
}
#endif

#endif /* _INCLUDE_REGEX_DEV_H_ */
