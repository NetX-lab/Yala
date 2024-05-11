#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <rte_ethdev.h>
#include <rte_lcore.h>

#include <click/dpdkbfregex_dpdk_live_shared.h>
#include <click/dpdkbf_acc_dev.h>
#include <click/dpdkbfregex_run_mode.h>
#include <click/dpdkbfregex_rxpb_log.h>
#include <click/dpdkbfregex_stats.h>
#include <click/dpdkbfregex_utils.h>


static int
run_mode_live_dpdk(rb_conf *run_conf, int qid, struct rte_mbuf **bufs, int *nb_dequeued_op, struct rte_mbuf **out_bufs, int push_batch)
{
	const uint32_t regex_thres = run_conf->input_len_threshold;
	const uint32_t max_duration = run_conf->input_duration;
	const uint32_t max_packets = run_conf->input_packets;
	const uint16_t batch_size = run_conf->input_batches;
	const bool payload_mode = run_conf->input_app_mode;
	const uint32_t max_bytes = run_conf->input_bytes;
	uint16_t cur_rx, cur_tx, tx_port_id, rx_port_id;
	rb_stats_t *stats = run_conf->stats;
	// struct rte_mbuf *bufs[batch_size];
	run_mode_stats_t *rm_stats;
	regex_stats_t *regex_stats;
	const unsigned char *pkt;
	pkt_stats_t *pkt_stats;
	dpdk_egress_t *dpdk_tx;
	uint64_t prev_cycles;
	uint64_t max_cycles;
	int ptype, pay_off;
	uint32_t pay_len;
	//uint16_t num_rx;
	bool main_lcore;
	uint64_t cycles;
	double run_time;
	uint64_t start;
	bool dual_port;
	int to_send;
	int ret;
	int i;

	/* Keep coverity check happy by initialising. */
	//memset(&bufs[0], '\0', sizeof(struct rte_mbuf *) * batch_size);

	/* Convert duration to cycles. */
	
	main_lcore = rte_lcore_id() == rte_get_main_lcore();

	rm_stats = &stats->rm_stats[qid];
	pkt_stats = &rm_stats->pkt_stats;
	regex_stats = &stats->regex_stats[qid];

	pay_off = 0;

	/* If packet data is to be examined, pull batch into cache. */
	// if (payload_mode)
	// 	for (i = 0; i < num_rx; i++)
	// 		rte_prefetch0(rte_pktmbuf_mtod(bufs[i], void *));

	/* If push_batch signal is set, push the batch( and pull at the same time to avoid full queue) */
	if (push_batch) {
		to_send = 0;
		for (i = 0; i < batch_size; i++) {
			rm_stats->rx_buf_cnt++;
			pay_len = rte_pktmbuf_data_len(bufs[i]);
			rm_stats->rx_buf_bytes += pay_len;

			to_send++;
			rm_stats->tx_buf_cnt++;
			rm_stats->tx_buf_bytes += pay_len;
			/* Prepare ops in regex_dev_search_live */
			ret = regex_dev_search_live(run_conf, qid, bufs[i], pay_off, rx_port_id, tx_port_id, dpdk_tx,
							regex_stats);
			if (ret)
				return ret;
		}

		if (to_send) {
		
			/* Push batch if contains some valid packets. */
			rm_stats->tx_batch_cnt++;
			
			dev_force_batch_push(run_conf, rx_port_id, qid, dpdk_tx, regex_stats, nb_dequeued_op, out_bufs);
		}	
	}
	else{
		/* If push_batch is not set, pull finished ops */
		dev_force_batch_pull(run_conf, qid, dpdk_tx, regex_stats, nb_dequeued_op, out_bufs);	
	}
	return 0;
}

void
run_mode_live_dpdk_reg(run_func_t *funcs)
{
	funcs->run = run_mode_live_dpdk;
}
