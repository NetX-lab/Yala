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

#include <rte_lcore.h>

#include "regex_dev.h"
#include "run_mode.h"
#include "stats.h"



static int
run_mode_preloaded(rb_conf *run_conf, int qid)
{
	const double max_rate = run_conf->rate;
	const uint32_t max_duration = run_conf->input_duration;
	const uint32_t max_buf_len = run_conf->input_buf_len;
	const uint32_t max_iter = run_conf->input_iterations;
	const uint32_t total_lens = run_conf->input_len_cnt;
	uint32_t batch_size;
	const uint32_t data_len = run_conf->input_data_len;
	const uint32_t overlap = run_conf->input_overlap;
	const uint16_t *lens = run_conf->input_lens;
	rb_stats_t *stats = run_conf->stats;
	char *data = run_conf->input_data;
	run_mode_stats_t *rm_stats;
	regex_stats_t *regex_stats;
	uint64_t prev_cycles;
	uint64_t prev_cycles_print;
	uint64_t max_cycles;
	uint32_t batch_cnt;
	uint32_t data_off;
	uint32_t iter_cnt;
	uint32_t buf_len;
	uint32_t len_cnt;
	bool main_lcore;
	uint64_t cycles;
	double run_time;
	bool file_done;
	uint64_t start;
	int ret;

	double run_time_test;
	double temp=0;
	double rate=0;

	/* Convert duration to cycles. */
	max_cycles = max_duration * rte_get_timer_hz();
	prev_cycles = 0;
	prev_cycles_print = 0;
	cycles = 0;

	main_lcore = rte_lcore_id() == rte_get_main_lcore();

	iter_cnt = 0;
	rm_stats = &stats->rm_stats[qid];
	regex_stats = &stats->regex_stats[qid];

	// test: use static mbufs
	uint64_t total_buf_len = 0;
	batch_cnt = 0;

	batch_size = run_conf->nb_preloaded_bufs;
	// printf("batch_size=%d\n",batch_size);

	/* Prepare a batch of packets beforehand. Preloaded buffers are loaded only once */
	while(batch_cnt < batch_size){
		file_done = false;
		data_off = 0;
		len_cnt = 0;
		while (batch_cnt < batch_size && !file_done) {
			/* manual packet lengths take priority. */
			if (total_lens) {
				buf_len = lens[len_cnt];
				len_cnt++;

				if (len_cnt == total_lens) {
					file_done = true;
				}
			} else if (data_off + max_buf_len >= data_len) {
				buf_len = data_len - data_off;
				file_done = true;
			} else
				buf_len = max_buf_len;

			total_buf_len += buf_len;
			/* prepare ops by allocating and populating mbufs */
			ret = regex_dev_search(run_conf, qid, &data[data_off], buf_len, false, regex_stats);
			batch_cnt++;
			data_off += buf_len;
			data_off -= overlap;
		}
	}
		
	// if (main_lcore)
	// 	stats_print_update(stats, run_conf->cores, 0.0, false);
	start = rte_rdtsc();

	while (!force_quit && iter_cnt < max_iter && (!max_cycles || cycles <= max_cycles)) {
		file_done = false;
		batch_cnt = 0;
		data_off = 0;
		len_cnt = 0;

		while (!force_quit && !file_done && (!max_cycles || cycles <= max_cycles)) {

			cycles = rte_rdtsc() - start;

			if (cycles - prev_cycles > STATS_INTERVAL_CYCLES) {
				run_time = (double)cycles / rte_get_timer_hz();
				prev_cycles = cycles;
				// stats_print_update(stats, run_conf->cores, run_time, false);
				stats_print_update_simple(stats, run_conf->cores, run_time, false);
			}

			/* rate limiting */
			run_time = (double)(cycles) / rte_get_timer_hz();
			rate = (((rm_stats->rx_buf_bytes) * 8) / run_time)/ 1000000000.0;
			if(!max_rate){
				;
			}
			else{
				if(rate >= max_rate) {
					continue;
				}
			}

			/* send ops to device */
			ret = regex_dev_search(run_conf, qid, NULL, batch_size, true, regex_stats);
			if (ret)
				return ret;

			rm_stats->tx_buf_cnt += batch_size;
			rm_stats->tx_buf_bytes += total_buf_len;
			rm_stats->rx_buf_cnt += batch_size;
			rm_stats->rx_buf_bytes += total_buf_len;


			if (!main_lcore)
				continue;

		}

		iter_cnt++;
	}

	/* Wait on results from any ops that are in flight. */
	regex_dev_post_search(run_conf, qid, regex_stats);

	return 0;
}

void
run_mode_preloaded_reg(run_func_t *funcs)
{
	funcs->run = run_mode_preloaded;
}
