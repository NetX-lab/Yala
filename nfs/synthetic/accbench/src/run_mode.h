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

#ifndef _INCLUDE_RUN_MODE_H_
#define _INCLUDE_RUN_MODE_H_

#include <stdio.h>

#include <rte_malloc.h>

#include "conf.h"
#include "rxpb_log.h"

extern volatile bool force_quit;

typedef struct run_func {
	int (*run)(rb_conf *run_conf, int qid);
} run_func_t;

void run_mode_preloaded_reg(run_func_t *funcs);

void run_mode_live_dpdk_reg(run_func_t *funcs);

/* Register run mode functions as dicatated by input mode selected. */
static inline int
run_mode_register(rb_conf *run_conf)
{
	run_func_t *funcs;

	funcs = rte_zmalloc(NULL, sizeof(run_func_t), 0);
	if (!funcs) {
		RXPB_LOG_ERR("Memory failure in run mode register.");
		return -ENOMEM;
	}

	switch (run_conf->input_mode) {
	case INPUT_TEXT_FILE:
	case INPUT_PCAP_FILE:
	case INPUT_JOB_FORMAT:
	case INPUT_REMOTE_MMAP:
		run_mode_preloaded_reg(funcs);
		break;

	case INPUT_LIVE:
		run_mode_live_dpdk_reg(funcs);
		break;

	default:
		rte_free(funcs);
		return -ENOTSUP;
	}

	run_conf->run_funcs = funcs;

	return 0;
}

static inline int
run_mode_launch(rb_conf *run_conf, int qid)
{
	run_func_t *funcs = run_conf->run_funcs;

	if (funcs->run)
		return funcs->run(run_conf, qid);

	return -EINVAL;
}

#endif /* _INCLUDE_RUN_MODE_H_ */
