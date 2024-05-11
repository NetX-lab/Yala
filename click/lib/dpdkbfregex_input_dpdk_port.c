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
#include <rte_malloc.h>

#include <click/dpdkbfregex_dpdk_live_shared.h>
#include <click/dpdkbfregex_input.h>
#include <click/dpdkbfregex_rxpb_log.h>
#include <click/dpdkbfregex_utils.h>

#define NUM_MBUFS		8191
#define MBUF_CACHE_SIZE		256

struct rte_mempool ***mbuf_pools;

struct rx_buf_cb_helper {
	uint64_t max_mem_loc;
	uint64_t min_mem_loc;
};

static void
input_dpdk_port_clean(rb_conf *run_conf);

static const struct
rte_eth_conf port_conf_default = {
	.rxmode = {
		.mq_mode = ETH_MQ_RX_RSS,
		.max_rx_pkt_len = RTE_ETHER_MAX_LEN,
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = NULL,
			.rss_hf = ETH_RSS_IP,
		},
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};

static int
input_dpdk_port_init_queues(uint16_t port_id, uint16_t q_id, int port_idx, unsigned int numa_id, uint16_t nb_rxd,
			    struct rte_eth_rxconf *rxconf, uint16_t nb_txd, struct rte_eth_txconf *txconf)
{
	struct rte_mempool *pool;
	char pool_name[64];
	int ret;

	snprintf(pool_name, sizeof(pool_name), "mbufpool_%u:%u", port_id, q_id);
	pool = rte_pktmbuf_pool_create(pool_name, NUM_MBUFS, MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, numa_id);
	if (!pool) {
		RXPB_LOG_ERR("Failed to create mbuf pool for dev %u.", port_id);
		return -ENOMEM;
	}

	mbuf_pools[port_idx][q_id] = pool;

	ret = rte_eth_rx_queue_setup(port_id, q_id, nb_rxd, numa_id, rxconf, pool);
	if (ret < 0) {
		RXPB_LOG_ERR("Failed rx queue setup on dev %u.", port_id);
		return ret;
	}

	ret = rte_eth_tx_queue_setup(port_id, q_id, nb_txd, numa_id, txconf);
	if (ret < 0) {
		RXPB_LOG_ERR("Failed tx queue setup on dev %u.", port_id);
		return ret;
	}

	return 0;
}

static int
input_dpdk_port_init(uint16_t port_id, uint32_t num_queues, int port_idx)
{
	struct rte_eth_conf port_conf = port_conf_default;
	struct rte_eth_dev_info dev_info = {};
	uint16_t num_rx_queues = num_queues;
	uint16_t num_tx_queues = num_queues;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	struct rte_eth_rxconf rxconf;
	struct rte_eth_txconf txconf;
	unsigned int lcore_id;
	unsigned int numa_id;
	uint16_t queue_id;
	int ret;

	/* Returns 1 on success. */
	if (!rte_eth_dev_is_valid_port(port_id)) {
		RXPB_LOG_ERR("Port %u is invalid.", port_id);
		return -EINVAL;
	}

	ret = rte_eth_dev_info_get(port_id, &dev_info);
	if (ret) {
		RXPB_LOG_ERR("Failed to get config of eth dev %u: %s.", port_id, strerror(-ret));
		return ret;
	}

	/* Note: all per-queue mbufs must be from the same pool. */
	if (dev_info.tx_offload_capa & DEV_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |= DEV_TX_OFFLOAD_MBUF_FAST_FREE;

	port_conf.rx_adv_conf.rss_conf.rss_hf &= dev_info.flow_type_rss_offloads;

	ret = rte_eth_dev_configure(port_id, num_rx_queues, num_tx_queues, &port_conf);
	if (ret) {
		RXPB_LOG_ERR("Failed to configure eth dev %u: %s,", port_id, strerror(-ret));
		return ret;
	}

	ret = rte_eth_dev_adjust_nb_rx_tx_desc(port_id, &nb_rxd, &nb_txd);
	if (ret != 0) {
		RXPB_LOG_ERR("Failed to adjust rx/tx descriptors.");
		return ret;
	}

	rxconf = dev_info.default_rxconf;
	rxconf.offloads = port_conf.rxmode.offloads;
	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;

	/* Main core takes queue 0. */
	queue_id = 0;
	numa_id = rte_socket_id();
	ret = input_dpdk_port_init_queues(port_id, queue_id, port_idx, numa_id, nb_rxd, &rxconf, nb_txd, &txconf);
	if (ret)
		return ret;

	/* Assign mbufs and queues based on numa ports. */
	RTE_LCORE_FOREACH_WORKER(lcore_id)
	{
		queue_id++;
		if (queue_id >= num_queues)
			break;
		numa_id = rte_lcore_to_socket_id(lcore_id);
		ret = input_dpdk_port_init_queues(port_id, queue_id, port_idx, numa_id, nb_rxd, &rxconf, nb_txd,
						  &txconf);
		if (ret)
			return ret;
	}

	ret = rte_eth_dev_start(port_id);
	if (ret) {
		RXPB_LOG_ERR("Failed to start eth device %u: %s.", port_id, strerror(-ret));
		return ret;
	}

	/* Set device to promiscumous mode. */
	ret = rte_eth_promiscuous_enable(port_id);
	if (ret) {
		RXPB_LOG_ERR("Failed to enable promis mode on dev %u.", port_id);
		return ret;
	}

	return 0;
}

static int
input_dpdk_port_init_ports(rb_conf *run_conf)
{
	/* Each port has 1 queue for each core. */
	const uint32_t num_queues = run_conf->cores;
	uint16_t num_ports;
	uint16_t port_id;
	int port_idx;
	int ret;
	int i;

	if (rte_eth_dev_count_avail() == 0) {
		RXPB_LOG_ERR("No available ethernet devices.");
		return -EINVAL;
	}

	num_ports = 1;
	if (run_conf->port2)
		num_ports++;

	/* Assign an mbuf pool per queue and per port - reference by [port_idx][qid]. */
	mbuf_pools = rte_zmalloc(NULL, sizeof(***mbuf_pools) * num_ports, 0);
	if (!mbuf_pools) {
		RXPB_LOG_ERR("Memory failure on dpdk live mbufs.");
		return -ENOMEM;
	}

	for (i = 0; i < num_ports; i++) {
		mbuf_pools[i] = rte_zmalloc(NULL, sizeof(**mbuf_pools) * num_queues, 0);
		if (!mbuf_pools[i]) {
			RXPB_LOG_ERR("Memory failure on dpdk live mbufs.");
			return -ENOMEM;
		}
	}

	port_idx = 0;
	RTE_ETH_FOREACH_DEV(port_id) {
		/* Port index references mbufs - port_ids may not be 0-N. */
		printf("port id=%d\n",port_id);
		ret = input_dpdk_port_init(port_id, num_queues, port_idx);
		if (ret) {
			RXPB_LOG_ERR("Failed to init port: %u.", port_id);
			input_dpdk_port_clean(run_conf);
			return ret;
		}
		port_idx++;
	}

	return 0;
}

static void
input_dpdk_port_clean(rb_conf *run_conf)
{
	const uint32_t num_queues = run_conf->cores;
	uint16_t num_ports = 1;
	uint32_t j;
	uint16_t i;

	if (run_conf->port2)
		num_ports++;

	if (mbuf_pools) {
		for (i = 0; i < num_ports; i++) {
			for (j = 0; j < num_queues; j++)
				rte_mempool_free(mbuf_pools[i][j]);
			rte_free(mbuf_pools[i]);
		}
		rte_free(mbuf_pools);
	}
}

static void
input_dpdk_port_rx_buf_cb(struct rte_mempool *mp __rte_unused, void *opaque, void *obj, unsigned obj_idx __rte_unused)
{
	struct rx_buf_cb_helper *helper = (struct rx_buf_cb_helper *)opaque;
	uint64_t mem_ptr = (uint64_t)obj;

	if (mem_ptr < helper->min_mem_loc)
		helper->min_mem_loc = mem_ptr;

	if (mem_ptr > helper->max_mem_loc)
		helper->max_mem_loc = mem_ptr;
}


static int
input_dpdk_port_get_rx_buffer(uint16_t q_id, int port_idx, void **start_addr, uint32_t *size)
{
	struct rx_buf_cb_helper buf_helper;
	struct rte_mempool *pool;
	int entry_size;

	pool = mbuf_pools[port_idx][q_id];
	if (!pool)
		return -EINVAL;

	buf_helper.min_mem_loc = UINT64_MAX;
	buf_helper.max_mem_loc = 0;

	/* Run through all buffer entries to determine allocated memory block. */
	rte_mempool_obj_iter(pool, &input_dpdk_port_rx_buf_cb, &buf_helper);

	if (buf_helper.max_mem_loc == 0 || buf_helper.min_mem_loc == UINT64_MAX)
		return -EINVAL;

	*start_addr = (void *)buf_helper.min_mem_loc;
	entry_size = pool->elt_size + pool->header_size + pool->trailer_size;
	*size = (buf_helper.max_mem_loc - buf_helper.min_mem_loc) + entry_size;

	return 0;
}

void
input_dpdk_port_reg(input_func_t *funcs)
{
	funcs->init = input_dpdk_port_init_ports;
	funcs->get_rx_buffer = input_dpdk_port_get_rx_buffer;
	funcs->clean = input_dpdk_port_clean;
}
