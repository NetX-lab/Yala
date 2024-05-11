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
#include <rte_compressdev.h>
#include <rte_timer.h>

#include "../dpdk_live_shared.h"
#include "../regex_dev.h"
#include "comp_dev.h"
#include "../rxpb_log.h"
#include "../utils.h"

/* Number of dpdk queue descriptors is 1024 so need more mbuf pool entries. */
#define MBUF_POOL_SIZE		     2047 /* Should be n = (2^q - 1)*/
#define MBUF_CACHE_SIZE		     256
#define MBUF_SIZE		     (1 << 11)

/* add missing definitions */
#define 	rte_comp_op_RSP_RESOURCE_LIMIT_REACHED_F   (1 << 4)

/* Mbuf has 9 dynamic entries (dynfield1) that we can use. */
#define DF_USER_ID_HIGH		     0
#define DF_USER_ID_LOW		     1
#define DF_TIME_HIGH		     2
#define DF_TIME_LOW		     3
#define DF_PAY_OFF		     4
#define DF_EGRESS_PORT		     5


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
static struct rte_comp_op **ops_arr_tx;
static struct rte_comp_op **ops_arr_rx;
static struct rte_mempool **mbuf_pool;
static uint8_t comp_dev_id;
static int max_batch_size;
static bool verbose;

struct rte_mbuf **output_bufs;

/* Compression related context */
void *priv_xform;
uint32_t out_seg_sz;

/* Job format specific arrays. */
static uint16_t **input_subset_ids;
static uint64_t *input_job_ids;
static uint32_t input_total_jobs;
static exp_matches_t *input_exp_matches;

static bool lat_mode;

static void comp_dev_dpdk_bf_clean(rb_conf *run_conf);

static void
extbuf_free_cb(void *addr __rte_unused, void *fcb_opaque __rte_unused)
{
}

void
comp_dev_set_def_conf(rb_conf *run_conf)
{	
	// run_conf->cdev_id = 0;
	const struct rte_compressdev_capabilities *cap 
	 			= rte_compressdev_capability_get(0, RTE_COMP_ALGO_DEFLATE);
	
	//strlcpy(run_conf->driver_name, "mlx5", sizeof(run_conf->driver_name));

	run_conf->algo= RTE_COMP_ALGO_DEFLATE;
	run_conf->algo_config.huffman_enc = RTE_COMP_HUFFMAN_DYNAMIC;

	run_conf->window_sz = cap->window_size.max;
	// run_conf->buf_id = 0;
	run_conf->out_seg_sz = 2048;
	run_conf->max_sgl_segs = 1;

	run_conf->level = RTE_COMP_LEVEL_MIN;
	// run_conf->level_lst.min = RTE_COMP_LEVEL_MIN;
	// run_conf->level_lst.max = RTE_COMP_LEVEL_MAX;
	// run_conf->level_lst.inc = 0;
	// run_conf->use_external_mbufs = 0;

	return;

}


static int
comp_dev_init_ops(int batch_size, int num_queues)
{
	size_t per_core_var_sz;
	int num_entries_tx;
	int num_entries_rx;
	char pool_n[50];
	int i;

	/* Set all to NULL to ensure cleanup doesn't free unallocated memory. */
	ops_arr_tx = NULL;
	ops_arr_rx = NULL;
	mbuf_pool = NULL;
	core_vars = NULL;
	verbose = false;

	num_entries_rx = batch_size * num_queues;

	num_entries_tx = 4096; /* allocate big enough buffer for tx */

	/* Allocate space for rx/tx batches per core/queue. */
	ops_arr_tx = rte_malloc(NULL, sizeof(*ops_arr_tx) * num_entries_tx , 0);
	if (!ops_arr_tx)
		goto err_out;

	ops_arr_rx = rte_malloc(NULL, sizeof(*ops_arr_rx) * num_entries_rx, 0);
	if (!ops_arr_rx)
		goto err_out;

	/* Allocate ops for TX */
	for (i = 0; i < num_entries_tx; i++) {
		ops_arr_tx[i] = rte_malloc(NULL, sizeof(*ops_arr_tx[0]), 0);
		if (!ops_arr_tx[i])
			goto err_out;
	}

	// for (i = 0; i < num_entries_rx; i++) {
	// 	ops_arr_rx[i] = rte_malloc(NULL, sizeof(*ops_arr_rx[0]), 0);
	// 	if (!ops_arr_rx[i])
	// 		goto err_out;
	// }

	/* Create mbuf pool for each queue. */
	mbuf_pool = rte_malloc(NULL, sizeof(*mbuf_pool) * num_queues, 0);
	if (!mbuf_pool)
		goto err_out;

	for (i = 0; i < num_queues; i++) {
		sprintf(pool_n, "COMP_MUF_POOL_%u", i);
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
comp_dev_dpdk_bf_check_capabilities(rb_conf *run_conf, uint8_t dev_id)
{
	const struct rte_compressdev_capabilities *cap;

	cap = rte_compressdev_capability_get(dev_id, run_conf->algo);

	if (cap == NULL) {
		RXPB_LOG_ERR("Compress device does not support DEFLATE");
		return -1;
	}

	uint64_t comp_flags = cap->comp_feature_flags;

	/* Huffman encoding feature check */
	if (run_conf->algo_config.huffman_enc == RTE_COMP_HUFFMAN_FIXED &&
			(comp_flags & RTE_COMP_FF_HUFFMAN_FIXED) == 0) {
		RXPB_LOG_ERR("Compress device does not supported Fixed Huffman");
		return -1;
	}

	if (run_conf->algo_config.huffman_enc == RTE_COMP_HUFFMAN_DYNAMIC &&
			(comp_flags & RTE_COMP_FF_HUFFMAN_DYNAMIC) == 0) {
		RXPB_LOG_ERR("Compress device does not supported Dynamic Huffman");
		return -1;
	}

	// /* Window size */
	// if (run_conf->window_sz != -1) {
	// 	if (param_range_check(run_conf->window_sz, &cap->window_size)
	// 			< 0) {
	// 		printf(
	// 			"Compress device does not support "
	// 			"this window size\n");
	// 		return -1;
	// 	}
	// } else
	// 	/* Set window size to PMD maximum if none was specified */
	// 	run_conf->window_sz = cap->window_size.max;

	/* Check if chained mbufs is supported */
	if (run_conf->max_sgl_segs > 1  &&
			(comp_flags & RTE_COMP_FF_OOP_SGL_IN_SGL_OUT) == 0) {
		RXPB_LOG_ERR("Compress device does not support chained mbufs. Max SGL segments set to 1");
		run_conf->max_sgl_segs = 1;
	}

	/* Level 0 support */
	if (run_conf->level == 0 &&
			(comp_flags & RTE_COMP_FF_NONCOMPRESSED_BLOCKS) == 0) {
		RXPB_LOG_ERR("Compress device does not support level 0 (no compression)");
		return -1;
	}

	return 0;
}


int comp_dev_dpdk_bf_create_xform(rb_conf *run_conf, uint8_t dev_id){
	struct rte_comp_xform xform = (struct rte_comp_xform) {
		.type = RTE_COMP_COMPRESS,
		.compress = {
			.algo = RTE_COMP_ALGO_DEFLATE,
			//.algo = RTE_COMP_ALGO_LZ4,
			.deflate.huffman = run_conf->algo_config.huffman_enc,
			//.lz4.flags = 
			.level = run_conf->level,
			.window_size = run_conf->window_sz,
			.chksum = RTE_COMP_CHECKSUM_NONE,
			.hash_algo = RTE_COMP_HASH_ALGO_NONE
		}
	};


	if (rte_compressdev_private_xform_create(dev_id, &xform, &run_conf->priv_xform) < 0) {
		RXPB_LOG_ERR("Private xform could not be created");
		return -EINVAL;
	}
	return 0;
}

static int
comp_dev_dpdk_bf_config(rb_conf *run_conf, uint8_t dev_id, struct rte_compressdev_config *dev_cfg, int num_queues)
{
	struct rte_compressdev_info dev_info = {};
	int ret, i;
	int socket_id;

	memset(dev_cfg, 0, sizeof(*dev_cfg));

	socket_id = rte_compressdev_socket_id(dev_id);

	rte_compressdev_info_get(dev_id, &dev_info);
	// if (ret) {
	// 	RXPB_LOG_ERR("Failed to get BF compression device info.");
	// 	return -EINVAL;
	// }

	if (!dev_info.max_nb_queue_pairs) {
		RXPB_LOG_ERR("The maximum number of queue pairs per device is zero");
		return -EINVAL;
	}


	if (num_queues > dev_info.max_nb_queue_pairs) {
		RXPB_LOG_ERR("Requested queues/cores (%d) exceeds device max (%d)", num_queues,
			     dev_info.max_nb_queue_pairs);
		return -EINVAL;
	}

	dev_cfg->socket_id = socket_id;
	dev_cfg->nb_queue_pairs = num_queues;
	dev_cfg->max_nb_priv_xforms = DPDK_COMP_NUM_MAX_XFORMS;
	dev_cfg->max_nb_streams = 0;

	/* Check capabilities */
	if (comp_dev_dpdk_bf_check_capabilities(run_conf, dev_id) < 0){
		return -EINVAL;
	}

	RXPB_LOG_INFO("Programming card....");
	/* Configure will program the algorithm to the card and create xform. */
	ret = rte_compressdev_configure(dev_id, dev_cfg);
	if (ret) {
		RXPB_LOG_ERR("Failed to configure BF compress device.");
		return ret;
	}
	RXPB_LOG_INFO("Card configured");

	for (i = 0; i < num_queues; i++) {
		ret = rte_compressdev_queue_pair_setup(dev_id, i, DPDK_COMP_NUM_MAX_INFLIGHT_OPS, socket_id);
		if (ret) {
			RXPB_LOG_ERR("Failed to configure queue pair %u on dev %u.", i, dev_id);
			return ret;
		}
	}

	/* Create xform */
	ret = comp_dev_dpdk_bf_create_xform(run_conf, dev_id);
	if (ret < 0) {
		RXPB_LOG_ERR("Failed to create xform");
	}	


	ret = rte_compressdev_start(dev_id);
	if (ret < 0) {
		RXPB_LOG_ERR("Failed to start device %u: error %d", dev_id, ret);
		return -EPERM;
	}

	return 0;
}


static int
comp_dev_dpdk_bf_init(rb_conf *run_conf)
{

	const int num_queues = run_conf->cores;
	struct rte_compressdev_config dev_cfg;
	comp_dpdk_stats_t *stats;
	comp_dev_id = 0;
	int ret = 0;
	int i, j;
	int num_entries_dst_buf;

	/* test. Should be from input */
	comp_dev_set_def_conf(run_conf);

	/* Current implementation supports a single compress device */
	if (rte_compressdev_count() != 1) {
		RXPB_LOG_ERR("%u compression devices detected - should be 1.", rte_compressdev_count());
		return -ENOTSUP;
	}

	ret = comp_dev_dpdk_bf_config(run_conf, comp_dev_id, &dev_cfg, num_queues);
	if (ret)
		return ret;

	ret = comp_dev_init_ops(run_conf->input_batches, num_queues);
	if (ret) {
		comp_dev_dpdk_bf_clean(run_conf);
		return ret;
	}

	/* Allocate dst bufs */
	if(run_conf->input_mode == INPUT_LIVE){
		num_entries_dst_buf = run_conf->input_batches;
	}
	else if (run_conf->input_mode == INPUT_PCAP_FILE){
		num_entries_dst_buf = run_conf->nb_preloaded_bufs;
	}
	else{
		num_entries_dst_buf = run_conf->input_batches;
	}
	output_bufs = rte_malloc(NULL, sizeof(*output_bufs) * num_entries_dst_buf * num_queues, 0);
	if (!output_bufs){
		comp_dev_dpdk_bf_clean(run_conf);
		return -EINVAL;
	}
	for (i = 0; i < num_queues; i++) {
		for(j = 0; j < num_entries_dst_buf; j++){
			output_bufs[i*num_entries_dst_buf + j] = rte_pktmbuf_alloc(mbuf_pool[i]);
			if (!output_bufs[i*num_entries_dst_buf + j])
			{
				comp_dev_dpdk_bf_clean(run_conf);
				return -EINVAL;
			}
		}
	}

	verbose = run_conf->verbose;
	if (verbose) {
		ret = 0;
		if (ret) {
			comp_dev_dpdk_bf_clean(run_conf);
			return ret;
		}
	}

	/* Init min latency stats to large value. */
	for (i = 0; i < num_queues; i++) {
		stats = (comp_dpdk_stats_t *)(run_conf->stats->regex_stats[i].custom);
		stats->min_lat = UINT64_MAX;
	}

	/* Grab a copy of job format specific arrays. */
	input_subset_ids = run_conf->input_subset_ids;
	input_job_ids = run_conf->input_job_ids;
	input_total_jobs = run_conf->input_len_cnt;

	lat_mode = run_conf->latency_mode;
	priv_xform = run_conf->priv_xform;
	out_seg_sz = run_conf->out_seg_sz;

	return ret;
}


static void
comp_dev_dpdk_bf_release_mbuf(struct rte_mbuf *mbuf, regex_stats_t *stats, uint64_t recv_time)
{
	comp_dpdk_stats_t *rxp_stats = (comp_dpdk_stats_t *)stats->custom;
	uint64_t time_mbuf, time_diff;

	/* Calculate and store latency of packet through HW. */
	time_mbuf = util_get_64_bit_from_2_32(&mbuf->dynfield1[DF_TIME_HIGH]);

	time_diff = (recv_time - time_mbuf);

	rxp_stats->tot_lat += time_diff;
	if (time_diff < rxp_stats->min_lat)
		rxp_stats->min_lat = time_diff;
	if (time_diff > rxp_stats->max_lat)
		rxp_stats->max_lat = time_diff;

	// debug
	// /* Mbuf refcnt will be 1 if created by local mempool. */
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

static void
comp_dev_dpdk_bf_process_resp(int qid, struct rte_comp_op *resp, regex_stats_t *stats)
{
	comp_dpdk_stats_t *comp_stats = (comp_dpdk_stats_t *)stats->custom;
	const uint8_t res_flags = resp->status;

	/* Only DPDK error flags are supported on BF dev. */
	if (res_flags != RTE_COMP_OP_STATUS_SUCCESS) {
		comp_stats->rx_invalid++;
		return;
	}

	comp_stats->dst_buf_bytes += resp->produced;
	stats->rx_valid++;
	return;
}

static void
comp_dev_dpdk_bf_dequeue(int qid, regex_stats_t *stats, bool live, dpdk_egress_t *dpdk_tx, uint16_t wait_on_dequeue)
{
	comp_dpdk_stats_t *rxp_stats = (comp_dpdk_stats_t *)stats->custom;
	int q_offset = qid * max_batch_size;
	struct rte_comp_op **ops;
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
		if (live) {
			port1_cnt = dpdk_tx->port_cnt[PRIM_PORT_IDX];
			port2_cnt = dpdk_tx->port_cnt[SEC_PORT_IDX];

			/* Don't do a pull if can't process the max size */
			if (port1_cnt + max_batch_size > TX_RING_SIZE || port2_cnt + max_batch_size > TX_RING_SIZE)
				break;
		}

		num_dequeued = rte_compressdev_dequeue_burst(0, qid, ops, max_batch_size);
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
			mbuf = ops[i]->m_src;
			comp_dev_dpdk_bf_process_resp(qid, ops[i], stats);
			comp_dev_dpdk_bf_release_mbuf(mbuf, stats, time);
			if (live) {
				egress_idx = mbuf->dynfield1[DF_EGRESS_PORT];
				dpdk_live_add_to_tx(dpdk_tx, egress_idx, mbuf);
			}
		}

		core_vars[qid].total_dequeued += num_dequeued;
		tot_dequeued += num_dequeued;
	} while (num_dequeued || tot_dequeued < wait_on_dequeue);
}

static inline int
comp_dev_dpdk_bf_send_ops(int qid, regex_stats_t *stats, bool live, dpdk_egress_t *dpdk_tx)
{
	comp_dpdk_stats_t *rxp_stats = (comp_dpdk_stats_t *)stats->custom;
	uint16_t to_enqueue = core_vars[qid].op_offset;
	int q_offset = qid * max_batch_size;
	struct rte_comp_op **ops;
	uint16_t num_enqueued = 0;
	uint64_t tx_busy_time = 0;
	bool tx_full = false;
	uint32_t *m_time;
	uint16_t num_ops;
	uint64_t time;
	uint16_t ret;
	int i;

	/* Loop until all ops are enqueued. */
	while (num_enqueued < to_enqueue) {
		ops = &ops_arr_tx[num_enqueued + q_offset];
		num_ops = to_enqueue - num_enqueued;
		ret = rte_compressdev_enqueue_burst(comp_dev_id, qid, ops, num_ops);
		if (ret) {
			time = rte_get_timer_cycles();
			/* Put the timestamps in dynfield of mbufs sent. */
			for (i = 0; i < ret; i++) {
				m_time = &ops[i]->m_src->dynfield1[DF_TIME_HIGH];
				util_store_64_bit_as_2_32(m_time, time);
			}

			/* Queue is now free so note any tx busy time. */
			if (tx_full) {
				rxp_stats->tx_busy += rte_get_timer_cycles() - tx_busy_time;
				tx_full = false;
			}
		} 
		else if (!tx_full) {
			/* Record time when the queue cannot be written to. */
			tx_full = true;
			tx_busy_time = rte_get_timer_cycles();
		}

		num_enqueued += ret;
		comp_dev_dpdk_bf_dequeue(qid, stats, live, dpdk_tx, lat_mode ? ret : 0);
	}

	core_vars[qid].total_enqueued += num_enqueued;
	/* Reset the offset for next batch. */
	core_vars[qid].op_offset = 0;

	return 0;
}

static inline void
comp_dev_dpdk_bf_prep_op(int qid, struct rte_comp_op *op)
{
	/* Store the buffer id in the mbuf metadata. */
	util_store_64_bit_as_2_32(&op->m_src->dynfield1[DF_USER_ID_HIGH], ++(core_vars[qid].buf_id));

	/* Set src field(m_src has already been set) */
	op->src.offset = 0;
	op->src.length = op->m_src->data_len;

	/* Set dst mbuf parameters */
	op->m_dst = output_bufs[core_vars[qid].op_offset]; /* set output_bufs as m_dst */
	op->m_dst->data_len = out_seg_sz;
	op->m_dst->pkt_len = out_seg_sz;
	
	/* Set other parameters for the op */
	op->op_type = RTE_COMP_OP_STATELESS;
	op->flush_flag = RTE_COMP_FLUSH_FINAL;
	op->input_chksum = core_vars[qid].buf_id;
	op->private_xform = priv_xform;
}


/* Note: this function is only called once to allocate mbufs for a operation, then it should always be called to send the op. */
static int
comp_dev_dpdk_bf_search(int qid, char *buf, int buf_len, bool push_batch, regex_stats_t *stats)
{
	uint16_t per_q_offset = core_vars[qid].op_offset;
	int q_offset = qid * max_batch_size;
	struct rte_comp_op *op;


	/* Send the batched ops if flag is set - this resets the ops array. */
	if (push_batch){
		/* we use buf_len as the number of ops to be enqueued here */
		(core_vars[qid].op_offset) = buf_len;
		comp_dev_dpdk_bf_send_ops(qid, stats, false, NULL);
		//debug 
		// (core_vars[qid].op_offset) = 0;
	}
	else{
		/* Get the next free op for this queue and prep request. */
		op = ops_arr_tx[q_offset + per_q_offset];

		/* Set src mbuf parameters */
		op->m_src = rte_pktmbuf_alloc(mbuf_pool[qid]);
		if (!op->m_src) {
			RXPB_LOG_ERR("Failed to get mbuf from pool.");
			return -ENOMEM;
		}
		rte_pktmbuf_attach_extbuf(op->m_src, buf, 0, buf_len, &shinfo);
		op->m_src->data_len = buf_len;
		op->m_src->pkt_len = buf_len;


		comp_dev_dpdk_bf_prep_op(qid, op);

		// if((core_vars[qid].op_offset) == 0){
		// 	// debug 
		// 	print_mbuf_data(op->mbuf);
		// }

		(core_vars[qid].op_offset)++;
	}

	return 0;
}

static int
comp_dev_dpdk_bf_search_live(int qid, struct rte_mbuf *mbuf, int pay_off, uint16_t rx_port __rte_unused,
			      uint16_t tx_port, dpdk_egress_t *dpdk_tx __rte_unused, regex_stats_t *stats __rte_unused)
{
	uint16_t per_q_offset = core_vars[qid].op_offset;
	int q_offset = qid * max_batch_size;
	struct rte_comp_op *op;

	op = ops_arr_tx[q_offset + per_q_offset];

	/* Mbuf already prepared so just add to the ops. */
	op->m_src = mbuf;
	if (!op->m_src) {
		RXPB_LOG_ERR("Failed to get mbuf from pool.");
		return -ENOMEM;
	}

	/* Mbuf is used elsewhere so increase ref cnt before using here. */
	rte_mbuf_refcnt_update(mbuf, 1);

	/* Adjust and store the data position to the start of the payload. */
	if (pay_off) {
		mbuf->dynfield1[DF_PAY_OFF] = pay_off;
		rte_pktmbuf_adj(mbuf, pay_off);
	} else {
		mbuf->dynfield1[DF_PAY_OFF] = 0;
	}

	mbuf->dynfield1[DF_EGRESS_PORT] = tx_port;
	comp_dev_dpdk_bf_prep_op(qid, op);

	// if((core_vars[qid].op_offset) == 0){
	// 	// debug 
	// 	print_mbuf_data(op->mbuf);
	// }

	(core_vars[qid].op_offset)++;

	/* Enqueue should be called by the force batch function. */

	return 0;
}

static void
comp_dev_dpdk_bf_force_batch_push(int qid, uint16_t rx_port __rte_unused, dpdk_egress_t *dpdk_tx, regex_stats_t *stats)
{
	comp_dev_dpdk_bf_send_ops(qid, stats, true, dpdk_tx);
}

static void
comp_dev_dpdk_bf_force_batch_pull(int qid, dpdk_egress_t *dpdk_tx, regex_stats_t *stats)
{
	/* Async dequeue is only needed if not in latency mode so set 'wait on' value to 0. */
	comp_dev_dpdk_bf_dequeue(qid, stats, true, dpdk_tx, 0);
}

/* Ensure all ops in flight are received before exiting. */
static void
comp_dev_dpdk_bf_post_search(int qid, regex_stats_t *stats)
{
	uint64_t start, diff;

	start = rte_rdtsc();
	while (core_vars[qid].total_enqueued > core_vars[qid].total_dequeued) {
		comp_dev_dpdk_bf_dequeue(qid, stats, false, NULL, 0);

		/* Prevent infinite loops. */
		diff = rte_rdtsc() - start;
		if (diff > MAX_POST_SEARCH_DEQUEUE_CYCLES) {
			RXPB_LOG_ALERT("Post-processing appears to be in an infinite loop. Breaking...");
			break;
		}
	}
}

static void
comp_dev_dpdk_bf_clean(rb_conf *run_conf)
{
	uint32_t batches = run_conf->input_batches;
	uint32_t queues = run_conf->cores;
	uint32_t ops_size;
	uint32_t i;

	uint32_t test_cnt=0;

	ops_size = run_conf->nb_preloaded_bufs * queues;
	/* Does not apply to live mode since ops are not preloaded */
	if (ops_arr_tx) {
		for (i = 0; i < ops_size; i++)
			if (ops_arr_tx[i]){
				if(run_conf->input_mode == INPUT_PCAP_FILE){
					rte_pktmbuf_detach_extbuf(ops_arr_tx[i]->m_src);	
				}
				rte_pktmbuf_free(ops_arr_tx[i]->m_src);
				// rte_pktmbuf_free(ops_arr_tx[i]->m_dst);
				rte_free(ops_arr_tx[i]);
				test_cnt++;
			}	
		rte_free(ops_arr_tx);
	}

	if (output_bufs) {
		for (i = 0; i < ops_size; i++)
			if (output_bufs[i]){
				rte_pktmbuf_free(output_bufs[i]);
			}	
		rte_free(output_bufs);	
	}


	if (mbuf_pool) {
		for (i = 0; i < queues; i++)
			if (mbuf_pool[i])
				rte_mempool_free(mbuf_pool[i]);
		rte_free(mbuf_pool);
	}

	rte_free(core_vars);

	if (verbose){;}

	/* Free queue-pair memory. */
	rte_compressdev_stop(comp_dev_id);
}

static int
comp_dev_dpdk_bf_compile(rb_conf *run_conf)
{
	return 0;
}

int
comp_dev_dpdk_comp_reg(acc_func_t *funcs)
{
	funcs->init_acc_dev = comp_dev_dpdk_bf_init;
	funcs->search_regex = comp_dev_dpdk_bf_search;
	funcs->search_regex_live = comp_dev_dpdk_bf_search_live;
	funcs->force_batch_push = comp_dev_dpdk_bf_force_batch_push;
	funcs->force_batch_pull = comp_dev_dpdk_bf_force_batch_pull;
	funcs->post_search_regex = comp_dev_dpdk_bf_post_search;
	funcs->clean_acc_dev = comp_dev_dpdk_bf_clean;
	funcs->compile_regex_rules = comp_dev_dpdk_bf_compile;

	return 0;
}
