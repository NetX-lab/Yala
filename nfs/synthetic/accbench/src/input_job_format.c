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
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>

#include <rte_malloc.h>

#include "input.h"
#include "regex_dev.h"
#include "rxpb_log.h"
#include "utils.h"

/* File should be of the form job_0xXXXXXXXX.yyy */
#define JOB_FILE_AND_DIR_LEN	150
#define JOB_FILE_DES_EXT	".des"
#define JOB_FILE_PKT_EXT	".pkt"
#define JOB_FILE_EXP_EXT	".exp"
/* Assumes all extentsions are same length. */
#define JOB_FILE_EXT_LEN	(sizeof(JOB_FILE_DES_EXT) - 1)
#define JOB_FILE_PREFIX		"job_0x"
#define JOB_FILE_PREFIX_LEN	(sizeof(JOB_FILE_PREFIX) - 1)
#define JOB_FILE_NUM_LEN	8
#define JOB_FILE_NAME_LEN	JOB_FILE_PREFIX_LEN + JOB_FILE_NUM_LEN + JOB_FILE_EXT_LEN

static int
input_job_format_filter_des(const struct dirent *file)
{
	int i;

	if (strlen(file->d_name) != JOB_FILE_NAME_LEN)
		return 0;

	/* Check for desciptor extension. */
	if (strcmp(&file->d_name[JOB_FILE_PREFIX_LEN + JOB_FILE_NUM_LEN], JOB_FILE_DES_EXT))
		return 0;

	/* Validate the prefix. */
	if (strncmp(file->d_name, JOB_FILE_PREFIX, JOB_FILE_PREFIX_LEN))
		return 0;

	/* Validate hex string. */
	for (i = 0; i < JOB_FILE_NUM_LEN; i++)
		if (!isxdigit(file->d_name[JOB_FILE_PREFIX_LEN + i]))
			return 0;

	return 1;
}

static inline int
input_job_format_extract_des(char *file_name, uint16_t *job_len, uint64_t *job_id, uint16_t **job_subsets)
{
	char *des_line = NULL;
	char *line = NULL;
	ssize_t line_len;
	size_t len = 0;
	bool have_data;
	char *csv_tok;
	FILE *des;
	long dec;
	int i;

	des = fopen(file_name, "r");
	if (!des) {
		RXPB_LOG_ERR("Failed to open descriptor file: %s.", file_name);
		return -ENOTSUP;
	}

	have_data = false;

	/* Line and len of NULL/0 forces getline to malloc buffer. */
	while ((line_len = getline(&line, &len, des)) != -1) {
		if (!line_len)
			continue;

		des_line = util_trim_whitespace(line);
		if (des_line[0] == '\0' || des_line[0] == '#')
			continue;

		have_data = true;

		/* Job_id. */
		csv_tok = strtok(des_line, ",");
		if (!csv_tok)
			goto parsing_error;

		if (util_str_to_dec(util_trim_whitespace(csv_tok), &dec, sizeof(uint64_t))) {
			RXPB_LOG_ERR("Invalid job id: %s - in %s.", csv_tok, file_name);
			goto error;
		}
		*job_id = dec;

		/* Flow_id - ignore. */
		csv_tok = strtok(NULL, ",");
		if (!csv_tok)
			goto parsing_error;

		/* Ctrl - ignore. */
		csv_tok = strtok(NULL, ",");
		if (!csv_tok)
			goto parsing_error;

		/* Job_length */
		csv_tok = strtok(NULL, ",");
		if (!csv_tok)
			goto parsing_error;

		if (util_str_to_dec(util_trim_whitespace(csv_tok), &dec, sizeof(uint16_t)) || dec == 0) {
			RXPB_LOG_ERR("Invalid length: %s - in %s.", csv_tok, file_name);
			goto error;
		}
		*job_len = dec;

		/* Subsets. */
		for (i = 0; i < MAX_SUBSET_IDS; i++) {
			csv_tok = strtok(NULL, ",");
			if (!csv_tok)
				goto parsing_error;

			if (util_str_to_dec(util_trim_whitespace(csv_tok), &dec, sizeof(uint16_t))) {
				RXPB_LOG_ERR("Invalid subset: %s - in %s.", csv_tok, file_name);
				goto error;
			}
			(*job_subsets)[i] = dec;
		}

		/* Only verfiy subset 1 is non zero. */
		if (!*job_subsets[0]) {
			RXPB_LOG_ERR("Invalid subset id 0 - in %s.", file_name);
			goto error;
		}

		/* Confirm end of line. */
		csv_tok = strtok(NULL, ",");
		if (csv_tok)
			goto parsing_error;
	}

	if (!have_data)
		goto parsing_error;

	free(line);
	fclose(des);

	return 0;

parsing_error:
	RXPB_LOG_ERR("Bad descriptor format: %s - in: %s.", line, file_name);
error:
	free(line);
	fclose(des);

	return -EINVAL;
}

static inline int
input_job_format_extract_pkt(const char *file_name, uint16_t expected_length, char *data_ptr)
{
	char hex_byte[] = "00";
	char *pkt_line = NULL;
	char *line = NULL;
	ssize_t line_len;
	size_t len = 0;
	bool have_data;
	FILE *pkt;
	int i;

	pkt = fopen(file_name, "r");
	if (!pkt) {
		RXPB_LOG_ERR("Failed to open packet file: %s.", file_name);
		return -ENOTSUP;
	}

	have_data = false;

	/* Line and len of NULL/0 forces getline to malloc buffer. */
	while ((line_len = getline(&line, &len, pkt)) != -1) {
		if (!line_len)
			continue;

		pkt_line = util_trim_whitespace(line);
		if (pkt_line[0] == '\0' || pkt_line[0] == '#')
			continue;

		have_data = true;
		line_len = strlen(pkt_line);

		/* Pkt data is in hex so should be double byte count. */
		if (line_len != expected_length * 2) {
			RXPB_LOG_ERR("Data length: %lu != Des length: %u.", (line_len / 2), expected_length);
			goto error;
		}

		/* Parse hex file to bytes. */
		for (i = 0; i < line_len; i += 2) {
			if (!isxdigit(pkt_line[i]) || !isxdigit(pkt_line[i+1])) {
				RXPB_LOG_ERR("Invalid hex digit: %c%c.", pkt_line[i], pkt_line[i+1]);
				goto error;
			}

			hex_byte[0] = pkt_line[i+0];
			hex_byte[1] = pkt_line[i+1];
			data_ptr[i/2] = (char)strtol(hex_byte, 0, 16);
		}
	}

	if (!have_data) {
		RXPB_LOG_ERR("No data detected.");
		goto error;
	}

	free(line);
	fclose(pkt);

	return 0;

error:
	RXPB_LOG_ERR("Bad packet format in: %s.", file_name);
	free(line);
	fclose(pkt);

	return -EINVAL;
}

static inline int
input_job_format_extract_exp(char *file_name, exp_matches_t *exp_matches)
{
	exp_match_t *exp_match = NULL;
	char *exp_line = NULL;
	char *line = NULL;
	ssize_t line_len;
	size_t len = 0;
	char *csv_tok;
	int exp_cnt;
	FILE *exp;
	long dec;
	int i;

	exp = fopen(file_name, "r");
	if (!exp) {
		RXPB_LOG_ERR("Could not open expected file: %s.", file_name);
		return -ENOTSUP;
	}

	exp_cnt = 0;

	/* Parse file to get number of valid lines. */
	while ((line_len = getline(&line, &len, exp)) != -1) {
		/* First call to getline will malloc a buffer. Subsequent calls realloc it if required. */
		if (!line_len)
			continue;

		exp_line = util_trim_whitespace(line);
		if (exp_line[0] == '\0' || exp_line[0] == '#')
			continue;

		exp_cnt++;
	}

	/* No expected matches can be a valid input - return leaving init'd fields 0/NULL. */
	if (!exp_cnt)
		goto out;

	exp_match = rte_malloc(NULL, sizeof(exp_match_t) * exp_cnt, 0);
	if (!exp_match) {
		RXPB_LOG_ERR("Memory failure on expect matches.");
		goto error;
	}

	rewind(exp);
	i = 0;
	while ((line_len = getline(&line, &len, exp)) != -1) {
		if (!line_len)
			continue;

		exp_line = util_trim_whitespace(line);
		if (exp_line[0] == '\0' || exp_line[0] == '#')
			continue;

		/* Job_id - ignore. */
		csv_tok = strtok(exp_line, ",");
		if (!csv_tok)
			goto parsing_error;

		/* Rule_id. */
		csv_tok = strtok(NULL, ",");
		if (!csv_tok)
			goto parsing_error;

		if (util_str_to_dec(util_trim_whitespace(csv_tok), &dec, sizeof(uint32_t))) {
			RXPB_LOG_ERR("Bad rule id: %s - in %s.", csv_tok, file_name);
			goto error;
		}
		exp_match[i].rule_id = dec;

		/* Start_ptr. */
		csv_tok = strtok(NULL, ",");
		if (!csv_tok)
			goto parsing_error;

		if (util_str_to_dec(util_trim_whitespace(csv_tok), &dec, sizeof(uint16_t))) {
			RXPB_LOG_ERR("Bad start ptr: %s - in %s.", csv_tok, file_name);
			goto error;
		}
		exp_match[i].start_ptr = dec;

		/* Length. */
		csv_tok = strtok(NULL, ",");
		if (!csv_tok)
			goto parsing_error;

		if (util_str_to_dec(util_trim_whitespace(csv_tok), &dec, sizeof(uint16_t))) {
			RXPB_LOG_ERR("Bad length %s - in %s.", csv_tok, file_name);
			goto error;
		}
		exp_match[i].length = dec;

		i++;
	}

	exp_matches->num_matches = exp_cnt;
	exp_matches->matches = exp_match;

out:
	free(line);
	fclose(exp);

	return 0;

parsing_error:
	RXPB_LOG_ERR("Bad exp match format: %s - in: %s.", line, file_name);
error:
	rte_free(exp_match);
	free(line);
	fclose(exp);

	return -EINVAL;
}

static int
input_job_format_read(rb_conf *run_conf)
{
	struct dirent **des_file_name_list = NULL;
	exp_matches_t *job_exp_matches = NULL;
	char dir_file[JOB_FILE_AND_DIR_LEN];
	char *dir = run_conf->input_file;
	uint16_t **job_subset_ids = NULL;
	uint16_t *job_lens = NULL;
	uint64_t *job_ids = NULL;
	uint32_t num_des_files;
	uint64_t total_bytes;
	struct stat buffer;
	int no_exp_files;
	int exp_files;
	char *data;
	uint32_t i;
	int ret;

	/* Check we will not overflow directory path + file name limit. */
	if (strlen(dir) + JOB_FILE_NAME_LEN + 1 >= JOB_FILE_AND_DIR_LEN) {
		RXPB_LOG_ERR("Job directory exceeds character limit: %s.", dir);
		return -EINVAL;
	}

	/* Get all .des files from directory. */
	ret = scandir(dir, &des_file_name_list, input_job_format_filter_des, alphasort);
	if (ret <= 0) {
		RXPB_LOG_ERR("Failed to read job files in directory: %s.", dir);
		return -errno;
	}

	num_des_files = ret;

	/* Limit number of jobs if requested by user. */
	if (run_conf->input_packets && run_conf->input_packets < num_des_files)
		num_des_files = run_conf->input_packets;

	/* Allocate memory to store job lengths, ids, and subsets. */
	job_lens = rte_malloc(NULL, sizeof(uint16_t) * num_des_files, 0);
	if (!job_lens) {
		RXPB_LOG_ERR("Memory failure allocating job format lens.");
		ret = -ENOMEM;
		goto free_namelist;
	}

	job_ids = rte_malloc(NULL, sizeof(uint64_t) * num_des_files, 0);
	if (!job_ids) {
		RXPB_LOG_ERR("Memory failure allocating job format ids.");
		ret = -ENOMEM;
		goto free_des_data;
	}

	job_subset_ids = rte_zmalloc(NULL, sizeof(uint16_t *) * num_des_files, 0);
	if (!job_subset_ids) {
		RXPB_LOG_ERR("Memory failure allocating job format subsets ids.");
		ret = -ENOMEM;
		goto free_des_data;
	}

	for (i = 0; i < num_des_files; i++) {
		job_subset_ids[i] = rte_malloc(NULL, sizeof(uint16_t) * MAX_SUBSET_IDS, 0);
		if (!job_subset_ids[i]) {
			RXPB_LOG_ERR("Memory failure allocating job format subsets ids.");
			ret = -ENOMEM;
			goto free_des_data;
		}
	}

	/* Verify and parse each descriptor file. */
	total_bytes = 0;
	no_exp_files = 0;
	exp_files = 0;
	for (i = 0; i < num_des_files; i++) {
		ret = snprintf(dir_file, JOB_FILE_AND_DIR_LEN, "%s//%s", dir, des_file_name_list[i]->d_name);
		if (ret < 0) {
			RXPB_LOG_ERR("Failed to create des file location.");
			ret = -ENOMEM;
			goto free_des_data;
		}

		ret = input_job_format_extract_des(dir_file, &job_lens[i], &job_ids[i], &job_subset_ids[i]);
		if (ret)
			goto free_des_data;

		/* If we pass a user byte limit then stop reading files and update the number of descriptor to use. */
		if (run_conf->input_bytes && run_conf->input_bytes < total_bytes + job_lens[i]) {
			num_des_files = i;
			break;
		}

		/* Check if expected match files are included. */
		strcpy(&dir_file[strlen(dir_file) - JOB_FILE_EXT_LEN], JOB_FILE_EXP_EXT);
		if (stat(dir_file, &buffer) == 0)
			exp_files++;
		else
			no_exp_files++;

		total_bytes += job_lens[i];
	}

	if (exp_files && no_exp_files) {
		RXPB_LOG_ERR("%u des files have exp files while %u do not - exp files must be for all or none.",
			exp_files, no_exp_files);
		ret = -EINVAL;
		goto free_des_data;
	}

	data = rte_malloc(NULL, total_bytes, 0);
	if (!data) {
		RXPB_LOG_ERR("Failed to allocate memory for job data.");
		ret = -ENOMEM;
		goto free_des_data;
	}

	if (exp_files) {
		job_exp_matches = rte_zmalloc(NULL, sizeof(*job_exp_matches) * num_des_files, 0);
		if (!job_exp_matches) {
			RXPB_LOG_ERR("Failed to allocate exp match memory for job data.");
			ret = -ENOMEM;
			goto free_data;
		}
	}

	/* Extract and copy the data files. */
	total_bytes = 0;
	for (i = 0; i < num_des_files; i++) {
		ret = snprintf(dir_file, JOB_FILE_AND_DIR_LEN, "%s//%s", dir, des_file_name_list[i]->d_name);
		if (ret < 0) {
			RXPB_LOG_ERR("Failed to create pkt file location.");
			ret = -ENOMEM;
			goto free_exp_matches;
		}

		/* Assumes the exts are the same length so just overwrite. */
		strcpy(&dir_file[strlen(dir_file) - JOB_FILE_EXT_LEN], JOB_FILE_PKT_EXT);

		ret = input_job_format_extract_pkt(dir_file, job_lens[i], &data[total_bytes]);
		if (ret)
			goto free_exp_matches;

		total_bytes += job_lens[i];

		if (exp_files) {
			/* Record expected matches if files exist. */
			strcpy(&dir_file[strlen(dir_file) - JOB_FILE_EXT_LEN], JOB_FILE_EXP_EXT);
			ret = input_job_format_extract_exp(dir_file, &job_exp_matches[i]);
			if (ret)
				goto free_exp_matches;
		}
	}

	run_conf->input_data = data;
	run_conf->input_data_len = total_bytes;
	run_conf->input_lens = job_lens;
	run_conf->input_len_cnt = num_des_files;
	run_conf->input_job_ids = job_ids;
	run_conf->input_subset_ids = job_subset_ids;
	run_conf->input_exp_matches = job_exp_matches;

	/* Clean list allocated by scandir. */
	for (i = 0; i < num_des_files; i++)
		free(des_file_name_list[i]);
	free(des_file_name_list);

	return 0;

free_exp_matches:
	if (job_exp_matches) {
		for (i = 0; i < num_des_files; i++)
			rte_free(job_exp_matches[i].matches);
		rte_free(job_exp_matches);
	}
free_data:
	rte_free(data);
free_des_data:
	if (job_subset_ids) {
		for (i = 0; i < num_des_files; i++)
			rte_free(job_subset_ids[i]);
		rte_free(job_subset_ids);
	}
	rte_free(job_ids);
	rte_free(job_lens);
free_namelist:
	for (i = 0; i < num_des_files; i++)
		free(des_file_name_list[i]);
	free(des_file_name_list);

	return ret;
}

static void
input_job_format_clean(rb_conf *run_conf)
{
	uint32_t i, num_entries;

	num_entries = run_conf->input_len_cnt;
	rte_free(run_conf->input_data);
	rte_free(run_conf->input_lens);
	rte_free(run_conf->input_job_ids);
	if (run_conf->input_subset_ids)
		for (i = 0; i < num_entries; i++)
			rte_free(run_conf->input_subset_ids[i]);
	if (run_conf->input_exp_matches)
		for (i = 0; i < num_entries; i++)
			rte_free(run_conf->input_exp_matches[i].matches);
}

void
input_job_format_reg(input_func_t *funcs)
{
	funcs->init = input_job_format_read;
	funcs->clean = input_job_format_clean;
}
