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

#ifndef _INCLUDE_INPUT_H_
#define _INCLUDE_INPUT_H_

#include <stdio.h>

#include <rte_malloc.h>

#include <click/dpdkbfregex_conf.h>
#include <click/dpdkbfregex_rxpb_log.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Input types should implement their own init and clean functions.
 * Type function pointers are registered based on the user input.
 */
typedef struct input_func {
	int (*init)(rb_conf *run_conf);
	int (*get_rx_buffer)(uint16_t q_id, int port_idx, void **start_addr, uint32_t *size);
	void (*clean)(rb_conf *run_conf);
} input_func_t;

// void input_txt_file_reg(input_func_t *funcs);

void input_dpdk_port_reg(input_func_t *funcs);

// void input_pcap_file_reg(input_func_t *funcs);

// void input_job_format_reg(input_func_t *funcs);

void input_remote_mmap_reg(input_func_t *funcs);

static inline int
input_register(rb_conf *run_conf)
{
	input_func_t *funcs;

	funcs = (input_func_t *)rte_zmalloc(NULL, sizeof(input_func_t), 0);
	if (!funcs) {
		RXPB_LOG_ERR("Memory failure in input register.");
		return -ENOMEM;
	}

	switch (run_conf->input_mode) {
	// case INPUT_TEXT_FILE:
	// 	input_txt_file_reg(funcs);
	// 	break;

	// case INPUT_PCAP_FILE:
	// 	input_pcap_file_reg(funcs);
	// 	break;

	case INPUT_LIVE:
		input_dpdk_port_reg(funcs);
		break;

	// case INPUT_JOB_FORMAT:
	// 	input_job_format_reg(funcs);
	// 	break;

	case INPUT_REMOTE_MMAP:
		input_remote_mmap_reg(funcs);
		break;

	default:
		rte_free(funcs);
		return -ENOTSUP;
	}

	run_conf->input_funcs = funcs;

	return 0;
}

static inline int
input_init(rb_conf *run_conf)
{
	input_func_t *funcs = run_conf->input_funcs;

	if (funcs->init)
		return funcs->init(run_conf);

	return -EINVAL;
}

static inline int
input_get_rx_buffer(rb_conf *run_conf, uint16_t q_id, int port_idx, void **start_addr, uint32_t *size)
{
	input_func_t *funcs = run_conf->input_funcs;

	if (funcs->get_rx_buffer)
		return funcs->get_rx_buffer(q_id, port_idx, start_addr, size);

	return 0;
}

static inline void
input_clean(rb_conf *run_conf)
{
	input_func_t *funcs = run_conf->input_funcs;

	if (funcs->clean)
		funcs->clean(run_conf);

	rte_free(run_conf->input_funcs);
}

#ifdef __cplusplus
}
#endif

#endif /* _INCLUDE_INPUT_H_ */
