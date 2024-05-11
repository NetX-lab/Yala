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

#ifndef _INCLUDE_CONF_H_
#define _INCLUDE_CONF_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <rte_compressdev.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_REGEX_BUF_SIZE 16384
#define MAX_SUBSET_IDS	   4
#define MAX_DPDK_ARGS	   20
#define MAX_WARNINGS	   10
#define MAX_WARNING_LEN	   74

#define CACHE_LINE_SIZE	   RTE_CACHE_LINE_SIZE

typedef struct input_func input_func_t;
typedef struct acc_func acc_func_t;
typedef struct run_func run_func_t;
typedef struct rxpbench_stats rb_stats_t;
typedef struct pkt_stats pkt_stats_t;
typedef struct exp_matches exp_matches_t;

enum rxpbench_acc_dev
{
	REGEX_DEV_DPDK_REGEX,
	REGEX_DEV_HYPERSCAN,
	REGEX_DEV_DOCA_REGEX,
	COMP_DEV_DPDK_COMP,
	REGEX_DEV_UNKNOWN
};

enum rxpbench_input_type
{
	INPUT_PCAP_FILE,
	INPUT_TEXT_FILE,
	INPUT_LIVE,
	INPUT_JOB_FORMAT,
	INPUT_REMOTE_MMAP,
	INPUT_UNKNOWN
};

typedef struct rxpbench_conf {
	/* Config: general ops. */
	int dpdk_argc;
	char *dpdk_argv[MAX_DPDK_ARGS];
	char *regex_pcie;
	uint32_t verbose;
	uint32_t cores;

	/* Config: required input. */
	enum rxpbench_acc_dev regex_dev_type;
	enum rxpbench_input_type input_mode;
	char *input_file;
	char *raw_rules_file;
	char *compiled_rules_file;

	/* Config: run specific. */
	uint32_t rate; /* unused */
	uint32_t input_buf_len; /* unused */
	uint32_t input_duration;
	uint32_t input_iterations;
	uint32_t input_packets;
	uint32_t input_bytes;
	bool input_app_mode;
	bool per_pkt_len; /* unused */

	/* Config: Preloaded data */
	char *input_data;
	uint64_t input_data_len; 
	uint16_t *input_lens;
	uint32_t input_len_cnt;
	uint64_t *input_job_ids;
	uint16_t **input_subset_ids;
	exp_matches_t *input_exp_matches;
	pkt_stats_t *input_pkt_stats;

	/* Config: Remote mmap specific. */
	void *remote_mmap_desc;
	uint32_t remote_mmap_desc_len;

	/* Config: search specific. */
	uint32_t input_len_threshold;
	uint32_t input_overlap;
	uint32_t input_batches;
	uint32_t nb_preloaded_bufs; /* unused */
	uint32_t sliding_window;

	/* Config: RXP specific. */
	uint32_t rxp_max_matches;
	uint32_t rxp_max_latency;
	uint32_t rxp_max_prefixes;
	uint32_t latency_mode;

	/* Config: HS specific. */
	bool hs_singlematch;
	bool hs_leftmost;

	/* Config: Regex compilation. */
	bool force_compile;
	bool single_line;
	bool caseless;
	bool multi_line;
	bool free_space;

	/* Config: Compression  */
    enum rte_comp_algorithm algo;
    /* parameters for deflate and lz4 respectively */
    union {
        enum rte_comp_huffman huffman_enc;
        uint8_t flags;
    }algo_config;
	/* xform for compression */
    void *priv_xform;
	uint32_t out_seg_sz;
	uint16_t max_sgl_segs;
	int window_sz;
	uint8_t level;

	/* Config: DPDK live. */
	char *port1;
	char *port2;

	/* Function pointers for each module */
	input_func_t *input_funcs;
	acc_func_t *regex_dev_funcs;
	run_func_t *run_funcs;

	/* Stats per queue/core. */
	rb_stats_t *stats;

	/* Validation warnings in completed config. */
	bool running;
	uint32_t no_conf_warnings;
	char *conf_warning[MAX_WARNINGS];
} rb_conf;

int conf_setup(rb_conf *run_conf, int argc, char **argv);

void conf_clean(rb_conf *run_conf);

#ifdef __cplusplus
}
#endif

#endif /* _INCLUDE_CONF_H_ */
