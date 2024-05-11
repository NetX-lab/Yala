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

#ifndef _INCLUDE_RULES_FILE_UTILS_H_
#define _INCLUDE_RULES_FILE_UTILS_H_

#include "conf.h"

enum rules_file_utils_type {
	RULES_FILE_RXP,
	RULES_FILE_HS,
	RULES_FILE_UNKNOWN
};

int rules_file_utils_convert_rules(rb_conf *run_conf, const char *raw_rules, const char **output_file,
				   enum rules_file_utils_type output);

int rules_file_utils_parse_rule(char *rule, char **exp, char **id, char **flags, enum rules_file_utils_type type);

int rules_file_compile_for_rxp(rb_conf *run_conf);

#endif /* _INCLUDE_RULES_FILE_UTILS_H_ */
