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

#include <rte_ethdev.h>
#include <rte_lcore.h>

#include <unistd.h>

#include "dpdk_live_shared.h"
#include "regex_dev.h"
#include "run_mode.h"
#include "rxpb_log.h"
#include "stats.h"
#include "utils.h"

static uint16_t primary_port_id;
static uint16_t second_port_id;

/* TX packets in dpdk_tx port queues. */
static inline void
run_mode_tx_ports(uint16_t qid, dpdk_egress_t *dpdk_tx)
{
	struct rte_mbuf **egress_pkts;
	int to_send, sent;
	uint16_t ret;

	to_send = dpdk_tx->port_cnt[PRIM_PORT_IDX];
	sent = 0;
	while (to_send) {
		egress_pkts = &dpdk_tx->egress_pkts[PRIM_PORT_IDX][sent];
		ret = rte_eth_tx_burst(primary_port_id, qid, egress_pkts, to_send);
		to_send -= ret;
		sent += ret;
	}
	dpdk_tx->port_cnt[PRIM_PORT_IDX] = 0;

	to_send = dpdk_tx->port_cnt[SEC_PORT_IDX];
	sent = 0;
	while (to_send) {
		egress_pkts = &dpdk_tx->egress_pkts[SEC_PORT_IDX][sent];
		ret = rte_eth_tx_burst(second_port_id, qid, egress_pkts, to_send);
		to_send -= ret;
		sent += ret;
	}
	dpdk_tx->port_cnt[SEC_PORT_IDX] = 0;
}

static int
run_mode_live_dpdk(rb_conf *run_conf, int qid)
{
	const uint32_t regex_thres = run_conf->input_len_threshold;
	const uint32_t max_duration = run_conf->input_duration;
	const uint32_t max_packets = run_conf->input_packets;
	const uint16_t batch_size = run_conf->input_batches;
	const bool payload_mode = run_conf->input_app_mode;
	const uint32_t max_bytes = run_conf->input_bytes;
	uint16_t cur_rx, cur_tx, tx_port_id, rx_port_id;
	rb_stats_t *stats = run_conf->stats;
	struct rte_mbuf *bufs[batch_size];
	run_mode_stats_t *rm_stats;
	regex_stats_t *regex_stats;
	const unsigned char *pkt;
	pkt_stats_t *pkt_stats;
	dpdk_egress_t *dpdk_tx;
	uint64_t prev_cycles;
	uint64_t max_cycles;
	int ptype, pay_off;
	uint32_t pay_len;
	uint16_t num_rx;
	bool main_lcore;
	uint64_t cycles;
	double run_time;
	uint64_t start;
	bool dual_port;
	int to_send;
	int ret;
	int i;

	int flag = 0;

	/* Keep coverity check happy by initialising. */
	memset(&bufs[0], '\0', sizeof(struct rte_mbuf *) * batch_size);

	/* Convert duration to cycles. */
	max_cycles = max_duration * rte_get_timer_hz();
	prev_cycles = 0;
	cycles = 0;

	main_lcore = rte_lcore_id() == rte_get_main_lcore();

	dpdk_tx = rte_malloc(NULL, sizeof(*dpdk_tx), 64);
	if (!dpdk_tx) {
		RXPB_LOG_ERR("Memory failure on tx queue.");
		return -ENOMEM;
	}

	rm_stats = &stats->rm_stats[qid];
	pkt_stats = &rm_stats->pkt_stats;
	regex_stats = &stats->regex_stats[qid];

	// printf("checking port %s\n",run_conf->port1);
	//sleep(3);
	ret = rte_eth_dev_get_port_by_name(run_conf->port1, &primary_port_id);
	if (ret) {
		RXPB_LOG_ERR("Cannot find port %s.", run_conf->port1);
		return -EINVAL;
	}

	dual_port = false;
	if (run_conf->port2) {
		ret = rte_eth_dev_get_port_by_name(run_conf->port2, &second_port_id);
		if (ret) {
			RXPB_LOG_ERR("Cannot find port %s.", run_conf->port2);
			return -EINVAL;
		}
		dual_port = true;
	}

	// if (main_lcore)
	// 	stats_print_update(stats, run_conf->cores, 0.0, false);

	dpdk_tx->port_cnt[PRIM_PORT_IDX] = 0;
	dpdk_tx->port_cnt[SEC_PORT_IDX] = 0;

	/* Default mode ingresses and egresses on the primary port. */
	cur_rx = primary_port_id;
	cur_tx = primary_port_id;
	rx_port_id = PRIM_PORT_IDX;
	tx_port_id = PRIM_PORT_IDX;
	pay_off = 0;
	start = rte_rdtsc();


	//run_conf->latency_mode = 1;

	RXPB_LOG_INFO("latency mode: %d\n",run_conf->latency_mode);

	while (!force_quit && (!max_packets || rm_stats->tx_buf_cnt <= max_packets) &&
	       (!max_bytes || rm_stats->tx_buf_bytes <= max_bytes) && (!max_cycles || cycles <= max_cycles)) {

		cycles = rte_rdtsc() - start;

		if (main_lcore) {
			if (cycles - prev_cycles > STATS_INTERVAL_CYCLES) {
				run_time = (double)cycles / rte_get_timer_hz();
				prev_cycles = cycles;
				// stats_print_update(stats, run_conf->cores, run_time, false);
			}
		}

		// if (dual_port) {
		// 	/* Flip the port on each loop. */
		// 	if (cur_rx == primary_port_id) {
		// 		cur_rx = second_port_id;
		// 		cur_tx = primary_port_id;
		// 		rx_port_id = SEC_PORT_IDX;
		// 		tx_port_id = PRIM_PORT_IDX;
		// 	} else {
		// 		cur_rx = primary_port_id;
		// 		cur_tx = second_port_id;
		// 		rx_port_id = PRIM_PORT_IDX;
		// 		tx_port_id = SEC_PORT_IDX;
		// 	}
		// }

		num_rx = rte_eth_rx_burst(cur_rx, qid, bufs, batch_size);
		// for(int k=0;k<1;k++){
		// 	printf("length of first packet in a batch: %d\n",bufs[0]->pkt_len);
		// }
		if (!num_rx) {
			/* Idle on RX so poll for match responses. */
			dev_force_batch_pull(run_conf, qid, dpdk_tx, regex_stats);
			//run_mode_tx_ports(qid, dpdk_tx);
			continue;
		}

		/* If packet data is to be examined, pull batch into cache. */
		if (payload_mode)
			for (i = 0; i < num_rx; i++)
				rte_prefetch0(rte_pktmbuf_mtod(bufs[i], void *));

		to_send = 0;
		for (i = 0; i < num_rx; i++) {
			rm_stats->rx_buf_cnt++;
			pay_len = rte_pktmbuf_data_len(bufs[i]);
			rm_stats->rx_buf_bytes += pay_len;

			// if (flag == 0){
			// 	printf("paylen=%d\n",pay_len);
			// 	flag = 1;
			// }

			if (payload_mode) {
				ptype = 0;
				pkt = rte_pktmbuf_mtod(bufs[i], const unsigned char *);
				pay_off = util_get_app_layer_payload(pkt, &pay_len, &ptype);

				if (pay_off < 0) {
					pkt_stats->unsupported_pkts++;
					dpdk_live_add_to_tx(dpdk_tx, cur_tx, bufs[i]);
					continue;
				}

				if (pay_len == 0) {
					pkt_stats->no_payload++;
					dpdk_live_add_to_tx(dpdk_tx, cur_tx, bufs[i]);
					continue;
				}

				if (pay_len < regex_thres || pay_len > MAX_REGEX_BUF_SIZE) {
					pkt_stats->thres_drop++;
					dpdk_live_add_to_tx(dpdk_tx, cur_tx, bufs[i]);
					continue;
				}

				stats_update_pkt_stats(pkt_stats, ptype);
			} else {
				if (pay_len < regex_thres || pay_len > MAX_REGEX_BUF_SIZE) {
					pkt_stats->thres_drop++;
					dpdk_live_add_to_tx(dpdk_tx, cur_tx, bufs[i]);
					continue;
				}
				rm_stats->pkt_stats.valid_pkts++;
			}

			to_send++;
			rm_stats->tx_buf_cnt++;
			rm_stats->tx_buf_bytes += pay_len;
			ret = regex_dev_search_live(run_conf, qid, bufs[i], pay_off, rx_port_id, tx_port_id, dpdk_tx,
						    regex_stats);
			if (ret)
				return ret;
		}

		if (to_send) {
			/* Push batch if contains some valid packets. */
			rm_stats->tx_batch_cnt++;
			dev_force_batch_push(run_conf, rx_port_id, qid, dpdk_tx, regex_stats);
		}

		run_mode_tx_ports(qid, dpdk_tx);
	}

	return 0;
}

void
run_mode_live_dpdk_reg(run_func_t *funcs)
{
	funcs->run = run_mode_live_dpdk;
}
