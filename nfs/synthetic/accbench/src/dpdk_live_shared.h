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

#ifndef _INCLUDE_DPDK_LIVE_SHARED_H_
#define _INCLUDE_DPDK_LIVE_SHARED_H_

#include <rte_mbuf_core.h>

/* DPDK port ring sizes. */
#define RX_RING_SIZE  1024
#define TX_RING_SIZE  1024

#define PRIM_PORT_IDX 0
#define SEC_PORT_IDX  1

typedef struct dpdk_live_egress_bufs {
	struct rte_mbuf *egress_pkts[2][TX_RING_SIZE];
	int port_cnt[2];
} dpdk_egress_t;

static inline void
dpdk_live_add_to_tx(dpdk_egress_t *dpdk_eg, int port_idx, struct rte_mbuf *buf)
{
	dpdk_eg->egress_pkts[port_idx][(dpdk_eg->port_cnt[port_idx])++] = buf;
}

#endif /* _INCLUDE_DPDK_LIVE_SHARED_H_ */
