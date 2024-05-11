#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_compressdev.h>
#include <rte_timer.h>

#include <click/dpdkbfregex_dpdk_live_shared.h>
#include <click/dpdkbf_acc_dev.h>
#include <click/dpdkbf_comp_dev.h>
#include <click/dpdkbfregex_rules_file_utils.h>
#include <click/dpdkbfregex_rxpb_log.h>
#include <click/dpdkbfregex_utils.h>

// static int
// dpdk_bf_comp_check_capabilities(struct compress_bf_conf *mystate, uint8_t cdev_id)
// {
// 	const struct rte_compressdev_capabilities *cap;

// 	cap = rte_compressdev_capability_get(cdev_id, mystate->algo);

// 	if (cap == NULL) {
// 		printf("Compress device does not support DEFLATE\n");
// 		return -1;
// 	}

// 	uint64_t comp_flags = cap->comp_feature_flags;

// 	/* Huffman encoding */
// 	if (mystate->algo_config.huffman_enc == RTE_COMP_HUFFMAN_FIXED &&
// 			(comp_flags & RTE_COMP_FF_HUFFMAN_FIXED) == 0) {
// 		printf("Compress device does not supported Fixed Huffman\n");
// 		return -1;
// 	}

// 	if (mystate->algo_config.huffman_enc == RTE_COMP_HUFFMAN_DYNAMIC &&
// 			(comp_flags & RTE_COMP_FF_HUFFMAN_DYNAMIC) == 0) {
// 		printf("Compress device does not supported Dynamic Huffman\n");
// 		return -1;
// 	}

// 	// /* Window size */
// 	// if (mystate->window_sz != -1) {
// 	// 	if (param_range_check(mystate->window_sz, &cap->window_size)
// 	// 			< 0) {
// 	// 		printf(
// 	// 			"Compress device does not support "
// 	// 			"this window size\n");
// 	// 		return -1;
// 	// 	}
// 	// } else
// 	// 	/* Set window size to PMD maximum if none was specified */
// 	// 	mystate->window_sz = cap->window_size.max;

// 	/* Check if chained mbufs is supported */
// 	//printf("max_sgl_segs setting: %d\n",mystate->max_sgl_segs);
// 	if (mystate->max_sgl_segs > 1  &&
// 			(comp_flags & RTE_COMP_FF_OOP_SGL_IN_SGL_OUT) == 0) {
// 		printf("Compress device does not support "
// 				"chained mbufs. Max SGL segments set to 1\n");
// 		mystate->max_sgl_segs = 1;
// 	}

// 	/* Level 0 support */
// 	// if (mystate->level_lst.min == 0 &&
// 	if (mystate->level == 0 &&
// 			(comp_flags & RTE_COMP_FF_NONCOMPRESSED_BLOCKS) == 0) {
// 		printf("Compress device does not support "
// 				"level 0 (no compression)\n");
// 		return -1;
// 	}

// 	return 0;
// }

// void
// dpdk_bf_comp_set_def_conf(struct compress_bf_conf *mystate)
// {	
// 	mystate->cdev_id = 0;
// 	const struct rte_compressdev_capabilities *cap 
// 	 			= rte_compressdev_capability_get(mystate->cdev_id, RTE_COMP_ALGO_DEFLATE);
	
// 	strlcpy(mystate->driver_name, "mlx5", sizeof(mystate->driver_name));

// 	mystate->algo= RTE_COMP_ALGO_DEFLATE;
// 	mystate->algo_config.huffman_enc = RTE_COMP_HUFFMAN_DYNAMIC;
// 	mystate->test_op = COMPRESS_ONLY;

// 	mystate->window_sz = cap->window_size.max;
// 	mystate->buf_id = 0;
// 	mystate->out_seg_sz = 2048;
// 	mystate->max_sgl_segs = 1;

// 	mystate->level = RTE_COMP_LEVEL_MIN;
// 	mystate->level_lst.min = RTE_COMP_LEVEL_MIN;
// 	mystate->level_lst.max = RTE_COMP_LEVEL_MAX;
// 	mystate->level_lst.inc = 0;
// 	mystate->use_external_mbufs = 0;

// 	return;

// }

// int dpdk_bf_comp_create_xform(struct compress_bf_conf *mystate){
// 	/* create data-path xforms */
// 	struct rte_comp_xform xform = (struct rte_comp_xform) {
// 		.type = RTE_COMP_COMPRESS,
// 		.compress = {
// 			.algo = RTE_COMP_ALGO_DEFLATE,
// 			//.algo = RTE_COMP_ALGO_LZ4,
// 			.deflate.huffman = mystate->algo_config.huffman_enc,
// 			//.lz4.flags = 
// 			.level = mystate->level,
// 			.window_size = mystate->window_sz,
// 			.chksum = RTE_COMP_CHECKSUM_NONE,
// 			.hash_algo = RTE_COMP_HASH_ALGO_NONE
// 		}
// 	};


// 	if (rte_compressdev_private_xform_create(mystate->cdev_id, &xform, &mystate->priv_xform) < 0) {
// 		printf("Private xform could not be created\n");
// 		return -EINVAL;
// 	}
// 	return 0;
// }

// int
// dpdk_bf_comp_init_compressdev(struct compress_bf_conf *mystate)
// {
// 	uint8_t enabled_cdev_count, nb_lcores, cdev_id;
// 	unsigned int i, j;
// 	int ret;
// 	uint8_t enabled_cdevs[RTE_COMPRESS_MAX_DEVS];

// 	/* check enabled compress device of a specific type(specified by driver name) */
// 	// enabled_cdev_count = rte_compressdev_devices_get(mystate->driver_name,
// 	// 		enabled_cdevs, RTE_COMPRESS_MAX_DEVS);
// 	enabled_cdev_count = rte_compressdev_count();
// 	if (enabled_cdev_count == 0) {
// 		printf("No compress devices type %s available,"
// 				    " please check the list of specified devices in EAL section\n",
// 				mystate->driver_name);
// 		return -EINVAL;
// 	}

// 	// TODO: multi-queue support
// 	// //nb_lcores = rte_lcore_count() - 1;
// 	// nb_lcores = rte_lcore_count();
// 	// printf("nb_lcores=%d\n",nb_lcores);
// 	/*
// 	 * Use fewer devices,
// 	 * if there are more available than cores.
// 	 */
// 	// if (enabled_cdev_count > nb_lcores) {
// 	// 	if (nb_lcores == 0) {
// 	// 		printf( "Cannot run with 0 cores! Increase the number of cores\n");
// 	// 		return -EINVAL;
// 	// 	}
// 	// 	enabled_cdev_count = nb_lcores;
// 	// 	printf(INFO, USER1,
// 	// 		"There's more available devices than cores!"
// 	// 		" The number of devices has been aligned to %d cores\n",
// 	// 		nb_lcores);
// 	// }

// 	/* currently we only support using one device */
// 	cdev_id = enabled_cdevs[0];
// 	mystate->cdev_id = cdev_id;

// 	/* check device information */
// 	struct rte_compressdev_info cdev_info;
// 	int socket_id = rte_compressdev_socket_id(cdev_id);

// 	rte_compressdev_info_get(cdev_id, &cdev_info);
// 	//printf("max_nb_queue_pairs: %d\n",cdev_info.max_nb_queue_pairs);
// 	if (!cdev_info.max_nb_queue_pairs) {
// 		printf("The maximum number of queue pairs per device is zero.\n");
// 		return -EINVAL;
// 	}

// 	// if (cdev_info.max_nb_queue_pairs
// 	// 	&& mystate->nb_qps > cdev_info.max_nb_queue_pairs) {
// 	// 	printf("Number of needed queue pairs is higher "
// 	// 		"than the maximum number of queue pairs "
// 	// 		"per device.");
// 	// 	return -EINVAL;
// 	// }

// 	if (dpdk_bf_comp_check_capabilities(mystate, cdev_id) < 0){
// 		return -EINVAL;
// 	}
		
// 	/* Configure compressdev */
// 	/* TODO: figure out will reconfiguration affect the device?
// 	 */
// 	struct rte_compressdev_config config = {
// 		.socket_id = socket_id,
// 		//.nb_queue_pairs = mystate->nb_qps,
// 		.nb_queue_pairs = 1,
// 		.max_nb_priv_xforms = DPDK_COMP_NUM_MAX_XFORMS,
// 		.max_nb_streams = 0
// 	};
// 	// mystate->nb_qps = config.nb_queue_pairs;

// 	if (rte_compressdev_configure(cdev_id, &config) < 0) {
// 		printf("Compress device %d configuration failed\n",cdev_id);
// 		return -EINVAL;
// 	}

// 	for (j = 0; j < 1; j++) {
// 		ret = rte_compressdev_queue_pair_setup(cdev_id, j,
// 				DPDK_COMP_NUM_MAX_INFLIGHT_OPS, socket_id);
// 		if (ret < 0) {
// 			printf("Failed to setup queue pair %u on compressdev %u\n",j, cdev_id);
// 			return -EINVAL;
// 		}
// 		mystate->dev_qid = j;
// 	}

// 	ret = dpdk_bf_comp_create_xform(mystate);
// 	if (ret < 0) {
// 		printf("Failed to create xform\n");
// 	}	

// 	ret = rte_compressdev_start(cdev_id);
// 	if (ret < 0) {
// 		printf("Failed to start device %u: error %d\n", cdev_id, ret);
// 		return -EPERM;
// 	}
	

// 	return enabled_cdev_count;
// }


// static void
// dpdkbfcomp_dequeue_pipeline(struct compress_bf_conf *mystate, int wait_on_dequeue,
//                             struct rte_mbuf **mbuf_out,
//                             int *nb_deq)
// {
	
// 	int qid = mystate->dev_qid;
// 	int cdev_id = mystate->cdev_id;


// 	struct rte_comp_op **ops;
// 	uint16_t tot_dequeued = 0;

// 	struct rte_mbuf *mbuf;

// 	int batch_size = mystate->batch_size;

// 	uint16_t num_dequeued;
// 	int out_offset = *nb_deq;

// 	ops = mystate->deq_ops;
	
// 	num_dequeued = rte_compressdev_dequeue_burst(cdev_id, qid, ops, batch_size);
// 	if(num_dequeued>0){
// 		//printf("dequeue=%d\n",num_dequeued);
// 		;
// 	}
	

// 	for (int i = 0; i < num_dequeued; i++) {
// 		mbuf = ops[i]->m_src;
// 		/* put mbuf into out buffer */
// 		mbuf_out[out_offset + i] = mbuf;
		
// 		// TODO: add post-processing
// 		//compress_bf_process_resp(qid, ops[i], stats);
// 		/* test if needed */
// 		rte_pktmbuf_free(ops[i]->m_dst);
		
// 	}

// 	tot_dequeued += num_dequeued;
// 	out_offset += num_dequeued;

// 	/* update # of dequeued mbufs, out_offset is assigned as nb_deq previously, so assign updated value back to nb_deq */
// 	*nb_deq = out_offset;
// }

// static void
// dpdkbfcomp_dequeue_rtc(struct compress_bf_conf *mystate, int wait_on_dequeue,
//                             struct rte_mbuf **mbuf_out,
//                             int *nb_deq)
// {
	
// 	int qid = mystate->dev_qid;
// 	int cdev_id = mystate->cdev_id;


// 	struct rte_comp_op **ops;
// 	uint16_t tot_dequeued = 0;

// 	struct rte_mbuf *mbuf;

// 	int batch_size = mystate->batch_size;

// 	uint16_t num_dequeued;
// 	int out_offset = *nb_deq;

// 	ops = mystate->deq_ops;

// 	/* Poll the device until no more matches are received. */
// 	do {
// 		num_dequeued = rte_compressdev_dequeue_burst(cdev_id, qid, ops, batch_size);
// 		if(num_dequeued>0){
// 			//printf("dequeue=%d\n",num_dequeued);
// 			;
// 		}
		

// 		for (int i = 0; i < num_dequeued; i++) {
// 			mbuf = ops[i]->m_src;
// 			/* put mbuf into out buffer */
// 			mbuf_out[out_offset + i] = mbuf;
			
// 			// TODO: add post-processing
// 			//compress_bf_process_resp(qid, ops[i], stats);
// 			rte_pktmbuf_free(ops[i]->m_dst);
			
// 		}

// 		tot_dequeued += num_dequeued;
// 		out_offset += num_dequeued;

// 	} while (tot_dequeued < wait_on_dequeue);

// 	/* update # of dequeued mbufs, out_offset is assigned as nb_deq previously, so assign updated value back to nb_deq */
// 	*nb_deq = out_offset;
// }

// int dpdk_bf_comp_prep_op(struct compress_bf_conf *mystate, struct rte_mbuf **mbufs, int total_ops){
	
// 	struct rte_mbuf **output_bufs = mystate->output_bufs;
// 	uint32_t buf_id = mystate->buf_id;
// 	void *priv_xform = mystate->priv_xform;
// 	uint32_t out_seg_sz = mystate->out_seg_sz;
// 	struct rte_comp_op **ops = mystate->ops;


// 	for (int op_id = 0; op_id < total_ops; op_id++) {

// 		/* Reset all data in output buffers */
// 		struct rte_mbuf *m = output_bufs[buf_id];
// 		//m->pkt_len = out_seg_sz * m->nb_segs;
// 		m->pkt_len = out_seg_sz;
// 		//printf("%d\n",m->nb_segs);
// 		// while (m) {
// 		// 	m->data_len = m->buf_len - m->data_off;
// 		// 	m = m->next;
// 		// }

// 		ops[op_id]->m_src = mbufs[op_id];
// 		//debug
// 		//ops[op_id]->m_src = test_mbufs[op_id];
// 		ops[op_id]->src.offset = 0;
// 		ops[op_id]->src.length = rte_pktmbuf_pkt_len(mbufs[op_id]);
// 		//printf("length=%d\n",rte_pktmbuf_pkt_len(mbufs[op_id]));
// 		// debug
// 		//ops[op_id]->src.length = rte_pktmbuf_pkt_len(test_mbufs[op_id]);
// 		//printf("length=%d\n",rte_pktmbuf_pkt_len(test_mbufs[op_id]));

// 		ops[op_id]->m_dst = output_bufs[buf_id];
// 		//ops[op_id]->dst.offset = 0;
// 		ops[op_id]->op_type = RTE_COMP_OP_STATELESS;
// 		ops[op_id]->flush_flag = RTE_COMP_FLUSH_FINAL;
// 		ops[op_id]->input_chksum = buf_id;
// 		ops[op_id]->private_xform = priv_xform;

// 		buf_id = (buf_id+1)%DPDK_COMP_OUTPUT_BUFS_MAX;
// 	}
// 	mystate->buf_id = buf_id;
// }



// int dpdk_bf_comp_run_rtc(struct compress_bf_conf *mystate, struct rte_mbuf **mbufs, int nb_enq,
//                             struct rte_mbuf **mbuf_out,
//                             int *nb_deq, int push_batch)

// {

// 	int batch_size = mystate->batch_size;
// 	int qid = mystate->dev_qid;
// 	int cdev_id = mystate->cdev_id;

// 	uint16_t to_enqueue = nb_enq;

// 	struct rte_comp_op **ops;
// 	uint16_t num_enqueued = 0;
// 	uint16_t num_ops;

// 	uint16_t ret;


// 	if(push_batch){
// 		dpdk_bf_comp_prep_op(mystate, mbufs, nb_enq);
// 		//debug
// 		//printf("nb_enq=%d, op_offset=%d, to_enqueue=%d\n",nb_enq,core_vars[qid].op_offset,to_enqueue);
		
// 		/* Loop until all ops are enqueued. */
// 		to_enqueue = nb_enq;

// 		while (num_enqueued < nb_enq) {
// 			//printf("enqueuing\n");
// 			ops = &mystate->ops[num_enqueued];
			
// 			/* Note that to_enqueue is always <= batch_size of this pl stage */
// 			num_ops = RTE_MIN(batch_size, to_enqueue);
// 			// printf("cdev_id=%d, qid=%d\n",cdev_id, qid);
// 			// printf("num_ops=%d\n",num_ops);
// 			//printf("in: sample seq num=%d\n",*rte_reorder_seqn(ops[0]->m_src));
// 			ret = rte_compressdev_enqueue_burst(cdev_id, qid, ops, num_ops);

// 			//debug
// 			//ret = rte_compressdev_enqueue_burst(cdev_id, qid, ops, 1);
// 			//printf("enqueue=%d\n",ret);

// 			num_enqueued += ret;
// 			to_enqueue -= ret;
// 			//printf("dequeuing\n");
// 			/* dequeue operations and put them into mbuf_out, update *nb_deq at the same time */
// 			dpdkbfcomp_dequeue_pipeline(mystate, 0, mbuf_out, nb_deq);
// 		}
// 		dpdkbfcomp_dequeue_rtc(mystate, nb_enq-*nb_deq, mbuf_out, nb_deq);
// 		if(*nb_deq != num_enqueued){
// 			printf("Rtc mode lacks packets, enqueued=%d, dequeued=%d\n",num_enqueued,*nb_deq);
// 		}
// 	}
// 	else{
// 		/* Batch not ready, for rtc mode, no need to pull anything */
// 		;
// 	}

// 	return 0;
// }



// int dpdk_bf_comp_run_pipeline(struct compress_bf_conf *mystate, struct rte_mbuf **mbufs, int nb_enq,
//                             struct rte_mbuf **mbuf_out,
//                             int *nb_deq, int push_batch)

// {

// 	int batch_size = mystate->batch_size;
// 	int qid = mystate->dev_qid;
// 	int cdev_id = mystate->cdev_id;

// 	uint16_t to_enqueue = nb_enq;

// 	struct rte_comp_op **ops;
// 	uint16_t num_enqueued = 0;
// 	uint16_t num_ops;

// 	uint16_t ret;

// 	if(push_batch){
// 		dpdk_bf_comp_prep_op(mystate, mbufs, nb_enq);
// 		//debug
// 		//printf("nb_enq=%d, op_offset=%d, to_enqueue=%d\n",nb_enq,core_vars[qid].op_offset,to_enqueue);
		
// 		/* Loop until all ops are enqueued. */
// 		to_enqueue = nb_enq;

// 		while (num_enqueued < nb_enq) {
// 			//printf("enqueuing\n");
// 			ops = &mystate->ops[num_enqueued];
			
// 			/* Note that to_enqueue is always <= batch_size of this pl stage */
// 			num_ops = RTE_MIN(batch_size, to_enqueue);
// 			// printf("cdev_id=%d, qid=%d\n",cdev_id, qid);
// 			// printf("num_ops=%d\n",num_ops);
// 			//printf("in: sample seq num=%d\n",*rte_reorder_seqn(ops[0]->m_src));
// 			ret = rte_compressdev_enqueue_burst(cdev_id, qid, ops, num_ops);

// 			//debug
// 			//ret = rte_compressdev_enqueue_burst(cdev_id, qid, ops, 1);
// 			//printf("enqueue=%d\n",ret);

// 			num_enqueued += ret;
// 			to_enqueue -= ret;
// 			//printf("dequeuing\n");
// 			/* dequeue operations and put them into mbuf_out, update *nb_deq at the same time */
// 			dpdkbfcomp_dequeue_pipeline(mystate, 0, mbuf_out, nb_deq);
// 		}
// 	}
// 	else{
// 		/* Batch not ready, pull finished ops */
// 		dpdkbfcomp_dequeue_pipeline(mystate, 0, mbuf_out, nb_deq);	
// 	}

// 	return 0;
// }

// int dpdk_bf_comp_register(struct compress_bf_conf *mystate){
// 	if(mystate->latency_mode){
// 		printf("Running in rtc mode...\n");
// 		mystate->run = dpdk_bf_comp_run_rtc;
// 	}
// 	else if(!mystate->latency_mode){
// 		printf("Running in pipeline mode...\n");
// 		mystate->run = dpdk_bf_comp_run_pipeline;	
// 	}
// 	return 0;
// }

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

	RXPB_LOG_INFO("Programming card %d....",dev_id);
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



static void
comp_dev_dpdk_bf_dequeue_dummy(int qid, regex_stats_t *stats, bool live, uint16_t wait_on_dequeue, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
	int q_offset = qid * max_batch_size;
	struct rte_comp_op **ops;
	uint16_t tot_dequeued = 0;
	int port1_cnt, port2_cnt;
	struct rte_mbuf *mbuf;
	uint16_t num_dequeued;
	int egress_idx;
	uint64_t time;
	int i;

	/* tx->rx */
	ops = &ops_arr_tx[q_offset];


	//num_dequeued = rte_compressdev_dequeue_burst(0, qid, ops, max_batch_size);
	num_dequeued = wait_on_dequeue;
	
	time = rte_get_timer_cycles();

	for (i = 0; i < num_dequeued; i++) {
		mbuf = ops[i]->m_src;
		//comp_dev_dpdk_bf_process_resp(qid, ops[i], stats);
		//comp_dev_dpdk_bf_release_mbuf(mbuf, stats, time);
		out_bufs[i+*nb_dequeued_op] = mbuf;
	}

	core_vars[qid].total_dequeued += num_dequeued;
	tot_dequeued += num_dequeued;
	*nb_dequeued_op += num_dequeued;
}

static void
comp_dev_dpdk_bf_dequeue_pipeline(int qid, regex_stats_t *stats, bool live, uint16_t wait_on_dequeue, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
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
	//printf("dequeue ops in pipeline...\n");
	//printf("wait_on_dequeue: %d",wait_on_dequeue);


	num_dequeued = rte_compressdev_dequeue_burst(0, qid, ops, max_batch_size);
	
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
		mbuf = ops[i]->m_src;
		//comp_dev_dpdk_bf_process_resp(qid, ops[i], stats);
		//comp_dev_dpdk_bf_release_mbuf(mbuf, stats, time);
		out_bufs[i+*nb_dequeued_op] = mbuf;
	}

	core_vars[qid].total_dequeued += num_dequeued;
	tot_dequeued += num_dequeued;
	*nb_dequeued_op += num_dequeued;
	//printf("total dequeued:%d\n",*nb_dequeued_op);
}

static void
comp_dev_dpdk_bf_dequeue_rtc(int qid, regex_stats_t *stats, bool live, uint16_t wait_on_dequeue, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
	int q_offset = qid * max_batch_size;
	struct rte_comp_op **ops;
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
		num_dequeued = rte_compressdev_dequeue_burst(0, qid, ops, max_batch_size);
	
		time = rte_get_timer_cycles();

		for (i = 0; i < num_dequeued; i++) {
			mbuf = ops[i]->m_src;
			//comp_dev_dpdk_bf_process_resp(qid, ops[i], stats);
			//comp_dev_dpdk_bf_release_mbuf(mbuf, stats, time);
			out_bufs[i+*nb_dequeued_op] = mbuf;
		}

		core_vars[qid].total_dequeued += num_dequeued;
		tot_dequeued += num_dequeued;
		*nb_dequeued_op += num_dequeued;
		// printf("total dequeued:%d\n",*nb_dequeued_op);
		retries++;
	}
	
}


comp_dev_dpdk_bf_send_ops_rtc(int qid, regex_stats_t *stats, bool live, dpdk_egress_t *dpdk_tx, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
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

	// printf("sending ops in rtc...\n");
	*nb_dequeued_op = 0;
	/* Loop until all ops are enqueued. */
	while (num_enqueued < to_enqueue) {
		ops = &ops_arr_tx[num_enqueued + q_offset];
		num_ops = to_enqueue - num_enqueued;
		if(num_ops != 0){
			ret = rte_compressdev_enqueue_burst(0, qid, ops, num_ops);
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
		comp_dev_dpdk_bf_dequeue_pipeline(qid, stats, live, 0, nb_dequeued_op, out_bufs);
	}

	/* Wait for all packets to dequeue but setting wait_on_dequeue to to_enqueue-*nb_dequeued_op */
	comp_dev_dpdk_bf_dequeue_rtc(qid, stats, live, to_enqueue-*nb_dequeued_op, nb_dequeued_op, out_bufs);
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
comp_dev_dpdk_bf_send_ops_dummy(int qid, regex_stats_t *stats, bool live, dpdk_egress_t *dpdk_tx, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
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
		
		comp_dev_dpdk_bf_dequeue_dummy(qid, stats, live, num_enqueued, nb_dequeued_op, out_bufs);

	}

	core_vars[qid].total_enqueued += num_enqueued;
	/* Reset the offset for next batch. */
	core_vars[qid].op_offset = 0;

	return 0;
}

static inline int
comp_dev_dpdk_bf_send_ops_pipeline(int qid, regex_stats_t *stats, bool live, dpdk_egress_t *dpdk_tx, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	rxp_stats_t *rxp_stats = (rxp_stats_t *)stats->custom;
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

	//printf("sending ops in pipeline...\n");
	*nb_dequeued_op = 0;
	/* Loop until all ops are enqueued. */
	while (num_enqueued < to_enqueue) {
		ops = &ops_arr_tx[num_enqueued + q_offset];
		num_ops = to_enqueue - num_enqueued;
		// printf("qid=%d, num_ops=%d\n",qid,num_ops);
		ret = rte_compressdev_enqueue_burst(0, qid, ops, num_ops);
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

		comp_dev_dpdk_bf_dequeue_pipeline(qid, stats, live, 0, nb_dequeued_op, out_bufs);

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
	// rte_mbuf_refcnt_update(mbuf, 1);

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
comp_dev_dpdk_bf_force_batch_push_dummy(int qid, uint16_t rx_port __rte_unused, dpdk_egress_t *dpdk_tx, regex_stats_t *stats, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	comp_dev_dpdk_bf_send_ops_dummy(qid, stats, true, dpdk_tx, nb_dequeued_op, out_bufs);
}

static void
comp_dev_dpdk_bf_force_batch_push_pipeline(int qid, uint16_t rx_port __rte_unused, dpdk_egress_t *dpdk_tx, regex_stats_t *stats, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	comp_dev_dpdk_bf_send_ops_pipeline(qid, stats, true, dpdk_tx, nb_dequeued_op, out_bufs);
}

static void
comp_dev_dpdk_bf_force_batch_push_rtc(int qid, uint16_t rx_port __rte_unused, dpdk_egress_t *dpdk_tx, regex_stats_t *stats, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	comp_dev_dpdk_bf_send_ops_rtc(qid, stats, true, dpdk_tx, nb_dequeued_op, out_bufs);
}


static void
comp_dev_dpdk_bf_force_batch_pull(int qid, dpdk_egress_t *dpdk_tx, regex_stats_t *stats, int *nb_dequeued_op, struct rte_mbuf **out_bufs)
{
	/* Async dequeue is only needed if not in latency mode so set 'wait on' value to 0. */
	comp_dev_dpdk_bf_dequeue_pipeline(qid, stats, true, 0, nb_dequeued_op, out_bufs);
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

	ops_size = batches * queues;
	if (ops_arr_tx) {
		for (i = 0; i < ops_size; i++)
			if (ops_arr_tx[i]){
				if(run_conf->input_mode == INPUT_PCAP_FILE){
					rte_pktmbuf_detach_extbuf(ops_arr_tx[i]->m_src);	
				}
				rte_pktmbuf_free(ops_arr_tx[i]->m_src);
				rte_pktmbuf_free(ops_arr_tx[i]->m_dst);
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
comp_dev_dpdk_comp_reg(acc_func_t *funcs, rb_conf *run_conf)
{
	funcs->init_acc_dev = comp_dev_dpdk_bf_init;
	funcs->search_regex_live = comp_dev_dpdk_bf_search_live;
	funcs->force_batch_pull = comp_dev_dpdk_bf_force_batch_pull;
	/* different running modes */
	if(run_conf->latency_mode == 1){
		printf("running in rtc mode...\n");
		funcs->force_batch_push = comp_dev_dpdk_bf_force_batch_push_rtc;
	}
	else if (run_conf->latency_mode == 0){
		printf("running in pipeline mode...\n");
		funcs->force_batch_push = comp_dev_dpdk_bf_force_batch_push_pipeline;
	}
	else{
		printf("running in dummy mode...\n");
		funcs->force_batch_push = comp_dev_dpdk_bf_force_batch_push_dummy;
	}
	
	funcs->post_search_regex = comp_dev_dpdk_bf_post_search;
	funcs->clean_acc_dev = comp_dev_dpdk_bf_clean;
	funcs->compile_regex_rules = comp_dev_dpdk_bf_compile;

	return 0;
}

