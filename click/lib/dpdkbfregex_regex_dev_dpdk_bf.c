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

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_regexdev.h>
#include <rte_timer.h>

#include <click/dpdkbfregex_dpdk_live_shared.h>
#include <click/dpdkbf_acc_dev.h>
#include <click/dpdkbfregex_rules_file_utils.h>
#include <click/dpdkbfregex_rxpb_log.h>
#include <click/dpdkbfregex_utils.h>

/* Number of dpdk queue descriptors is 1024 so need more mbuf pool entries. */
#define MBUF_POOL_SIZE		     2047 /* Should be n = (2^q - 1)*/
#define MBUF_CACHE_SIZE		     256
#define MBUF_SIZE		     (1 << 8)

/* add missing definitions */
#define 	RTE_REGEX_OPS_RSP_RESOURCE_LIMIT_REACHED_F   (1 << 4)

/* Mbuf has 9 dynamic entries (dynfield1) that we can use. */
#define DF_USER_ID_HIGH		     0
#define DF_USER_ID_LOW		     1
#define DF_TIME_HIGH		     2
#define DF_TIME_LOW		     3
#define DF_PAY_OFF		     4
#define DF_EGRESS_PORT		     5

/* dequeue parameters */
#define MAX_RETRIES 10

struct per_core_globals {
	union {
		struct {
			uint64_t last_idle_time;
			uint64_t total_enqueued;
			uint64_t total_dequeued;
			uint64_t buf_id;
			uint16_t op_offset;
		};
		unsigned char cache_align[CACHE_LINE_SIZE];
	};
};

static struct rte_mbuf_ext_shared_info shinfo;
static struct per_core_globals *core_vars;
/* First 'batch' ops_arr_tx entries are queue 0, next queue 1 etc. */
static struct rte_regex_ops **ops_arr_tx;
static struct rte_regex_ops **ops_arr_rx;
static struct rte_mempool **mbuf_pool;
static uint8_t regex_dev_id;
static int max_batch_size;
static bool verbose;
static char *rules;

/* Job format specific arrays. */
static uint16_t **input_subset_ids;
static uint64_t *input_job_ids;
static uint32_t input_total_jobs;
static exp_matches_t *input_exp_matches;

static bool lat_mode;

static void regex_dev_dpdk_bf_clean(rb_conf *run_conf);

static void
extbuf_free_cb(void *addr __rte_unused, void *fcb_opaque __rte_unused)
{
}

static int
regex_dev_dpdk_bf_config(rb_conf *run_conf, uint8_t dev_id, struct rte_regexdev_config *dev_cfg, const char *rules_file,
			 int num_queues)
{
	struct rte_regexdev_info dev_info = {};
	struct rte_regexdev_qp_conf qp_conf;
	uint64_t rules_len;
	int ret, i;

	memset(dev_cfg, 0, sizeof(*dev_cfg));
	memset(&qp_conf, 0, sizeof(qp_conf));
	qp_conf.nb_desc = 1024;
	/* Accept out of order results. */
	qp_conf.qp_conf_flags = RTE_REGEX_QUEUE_PAIR_CFG_OOS_F;

	ret = rte_regexdev_info_get(dev_id, &dev_info);
	if (ret) {
		RXPB_LOG_ERR("Failed to get BF regex device info.");
		return -EINVAL;
	}

	/*
	 * Note that the currently targeted DPDK version does not support card
	 * configuration of variables/flags (config warns on unsupported
	 * inputs, e.g. rxp-max-matches).
	 * Therefore, the default values will be added to dev_cfg in the
	 * following code.
	 */

	if (num_queues > dev_info.max_queue_pairs) {
		RXPB_LOG_ERR("Requested queues/cores (%d) exceeds device max (%d)", num_queues,
			     dev_info.max_queue_pairs);
		return -EINVAL;
	}

	if (run_conf->rxp_max_matches > dev_info.max_matches) {
		RXPB_LOG_ERR("Requested max matches > device supports.");
		return -EINVAL;
	}
	dev_cfg->nb_max_matches = run_conf->rxp_max_matches ? run_conf->rxp_max_matches : dev_info.max_matches;
	run_conf->rxp_max_matches = dev_cfg->nb_max_matches;
	dev_cfg->nb_queue_pairs = num_queues;
	dev_cfg->nb_groups = 1;
	dev_cfg->nb_rules_per_group = dev_info.max_rules_per_group;

	if (dev_info.regexdev_capa & RTE_REGEXDEV_SUPP_MATCH_AS_END_F)
		dev_cfg->dev_cfg_flags |= RTE_REGEXDEV_CFG_MATCH_AS_END_F;

	/* Load in rules file. */
	ret = util_load_file_to_buffer(rules_file, &rules, &rules_len, 0);
	if (ret) {
		RXPB_LOG_ERR("Failed to read in rules file.");
		return ret;
	}

	dev_cfg->rule_db = rules;
	dev_cfg->rule_db_len = rules_len;

	//RXPB_LOG_INFO("Programming card memories....");
	/* Configure will program the rules to the card. */
	ret = rte_regexdev_configure(dev_id, dev_cfg);
	if (ret) {
		RXPB_LOG_ERR("Failed to configure BF regex device.");
		rte_free(rules);
		return ret;
	}
	//RXPB_LOG_INFO("Card configured");

	for (i = 0; i < num_queues; i++) {
		ret = rte_regexdev_queue_pair_setup(dev_id, i, &qp_conf);
		if (ret) {
			RXPB_LOG_ERR("Failed to configure queue pair %u on dev %u.", i, dev_id);
			rte_free(rules);
			return ret;
		}
	}

	return 0;
}

static int
regex_dev_init_ops(int batch_size, int max_matches, int num_queues)
{
	size_t per_core_var_sz;
	int match_mem_size;
	int num_entries;
	char pool_n[50];
	int i;

	/* Set all to NULL to ensure cleanup doesn't free unallocated memory. */
	ops_arr_tx = NULL;
	ops_arr_rx = NULL;
	mbuf_pool = NULL;
	core_vars = NULL;
	verbose = false;

	num_entries = batch_size * num_queues;

	/* Allocate space for rx/tx batches per core/queue. */
	ops_arr_tx = rte_malloc(NULL, sizeof(*ops_arr_tx) * num_entries, 0);
	if (!ops_arr_tx)
		goto err_out;

	ops_arr_rx = rte_malloc(NULL, sizeof(*ops_arr_rx) * num_entries, 0);
	if (!ops_arr_rx)
		goto err_out;

	/* Size of rx regex_ops is extended by potentially MAX match fields. */
	match_mem_size = max_matches * sizeof(struct rte_regexdev_match);
	for (i = 0; i < num_entries; i++) {
		ops_arr_tx[i] = rte_malloc(NULL, sizeof(*ops_arr_tx[0]), 0);
		if (!ops_arr_tx[i])
			goto err_out;

		ops_arr_rx[i] = rte_malloc(NULL, sizeof(*ops_arr_rx[0]) + match_mem_size, 0);
		if (!ops_arr_rx[i])
			goto err_out;
	}

	/* Create mbuf pool for each queue. */
	mbuf_pool = rte_malloc(NULL, sizeof(*mbuf_pool) * num_queues, 0);
	if (!mbuf_pool)
		goto err_out;

	for (i = 0; i < num_queues; i++) {
		sprintf(pool_n, "REGEX_MUF_POOL_%u", i);
		/* Pool size should be > dpdk descriptor queue. */
		mbuf_pool[i] =
			rte_pktmbuf_pool_create(pool_n, MBUF_POOL_SIZE, MBUF_CACHE_SIZE, 0, MBUF_SIZE, rte_socket_id());
		if (!mbuf_pool[i]) {
			RXPB_LOG_ERR("Failed to create mbuf pool.");
			goto err_out;
		}
	}

	shinfo.free_cb = extbuf_free_cb;
	max_batch_size = batch_size;

	/* Maintain global variables operating on each queue (per lcore). */
	per_core_var_sz = sizeof(struct per_core_globals) * num_queues;
	core_vars = rte_zmalloc(NULL, per_core_var_sz, 64);
	if (!core_vars)
		goto err_out;

	return 0;

err_out:
	/* Clean happens in calling function. */
	RXPB_LOG_ERR("Mem failure initiating dpdk ops.");

	return -ENOMEM;
}

static int
regex_dev_dpdk_bf_init(rb_conf *run_conf)
{
	const int num_queues = run_conf->cores;
	struct rte_regexdev_config dev_cfg;
	rxp_stats_t *stats;
	regex_dev_id = 0;
	int ret = 0;
	int i;

	/* Current implementation supports a single regex device */
	if (rte_regexdev_count() != 1) {
		RXPB_LOG_ERR("%u regex devices detected - should be 1.", rte_regexdev_count());
		return -ENOTSUP;
	}

	ret = regex_dev_dpdk_bf_config(run_conf, regex_dev_id, &dev_cfg, run_conf->compiled_rules_file, num_queues);
	if (ret)
		return ret;

	ret = regex_dev_init_ops(run_conf->input_batches, dev_cfg.nb_max_matches, num_queues);
	if (ret) {
		regex_dev_dpdk_bf_clean(run_conf);
		return ret;
	}

	verbose = run_conf->verbose;
	if (verbose) {
		ret = regex_dev_open_match_file(run_conf);
		if (ret) {
			regex_dev_dpdk_bf_clean(run_conf);
			return ret;
		}
	}

	/* Init min latency stats to large value. */
	for (i = 0; i < num_queues; i++) {
		stats = (rxp_stats_t *)(run_conf->stats->regex_stats[i].custom);
		stats->min_lat = UINT64_MAX;
	}

	/* Grab a copy of job format specific arrays. */
	input_subset_ids = run_conf->input_subset_ids;
	input_job_ids = run_conf->input_job_ids;
	input_total_jobs = run_conf->input_len_cnt;
	input_exp_matches = run_conf->input_exp_matches;

	lat_mode = run_conf->latency_mode;

	return ret;
}

static void
regex_dev_dpdk_bf_release_mbuf(struct rte_mbuf *mbuf, regex_stats_t *stats, uint64_t recv_time)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
	uint64_t time_mbuf, time_diff;

	/* Calculate and store latency of packet through HW. */
	// time_mbuf = util_get_64_bit_from_2_32(&mbuf->dynfield1[DF_TIME_HIGH]);

	// time_diff = (recv_time - time_mbuf);

	// rxp_stats->tot_lat += time_diff;
	// if (time_diff < rxp_stats->min_lat)
	// 	rxp_stats->min_lat = time_diff;
	// if (time_diff > rxp_stats->max_lat)
	// 	rxp_stats->max_lat = time_diff;

	/* Mbuf refcnt will be 1 if created by local mempool. */
	// if (rte_mbuf_refcnt_read(mbuf) == 1) {
	// 	rte_pktmbuf_detach_extbuf(mbuf);
	// 	rte_pktmbuf_free(mbuf);
	// } else {
	// 	/* Packet is created elsewhere - may have to update data ptr. */
	// 	if (mbuf->dynfield1[DF_PAY_OFF])
	// 		rte_pktmbuf_prepend(mbuf, mbuf->dynfield1[DF_PAY_OFF]);

	// 	rte_mbuf_refcnt_update(mbuf, -1);
	// }
}

static inline int
regex_dev_dpdk_bf_get_array_offset(uint64_t job_id)
{
	/* Job ids start at 1 while array starts at 0 so need to decrement before wrap. */
	return (job_id - 1) % input_total_jobs;
}

// static void
// regex_dev_dpdk_bf_matches(int qid, struct rte_mbuf *mbuf, uint16_t num_matches, struct rte_regexdev_match *matches)
// {
// 	uint64_t job_id;
// 	uint16_t offset;
// 	char *data;
// 	int i;

// 	/* Extract job id from mbuf metadata. */
// 	job_id = util_get_64_bit_from_2_32(&mbuf->dynfield1[DF_USER_ID_HIGH]);

// 	/* May have to convert the incrementing rule id to user input ID. */
// 	if (input_job_ids)
// 		job_id = input_job_ids[regex_dev_dpdk_bf_get_array_offset(job_id)];

// 	for (i = 0; i < num_matches; i++) {
// 		offset = matches[i].start_offset;
// 		data = rte_pktmbuf_mtod_offset(mbuf, char *, offset);
// 		regex_dev_write_to_match_file(qid, job_id, matches[i].rule_id, offset, matches[i].len, data);
// 	}
// }

// static void
// regex_dev_dpdk_bf_exp_matches(struct rte_regex_ops *resp, rxp_stats_t *rxp_stats, bool max)
// {
// 	const uint16_t num_matches = resp->nb_matches;
// 	struct rte_mbuf *mbuf = resp->user_ptr;
// 	exp_match_t actual_match[num_matches];
// 	struct rte_regexdev_match *matches;
// 	exp_matches_t actual_matches;
// 	rxp_exp_match_stats_t *stats;
// 	exp_matches_t *exp_matches;
// 	uint64_t job_id;
// 	uint16_t i;

// 	/* Copy matches to shared type - exp matches are for validation so perf is not a priority here. */
// 	matches = resp->matches;
// 	for (i = 0; i < num_matches; i++) {
// 		actual_match[i].rule_id = matches[i].rule_id;
// 		actual_match[i].start_ptr = matches[i].start_offset;
// 		actual_match[i].length = matches[i].len;
// 	}

// 	actual_matches.num_matches = num_matches;
// 	actual_matches.matches = &actual_match[0];

// 	stats = max ? &rxp_stats->max_exp : &rxp_stats->exp;
// 	job_id = util_get_64_bit_from_2_32(&mbuf->dynfield1[DF_USER_ID_HIGH]);
// 	exp_matches = &input_exp_matches[regex_dev_dpdk_bf_get_array_offset(job_id)];

// 	regex_dev_verify_exp_matches(exp_matches, &actual_matches, stats);
// }

// static void
// regex_dev_dpdk_bf_process_resp(int qid, struct rte_regex_ops *resp, regex_stats_t *stats)
// {
// 	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
// 	const uint16_t res_flags = resp->rsp_flags;

// 	/* Only DPDK error flags are supported on BF dev. */
// 	if (res_flags) {
// 		if (res_flags & RTE_REGEX_OPS_RSP_MAX_SCAN_TIMEOUT_F)
// 			rxp_stats->rx_timeout++;
// 		else if (res_flags & RTE_REGEX_OPS_RSP_MAX_MATCH_F)
// 			rxp_stats->rx_max_match++;
// 		else if (res_flags & RTE_REGEX_OPS_RSP_MAX_PREFIX_F)
// 			rxp_stats->rx_max_prefix++;
// 		else if (res_flags & RTE_REGEX_OPS_RSP_RESOURCE_LIMIT_REACHED_F)
// 			rxp_stats->rx_resource_limit++;
// 		rxp_stats->rx_invalid++;

// 		/* Still check expected matches if job failed. */
// 		if (input_exp_matches)
// 			regex_dev_dpdk_bf_exp_matches(resp, rxp_stats, res_flags);

// 		return;
// 	}

// 	stats->rx_valid++;

// 	const uint16_t num_matches = resp->nb_matches;
// 	if (num_matches) {
// 		stats->rx_buf_match_cnt++;
// 		stats->rx_total_match += num_matches;

// 		if (verbose)
// 			regex_dev_dpdk_bf_matches(qid, resp->user_ptr, num_matches, resp->matches);
// 	}

// 	if (input_exp_matches)
// 		regex_dev_dpdk_bf_exp_matches(resp, rxp_stats, res_flags);
// }

static void
regex_dev_dpdk_bf_dequeue(int qid, regex_stats_t *stats, bool live, dpdk_egress_t *dpdk_tx, uint16_t wait_on_dequeue)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
	int q_offset = qid * max_batch_size;
	struct rte_regex_ops **ops;
	uint16_t tot_dequeued = 0;
	int port1_cnt, port2_cnt;
	struct rte_mbuf *mbuf;
	uint16_t num_dequeued;
	int egress_idx;
	uint64_t time;
	int i;

	/* Determine rx ops for this queue/lcore. */
	ops = &ops_arr_rx[q_offset];

	/* Poll the device until no more matches are received. */
	do {
		// if (live) {
		// 	port1_cnt = dpdk_tx->port_cnt[PRIM_PORT_IDX];
		// 	port2_cnt = dpdk_tx->port_cnt[SEC_PORT_IDX];

		// 	/* Don't do a pull if can't process the max size */
		// 	if (port1_cnt + max_batch_size > TX_RING_SIZE || port2_cnt + max_batch_size > TX_RING_SIZE)
		// 		break;
		// }

		num_dequeued = rte_regexdev_dequeue_burst(0, qid, ops, max_batch_size);
		
		time = rte_get_timer_cycles();

		/* Handle idle timers (periods with no matches). */
		if (num_dequeued == 0) {
			if ((core_vars[qid].last_idle_time == 0) && (core_vars[qid].total_enqueued > 0)) {
				core_vars[qid].last_idle_time = time;
			}
		} else {
			if (core_vars[qid].last_idle_time != 0) {
				rxp_stats->rx_idle += time - core_vars[qid].last_idle_time;
				core_vars[qid].last_idle_time = 0;
			}
		}

		for (i = 0; i < num_dequeued; i++) {
			mbuf = ops[i]->user_ptr;
			//regex_dev_dpdk_bf_process_resp(qid, ops[i], stats);
			//regex_dev_dpdk_bf_release_mbuf(mbuf, stats, time);
		}

		core_vars[qid].total_dequeued += num_dequeued;
		tot_dequeued += num_dequeued;
		//printf("num_dequeued=%d\n",num_dequeued);
		//printf("tot_dequeued=%d\n",tot_dequeued);
		//printf("wait_on_dequeue=%d\n",wait_on_dequeue);
	//} while (num_dequeued || tot_dequeued < wait_on_dequeue);
	} while (tot_dequeued < wait_on_dequeue);
}


static void
regex_dev_dpdk_bf_dequeue_dummy(int qid, regex_stats_t *stats, bool live, uint16_t wait_on_dequeue, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
	int q_offset = qid * max_batch_size;
	struct rte_regex_ops **ops;
	uint16_t tot_dequeued = 0;
	int port1_cnt, port2_cnt;
	struct rte_mbuf *mbuf;
	uint16_t num_dequeued;
	int egress_idx;
	uint64_t time;
	int i;

	/* tx->rx */
	ops = &ops_arr_tx[q_offset];


	//num_dequeued = rte_regexdev_dequeue_burst(0, qid, ops, max_batch_size);
	num_dequeued = wait_on_dequeue;
	
	time = rte_get_timer_cycles();

	for (i = 0; i < num_dequeued; i++) {
		mbuf = ops[i]->user_ptr;
		//regex_dev_dpdk_bf_process_resp(qid, ops[i], stats);
		//regex_dev_dpdk_bf_release_mbuf(mbuf, stats, time);
		out_bufs[i+*nb_dequeued_op] = mbuf;
	}

	core_vars[qid].total_dequeued += num_dequeued;
	tot_dequeued += num_dequeued;
	*nb_dequeued_op += num_dequeued;
}

static void
regex_dev_dpdk_bf_dequeue_pipeline(int qid, regex_stats_t *stats, bool live, uint16_t wait_on_dequeue, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
	int q_offset = qid * max_batch_size;
	struct rte_regex_ops **ops;
	uint16_t tot_dequeued = 0;
	int port1_cnt, port2_cnt;
	struct rte_mbuf *mbuf;
	uint16_t num_dequeued;
	int egress_idx;
	uint64_t time;
	int i;

	/* Determine rx ops for this queue/lcore. */
	ops = &ops_arr_rx[q_offset];
	//printf("dequeue ops in pipeline...\n");
	//printf("wait_on_dequeue: %d",wait_on_dequeue);


	num_dequeued = rte_regexdev_dequeue_burst(0, qid, ops, max_batch_size);
	
	time = rte_get_timer_cycles();

	/* Handle idle timers (periods with no matches). */
	// if (num_dequeued == 0) {
	// 	if ((core_vars[qid].last_idle_time == 0) && (core_vars[qid].total_enqueued > 0)) {
	// 		core_vars[qid].last_idle_time = time;
	// 	}
	// } else {
	// 	if (core_vars[qid].last_idle_time != 0) {
	// 		rxp_stats->rx_idle += time - core_vars[qid].last_idle_time;
	// 		core_vars[qid].last_idle_time = 0;
	// 	}
	// }

	for (i = 0; i < num_dequeued; i++) {
		mbuf = ops[i]->user_ptr;
		//regex_dev_dpdk_bf_process_resp(qid, ops[i], stats);
		//regex_dev_dpdk_bf_release_mbuf(mbuf, stats, time);
		out_bufs[i+*nb_dequeued_op] = mbuf;
	}

	core_vars[qid].total_dequeued += num_dequeued;
	tot_dequeued += num_dequeued;
	*nb_dequeued_op += num_dequeued;
	//printf("total dequeued:%d\n",*nb_dequeued_op);
}

static void
regex_dev_dpdk_bf_dequeue_rtc(int qid, regex_stats_t *stats, bool live, uint16_t wait_on_dequeue, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
	int q_offset = qid * max_batch_size;
	struct rte_regex_ops **ops;
	uint16_t tot_dequeued = 0;
	int port1_cnt, port2_cnt;
	struct rte_mbuf *mbuf;
	uint16_t num_dequeued;
	int egress_idx;
	uint64_t time;
	int i;

	int retries=0;

	/* Determine rx ops for this queue/lcore. */
	ops = &ops_arr_rx[q_offset];
	// printf("wait on dequeue=%d\n",wait_on_dequeue);
	// printf("total dequeued:%d\n",*nb_dequeued_op);
	//while(tot_dequeued < wait_on_dequeue && retries < MAX_RETRIES){
	while(tot_dequeued < wait_on_dequeue){
		num_dequeued = rte_regexdev_dequeue_burst(0, qid, ops, max_batch_size);
	
		time = rte_get_timer_cycles();

		for (i = 0; i < num_dequeued; i++) {
			mbuf = ops[i]->user_ptr;
			//regex_dev_dpdk_bf_process_resp(qid, ops[i], stats);
			//regex_dev_dpdk_bf_release_mbuf(mbuf, stats, time);
			out_bufs[i+*nb_dequeued_op] = mbuf;
		}

		core_vars[qid].total_dequeued += num_dequeued;
		tot_dequeued += num_dequeued;
		*nb_dequeued_op += num_dequeued;
		// printf("total dequeued:%d\n",*nb_dequeued_op);
		retries++;
	}
	
}


regex_dev_dpdk_bf_send_ops_rtc(int qid, regex_stats_t *stats, bool live, dpdk_egress_t *dpdk_tx, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
	uint16_t to_enqueue = core_vars[qid].op_offset;
	int q_offset = qid * max_batch_size;
	struct rte_regex_ops **ops;
	uint16_t num_enqueued = 0;
	uint64_t tx_busy_time = 0;
	bool tx_full = false;
	uint32_t *m_time;
	uint16_t num_ops;
	uint64_t time;
	uint16_t ret;
	int i;

	// printf("sending ops in rtc...\n");
	*nb_dequeued_op = 0;
	/* Loop until all ops are enqueued. */
	while (num_enqueued < to_enqueue) {
		ops = &ops_arr_tx[num_enqueued + q_offset];
		num_ops = to_enqueue - num_enqueued;
		if(num_ops != 0){
			ret = rte_regexdev_enqueue_burst(0, qid, ops, num_ops);
			if (ret) {
				time = rte_get_timer_cycles();

				/* Queue is now free so note any tx busy time. */
				if (tx_full) {
					rxp_stats->tx_busy += rte_get_timer_cycles() - tx_busy_time;
					tx_full = false;
				}
			} else if (!tx_full) {
				/* Record time when the queue cannot be written to. */
				tx_full = true;
				tx_busy_time = rte_get_timer_cycles();
			}

			num_enqueued += ret;
			// printf("enqueue burst ret=%d\n",ret);
			// printf("num_enqueued=%d\n",num_enqueued);
			// printf("to_enqueue=%d\n",to_enqueue);
		}	
		/* Dequeue once after each enqueue */
		regex_dev_dpdk_bf_dequeue_pipeline(qid, stats, live, 0, nb_dequeued_op, out_bufs);
	}

	/* Wait for all packets to dequeue but setting wait_on_dequeue to to_enqueue-*nb_dequeued_op */
	regex_dev_dpdk_bf_dequeue_rtc(qid, stats, live, to_enqueue-*nb_dequeued_op, nb_dequeued_op, out_bufs);
	if(to_enqueue != *nb_dequeued_op){
		/* a check if all packets have been examined */
		printf("to_enqueue=%d\n",to_enqueue);
		printf("total dequeue=%d",*nb_dequeued_op);
	}
	

	core_vars[qid].total_enqueued += num_enqueued;
	/* Reset the offset for next batch. */
	core_vars[qid].op_offset = 0;

	return 0;
}

static inline int
regex_dev_dpdk_bf_send_ops_dummy(int qid, regex_stats_t *stats, bool live, dpdk_egress_t *dpdk_tx, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
	uint16_t to_enqueue = core_vars[qid].op_offset;
	int q_offset = qid * max_batch_size;
	struct rte_regex_ops **ops;
	uint16_t num_enqueued = 0;
	uint64_t tx_busy_time = 0;
	bool tx_full = false;
	uint32_t *m_time;
	uint16_t num_ops;
	uint64_t time;
	uint16_t ret;
	int i;

	*nb_dequeued_op = 0;
	/* Loop until all ops are enqueued. */
	while (num_enqueued < to_enqueue) {
		ops = &ops_arr_tx[num_enqueued + q_offset];
		num_ops = to_enqueue - num_enqueued;
		ret = to_enqueue;
		if (ret) {
			time = rte_get_timer_cycles();

			/* Queue is now free so note any tx busy time. */
			if (tx_full) {
				rxp_stats->tx_busy += rte_get_timer_cycles() - tx_busy_time;
				tx_full = false;
			}
		} else if (!tx_full) {
			/* Record time when the queue cannot be written to. */
			tx_full = true;
			tx_busy_time = rte_get_timer_cycles();
		}

		num_enqueued += ret;
		
		regex_dev_dpdk_bf_dequeue_dummy(qid, stats, live, num_enqueued, nb_dequeued_op, out_bufs);

	}

	core_vars[qid].total_enqueued += num_enqueued;
	/* Reset the offset for next batch. */
	core_vars[qid].op_offset = 0;

	return 0;
}

static inline int
regex_dev_dpdk_bf_send_ops_pipeline(int qid, regex_stats_t *stats, bool live, dpdk_egress_t *dpdk_tx, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
	uint16_t to_enqueue = core_vars[qid].op_offset;
	int q_offset = qid * max_batch_size;
	struct rte_regex_ops **ops;
	uint16_t num_enqueued = 0;
	uint64_t tx_busy_time = 0;
	bool tx_full = false;
	uint32_t *m_time;
	uint16_t num_ops;
	uint64_t time;
	uint16_t ret;
	int i;

	//printf("sending ops in pipeline...\n");
	*nb_dequeued_op = 0;
	/* Loop until all ops are enqueued. */
	while (num_enqueued < to_enqueue) {
		ops = &ops_arr_tx[num_enqueued + q_offset];
		num_ops = to_enqueue - num_enqueued;
		ret = rte_regexdev_enqueue_burst(0, qid, ops, num_ops);
		if (ret) {
			time = rte_get_timer_cycles();
			/* Put the timestamps in dynfield of mbufs sent. */
			// for (i = 0; i < ret; i++) {
			// 	m_time = &ops[i]->mbuf->dynfield1[DF_TIME_HIGH];
			// 	util_store_64_bit_as_2_32(m_time, time);
			// }

			/* Queue is now free so note any tx busy time. */
			if (tx_full) {
				rxp_stats->tx_busy += rte_get_timer_cycles() - tx_busy_time;
				tx_full = false;
			}
		} else if (!tx_full) {
			/* Record time when the queue cannot be written to. */
			tx_full = true;
			tx_busy_time = rte_get_timer_cycles();
		}

		num_enqueued += ret;
		// printf("enqueue burst ret=%d\n",ret);
		// printf("num_enqueued=%d\n",num_enqueued);
		// printf("to_enqueue=%d\n",to_enqueue);

		regex_dev_dpdk_bf_dequeue_pipeline(qid, stats, live, 0, nb_dequeued_op, out_bufs);

	}

	core_vars[qid].total_enqueued += num_enqueued;
	/* Reset the offset for next batch. */
	core_vars[qid].op_offset = 0;

	return 0;
}


static inline void
regex_dev_dpdk_bf_prep_op(int qid, struct rte_regex_ops *op)
{
	/* Store the buffer id in the mbuf metadata. */
	util_store_64_bit_as_2_32(&op->mbuf->dynfield1[DF_USER_ID_HIGH], ++(core_vars[qid].buf_id));

	if (input_subset_ids) {
		const int job_offset = regex_dev_dpdk_bf_get_array_offset(core_vars[qid].buf_id);

		op->group_id0 = input_subset_ids[job_offset][0];
		op->group_id1 = input_subset_ids[job_offset][1];
		op->group_id2 = input_subset_ids[job_offset][2];
		op->group_id3 = input_subset_ids[job_offset][3];
		op->req_flags = RTE_REGEX_OPS_REQ_GROUP_ID0_VALID_F | RTE_REGEX_OPS_REQ_GROUP_ID1_VALID_F |
				RTE_REGEX_OPS_REQ_GROUP_ID2_VALID_F | RTE_REGEX_OPS_REQ_GROUP_ID3_VALID_F;
	} else {
		op->group_id0 = 1;
		op->req_flags = RTE_REGEX_OPS_REQ_GROUP_ID0_VALID_F;
	}

	/* User id of the job is the address of it's mbuf - mbuf is not released until response is handled. */
	op->user_ptr = op->mbuf;
}


static int
regex_dev_dpdk_bf_search_live(int qid, struct rte_mbuf *mbuf, int pay_off, uint16_t rx_port __rte_unused,
			      uint16_t tx_port __rte_unused, dpdk_egress_t *dpdk_tx __rte_unused, regex_stats_t *stats __rte_unused)
{
	uint16_t per_q_offset = core_vars[qid].op_offset;
	int q_offset = qid * max_batch_size;
	struct rte_regex_ops *op;

	op = ops_arr_tx[q_offset + per_q_offset];

	/* Mbuf already prepared so just add to the ops. */
	op->mbuf = mbuf;
	if (!op->mbuf) {
		RXPB_LOG_ERR("Failed to get mbuf from pool.");
		return -ENOMEM;
	}

	/* Mbuf is used elsewhere so increase ref cnt before using here. */
	//rte_mbuf_refcnt_update(mbuf, 1);

	/* Adjust and store the data position to the start of the payload. */
	if (pay_off) {
		mbuf->dynfield1[DF_PAY_OFF] = pay_off;
		rte_pktmbuf_adj(mbuf, pay_off);
	} else {
		mbuf->dynfield1[DF_PAY_OFF] = 0;
	}

	// mbuf->dynfield1[DF_EGRESS_PORT] = tx_port;
	regex_dev_dpdk_bf_prep_op(qid, op);
	//printf("ops prepared\n");

	(core_vars[qid].op_offset)++;
	//printf("regex_dev_dpdk_bf_search_live() finished\n");
	/* Enqueue should be called by the force batch function. */

	return 0;
}

static void
regex_dev_dpdk_bf_force_batch_push_dummy(int qid, uint16_t rx_port __rte_unused, dpdk_egress_t *dpdk_tx, regex_stats_t *stats, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	regex_dev_dpdk_bf_send_ops_dummy(qid, stats, true, dpdk_tx, nb_dequeued_op, out_bufs);
}

static void
regex_dev_dpdk_bf_force_batch_push_pipeline(int qid, uint16_t rx_port __rte_unused, dpdk_egress_t *dpdk_tx, regex_stats_t *stats, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	regex_dev_dpdk_bf_send_ops_pipeline(qid, stats, true, dpdk_tx, nb_dequeued_op, out_bufs);
}

static void
regex_dev_dpdk_bf_force_batch_push_rtc(int qid, uint16_t rx_port __rte_unused, dpdk_egress_t *dpdk_tx, regex_stats_t *stats, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	regex_dev_dpdk_bf_send_ops_rtc(qid, stats, true, dpdk_tx, nb_dequeued_op, out_bufs);
}

static void
regex_dev_dpdk_bf_force_batch_pull(int qid, dpdk_egress_t *dpdk_tx, regex_stats_t *stats, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	/* Async dequeue is only needed if not in latency mode so set 'wait on' value to 0. */
	//regex_dev_dpdk_bf_dequeue(qid, stats, true, dpdk_tx, 0);
	/* pull once */
	regex_dev_dpdk_bf_dequeue_pipeline(qid, stats, true, 0, nb_dequeued_op, out_bufs);
}

/* Ensure all ops in flight are received before exiting. */
static void
regex_dev_dpdk_bf_post_search(int qid, regex_stats_t *stats)
{
	uint64_t start, diff;

	start = rte_rdtsc();
	while (core_vars[qid].total_enqueued > core_vars[qid].total_dequeued) {
		regex_dev_dpdk_bf_dequeue(qid, stats, false, NULL, 0);

		/* Prevent infinite loops. */
		diff = rte_rdtsc() - start;
		if (diff > MAX_POST_SEARCH_DEQUEUE_CYCLES) {
			RXPB_LOG_ALERT("Post-processing appears to be in an infinite loop. Breaking...");
			break;
		}
	}
}

static void
regex_dev_dpdk_bf_clean(rb_conf *run_conf)
{
	uint32_t batches = run_conf->input_batches;
	uint32_t queues = run_conf->cores;
	uint32_t ops_size;
	uint32_t i;

	ops_size = batches * queues;
	if (ops_arr_tx) {
		for (i = 0; i < ops_size; i++)
			if (ops_arr_tx[i])
				rte_free(ops_arr_tx[i]);
		rte_free(ops_arr_tx);
	}

	if (ops_arr_rx) {
		for (i = 0; i < ops_size; i++)
			if (ops_arr_rx[i])
				rte_free(ops_arr_rx[i]);
		rte_free(ops_arr_rx);
	}

	if (mbuf_pool) {
		for (i = 0; i < queues; i++)
			if (mbuf_pool[i])
				rte_mempool_free(mbuf_pool[i]);
		rte_free(mbuf_pool);
	}

	rte_free(core_vars);

	if (verbose)
		regex_dev_close_match_file(run_conf);
	rte_free(rules);

	/* Free queue-pair memory. */
	rte_regexdev_stop(regex_dev_id);
}

static int
regex_dev_dpdk_bf_compile(rb_conf *run_conf)
{
	// return rules_file_compile_for_rxp(run_conf);
	return 0;
}

int
regex_dev_dpdk_bf_reg(acc_func_t *funcs, rb_conf *run_conf)
{
	funcs->init_acc_dev = regex_dev_dpdk_bf_init;
	funcs->search_regex_live = regex_dev_dpdk_bf_search_live;
	/* different running modes */
	if(run_conf->latency_mode == 1){
		printf("running in rtc mode...\n");
		funcs->force_batch_push = regex_dev_dpdk_bf_force_batch_push_rtc;
	}
	else if (run_conf->latency_mode == 0){
		printf("running in pipeline mode...\n");
		funcs->force_batch_push = regex_dev_dpdk_bf_force_batch_push_pipeline;
	}
	else{
		printf("running in dummy mode...\n");
		funcs->force_batch_push = regex_dev_dpdk_bf_force_batch_push_dummy;
	}
	funcs->force_batch_pull = regex_dev_dpdk_bf_force_batch_pull;
	funcs->post_search_regex = regex_dev_dpdk_bf_post_search;
	funcs->clean_acc_dev = regex_dev_dpdk_bf_clean;
	funcs->compile_regex_rules = regex_dev_dpdk_bf_compile;

	return 0;
}
