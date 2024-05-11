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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_timer.h>

#include "conf.h"
#include "input.h"
#include "regex_dev.h"
#include "run_mode.h"
#include "rxpb_log.h"
#include "stats.h"
#include "utils.h"

#define ERR_STR_SIZE	50

volatile bool force_quit;

struct lcore_worker_args {
	rb_conf *run_conf;
	uint32_t qid;
};

static void
signal_handler(int signum)
{
	if (signum == SIGINT || signum == SIGTERM) {
		RXPB_LOG_INFO("Signal %d received, preparing to exit...", signum);
		force_quit = true;
	}
}

static int
init_dpdk(rb_conf *run_conf)
{
	int ret;

	if (run_conf->dpdk_argc <= 1) {
		RXPB_LOG_ERR("Too few DPDK parameters.");
		return -EINVAL;
	}

	ret = rte_eal_init(run_conf->dpdk_argc, run_conf->dpdk_argv);

	/* Return num of params on success. */
	return ret < 0 ? -rte_errno : 0;
}

static int
launch_worker(void *args)
{
	struct lcore_worker_args *worker_args = args;
	int ret;

	/* Kick off a run_mode thread for this worker. */
	ret = run_mode_launch(worker_args->run_conf, worker_args->qid);
	rte_free(args);

	return ret;
}

int
main(int argc, char **argv)
{
	uint64_t start_cycles, end_cycles;
	char err[ERR_STR_SIZE] = {0};
	rb_conf run_conf = {0};
	unsigned int lcore_id;
	uint32_t worker_qid;
	rb_stats_t *stats;
	double run_time;
	int ret;

	force_quit = false;
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	ret = conf_setup(&run_conf, argc, argv);
	if (ret)
		rte_exit(EXIT_FAILURE, "Configuration error\n");

	ret = init_dpdk(&run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Failed to init DPDK");
		goto clean_conf;
	}

	/* Confirm there are enough DPDK lcores for user core request. */
	if (run_conf.cores > rte_lcore_count()) {
		RXPB_LOG_WARN_REC(&run_conf, "requested cores (%d) > dpdk lcores (%d) - using %d.", run_conf.cores,
				  rte_lcore_count(), rte_lcore_count());
		run_conf.cores = rte_lcore_count();
	}

	ret = stats_init(&run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Failed initialising stats");
		goto clean_conf;
	}

	ret = input_register(&run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Input registration error");
		goto clean_stats;
	}

	ret = input_init(&run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Failed reading input file");
		goto clean_stats;
	}

	ret = acc_dev_register(&run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Regex dev registration error");
		goto clean_input;
	}

	ret = regex_dev_compile_rules(&run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Regex dev rule compilation error");
		goto clean_input;
	}

	ret = acc_dev_init(&run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Failed initialising regex device");
		goto clean_input;
	}

	ret = run_mode_register(&run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Run mode registration error");
		goto clean_regex;
	}

	/* Main core gets regex queue 0 and stats position 0. */
	stats = run_conf.stats;
	stats->rm_stats[0].lcore_id = rte_get_main_lcore();

	worker_qid = 1;

	RXPB_LOG_INFO("Beginning Processing...");
	run_conf.running = true;
	start_cycles = rte_get_timer_cycles();

	/* Start each worker lcore. */
	RTE_LCORE_FOREACH_WORKER(lcore_id) {
		struct lcore_worker_args *worker_args;

		if (worker_qid >= run_conf.cores)
			break;

		worker_args = rte_malloc(NULL, sizeof(*worker_args), 0);
		if (!worker_args) {
			RXPB_LOG_WARN_REC(&run_conf, "Lcore %d launch failed (mem error)", lcore_id);
			continue;
		}

		stats->rm_stats[worker_qid].lcore_id = lcore_id;
		worker_args->run_conf = &run_conf;
		worker_args->qid = worker_qid++;
		rte_eal_remote_launch(launch_worker, worker_args, lcore_id);
	}

	/* Start processing on the main lcore. */
	ret = run_mode_launch(&run_conf, 0);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Failure in run mode");
		goto clean_regex;
	}

	/* Wait on all threads/lcore processing to complete. */
	RTE_LCORE_FOREACH_WORKER(lcore_id) {
		ret = rte_eal_wait_lcore(lcore_id);
		if (ret)
			snprintf(err, ERR_STR_SIZE, "Lcore %u returned a runtime error", lcore_id);
	}

	end_cycles = rte_get_timer_cycles();
	run_time = ((double)end_cycles - start_cycles) / rte_get_timer_hz();

	stats_print_end_of_run(&run_conf, run_time);
clean_regex:
	regex_dev_clean_regex(&run_conf);
clean_input:
	input_clean(&run_conf);
clean_stats:
	stats_clean(&run_conf);
clean_conf:
	conf_clean(&run_conf);

	if (ret)
		rte_exit(EXIT_FAILURE, "%s\n", err);
	else
		rte_eal_cleanup();

	return ret;
}
