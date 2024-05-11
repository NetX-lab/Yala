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

#ifndef _INCLUDE_STATS_H_
#define _INCLUDE_STATS_H_

#include <stdint.h>

#include <rte_cycles.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include <click/dpdkbfregex_conf.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STATS_INTERVAL_SEC	1
#define STATS_INTERVAL_CYCLES	STATS_INTERVAL_SEC * rte_get_timer_hz()

typedef struct pkt_stats {
	uint64_t valid_pkts;	   /* Successfully parsed. */
	uint64_t unsupported_pkts; /* Unrecognised protocols. */
	uint64_t no_payload;	   /* TCP ack or similar. */
	uint64_t invalid_pkt;	   /* Bad cap length. */
	uint64_t thres_drop;	   /* Payload < input threshold. */

	/* Protocol counters. */
	uint64_t vlan;
	uint64_t qnq;
	uint64_t ipv4;
	uint64_t ipv6;
	uint64_t tcp;
	uint64_t udp;
} pkt_stats_t;

typedef struct regex_custom_rxp_exp_matches {
	uint64_t score7;
	uint64_t score6;
	uint64_t score4;
	uint64_t score0;
	uint64_t false_positives;
} rxp_exp_match_stats_t;

typedef struct regex_custom_rxp {
	uint64_t rx_invalid;
	uint64_t rx_timeout;
	uint64_t rx_max_match;
	uint64_t rx_max_prefix;
	uint64_t rx_resource_limit;
	uint64_t rx_idle;
	uint64_t tx_busy;
	uint64_t tot_lat;
	uint64_t max_lat;
	uint64_t min_lat;

	/* Expected match results. */
	rxp_exp_match_stats_t exp;
	rxp_exp_match_stats_t max_exp;
} rxp_stats_t;

typedef struct regex_custom_hs {
	uint64_t tot_lat;
	uint64_t max_lat;
	uint64_t min_lat;
} hs_stats_t;

typedef struct comp_custom_dpdk {
	uint64_t rx_invalid;
	uint64_t rx_timeout;
	uint64_t rx_idle;
	uint64_t tx_busy;
	uint64_t tot_lat;
	uint64_t max_lat;
	uint64_t min_lat;
	uint64_t dst_buf_bytes; /* size of all dst bufs */
} comp_dpdk_stats_t;

/* Stats per core are stored as an array. */
typedef struct run_mode_stats {
	union {
		struct {
			int lcore_id;
			uint64_t rx_buf_cnt;   /* Data received. */
			uint64_t rx_buf_bytes; /* Bytes received. */
			uint64_t tx_buf_cnt;   /* Data sent to regex. */
			uint64_t tx_buf_bytes; /* Bytes sent to regex. */
			uint64_t tx_batch_cnt; /* Batches sent to regex. */

			pkt_stats_t pkt_stats; /* Packet stats. */
		};
		/* Ensure multiple cores don't access the same cache line. */
		unsigned char cache_align[CACHE_LINE_SIZE * 2];
	};
} run_mode_stats_t;

typedef struct regex_dev_stats {
	union {
		struct {
			uint64_t rx_valid;
			uint64_t rx_buf_match_cnt;
			uint64_t rx_total_match;
			void *custom; /* Stats defined by dev in use. */
		};
		/* Ensure multiple cores don't access the same cache line. */
		unsigned char cache_align[CACHE_LINE_SIZE];
	};
} regex_stats_t;

typedef struct rxpbench_stats {
	run_mode_stats_t *rm_stats;
	regex_stats_t *regex_stats;
} rb_stats_t;

/* Modify packet stats (common to live and pcap modes). */
static inline void
stats_update_pkt_stats(pkt_stats_t *pkt_stats, int rte_ptype)
{
	pkt_stats->valid_pkts++;
	if ((rte_ptype & RTE_PTYPE_L2_MASK) == RTE_PTYPE_L2_ETHER_VLAN)
		pkt_stats->vlan++;
	else if ((rte_ptype & RTE_PTYPE_L2_MASK) == RTE_PTYPE_L2_ETHER_QINQ)
		pkt_stats->qnq++;

	if (RTE_ETH_IS_IPV4_HDR(rte_ptype))
		pkt_stats->ipv4++;
	else if (RTE_ETH_IS_IPV6_HDR(rte_ptype))
		pkt_stats->ipv6++;

	if ((rte_ptype & RTE_PTYPE_L4_MASK) == RTE_PTYPE_L4_TCP)
		pkt_stats->tcp++;
	else if ((rte_ptype & RTE_PTYPE_L4_MASK) == RTE_PTYPE_L4_UDP)
		pkt_stats->udp++;
}

void stats_print_update(rb_stats_t *stats, int num_queues, double time, bool end);

int stats_init(rb_conf *run_conf);

void stats_print_end_of_run(rb_conf *run_conf, double run_time);

void stats_clean(rb_conf *run_conf);

#ifdef __cplusplus
}
#endif

#endif /* _INCLUDE_STATS_H_ */
