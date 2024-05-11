/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES, ALL RIGHTS RESERVED.
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
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <rte_malloc.h>

#include "input.h"
#include "utils.h"

static int
input_remote_mmap_init(rb_conf *run_conf)
{
	const char *file = run_conf->input_file;
	uint64_t data_length;
	char *data, *read_ptr;
	int ret;
	uint64_t remote_start_addr;
	uint64_t remote_data_len;

	/* Export file has two strings followed by a binary blob, read all of it as a binary blob first */
	ret = util_load_file_to_buffer(file, &data, &data_length, 0);
	if (ret)
		return ret;

	read_ptr = data;

	/* extract out the two fixed length strings */
	memcpy(&remote_start_addr, read_ptr, sizeof(uint64_t));
	read_ptr += sizeof(uint64_t);

	memcpy(&remote_data_len, read_ptr, sizeof(uint64_t));
	read_ptr += sizeof(uint64_t);

	run_conf->input_data = (char *)(uintptr_t *)remote_start_addr;
	run_conf->input_data_len = remote_data_len;

	/* extract out the variable length binary blob */
	run_conf->remote_mmap_desc_len = data_length - (2 * sizeof(size_t));
	run_conf->remote_mmap_desc = rte_malloc(NULL, run_conf->remote_mmap_desc_len, 0);
	if (run_conf->remote_mmap_desc == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	memcpy(run_conf->remote_mmap_desc, read_ptr, run_conf->remote_mmap_desc_len);
	rte_free(data);

	return 0;

err:
	rte_free(run_conf->remote_mmap_desc);
	run_conf->remote_mmap_desc = NULL;
	run_conf->input_data = NULL;
	run_conf->input_data_len = 0;
	run_conf->remote_mmap_desc_len = 0;
	rte_free(data);
	return ret;
}

static void
input_remote_mmap_clean(rb_conf *run_conf)
{
	rte_free(run_conf->remote_mmap_desc);
}

void
input_remote_mmap_reg(input_func_t *funcs)
{
	funcs->init = input_remote_mmap_init;
	funcs->clean = input_remote_mmap_clean;
}
