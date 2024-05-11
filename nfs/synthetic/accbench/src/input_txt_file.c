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

#include "input.h"
#include "utils.h"

static int
input_txt_file_read(rb_conf *run_conf)
{
	const char *file = run_conf->input_file;
	uint64_t data_length;
	char *data;
	int ret;

	ret = util_load_file_to_buffer(file, &data, &data_length, run_conf->input_bytes);
	if (ret)
		return ret;

	run_conf->input_data = data;
	run_conf->input_data_len = data_length;

	return 0;
}

static void
input_txt_file_clean(rb_conf *run_conf)
{
	rte_free(run_conf->input_data);
}

void
input_txt_file_reg(input_func_t *funcs)
{
	funcs->init = input_txt_file_read;
	funcs->clean = input_txt_file_clean;
}
