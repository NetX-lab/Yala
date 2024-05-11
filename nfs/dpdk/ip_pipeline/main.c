/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2018 Intel Corporation
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>

#include <rte_launch.h>
#include <rte_eal.h>

#include "cli.h"
#include "conn.h"
#include "kni.h"
#include "cryptodev.h"
#include "link.h"
#include "mempool.h"
#include "pipeline.h"
#include "swq.h"
#include "tap.h"
#include "thread.h"
#include "tmgr.h"

#define STATS_INTERVAL_SEC	1
#define STATS_INTERVAL_CYCLES	STATS_INTERVAL_SEC * rte_get_timer_hz()
volatile uint64_t tot_pkts;
uint64_t prev_tot_pkts;
uint64_t tot_bytes;
uint64_t cycles;
uint64_t prev_cycles;
uint64_t start;
double run_time;
double rate;



static const char usage[] =
	"%s EAL_ARGS -- [-h HOST] [-p PORT] [-s SCRIPT]\n";

static const char welcome[] =
	"\n"
	"Welcome to IP Pipeline!\n"
	"\n";

static const char prompt[] = "pipeline> ";

static struct app_params {
	struct conn_params conn;
	char *script_name;
} app = {
	.conn = {
		.welcome = welcome,
		.prompt = prompt,
		.addr = "0.0.0.0",
		.port = 8086,
		.buf_size = 1024 * 1024,
		.msg_in_len_max = 1024,
		.msg_out_len_max = 1024 * 1024,
		.msg_handle = cli_process,
	},
	.script_name = NULL,
};

static int
parse_args(int argc, char **argv)
{
	char *app_name = argv[0];
	struct option lgopts[] = {
		{ NULL,  0, 0, 0 }
	};
	int opt, option_index;
	int h_present, p_present, s_present, n_args, i;

	/* Skip EAL input args */
	n_args = argc;
	for (i = 0; i < n_args; i++)
		if (strcmp(argv[i], "--") == 0) {
			argc -= i;
			argv += i;
			break;
		}

	if (i == n_args)
		return 0;

	/* Parse args */
	h_present = 0;
	p_present = 0;
	s_present = 0;

	while ((opt = getopt_long(argc, argv, "h:p:s:", lgopts, &option_index))
			!= EOF)
		switch (opt) {
		case 'h':
			if (h_present) {
				printf("Error: Multiple -h arguments\n");
				return -1;
			}
			h_present = 1;

			if (!strlen(optarg)) {
				printf("Error: Argument for -h not provided\n");
				return -1;
			}

			app.conn.addr = strdup(optarg);
			if (app.conn.addr == NULL) {
				printf("Error: Not enough memory\n");
				return -1;
			}
			break;

		case 'p':
			if (p_present) {
				printf("Error: Multiple -p arguments\n");
				return -1;
			}
			p_present = 1;

			if (!strlen(optarg)) {
				printf("Error: Argument for -p not provided\n");
				return -1;
			}

			app.conn.port = (uint16_t) atoi(optarg);
			break;

		case 's':
			if (s_present) {
				printf("Error: Multiple -s arguments\n");
				return -1;
			}
			s_present = 1;

			if (!strlen(optarg)) {
				printf("Error: Argument for -s not provided\n");
				return -1;
			}

			app.script_name = strdup(optarg);
			if (app.script_name == NULL) {
				printf("Error: Not enough memory\n");
				return -1;
			}
			break;

		default:
			printf(usage, app_name);
			return -1;
		}

	optind = 1; /* reset getopt lib */

	return 0;
}

int
main(int argc, char **argv)
{
	struct conn *conn;
	int status;

	/* Parse application arguments */
	status = parse_args(argc, argv);
	if (status < 0)
		return status;

	/* EAL */
	status = rte_eal_init(argc, argv);
	if (status < 0) {
		printf("Error: EAL initialization failed (%d)\n", status);
		return status;
	};

	/* Connectivity */
	conn = conn_init(&app.conn);
	if (conn == NULL) {
		printf("Error: Connectivity initialization failed (%d)\n",
			status);
		return status;
	};

	/* Mempool */
	status = mempool_init();
	if (status) {
		printf("Error: Mempool initialization failed (%d)\n", status);
		return status;
	}

	/* Link */
	status = link_init();
	if (status) {
		printf("Error: Link initialization failed (%d)\n", status);
		return status;
	}

	/* SWQ */
	status = swq_init();
	if (status) {
		printf("Error: SWQ initialization failed (%d)\n", status);
		return status;
	}

	/* Traffic Manager */
	status = tmgr_init();
	if (status) {
		printf("Error: TMGR initialization failed (%d)\n", status);
		return status;
	}

	/* TAP */
	status = tap_init();
	if (status) {
		printf("Error: TAP initialization failed (%d)\n", status);
		return status;
	}

	/* KNI */
	status = kni_init();
	if (status) {
		printf("Error: KNI initialization failed (%d)\n", status);
		return status;
	}

	/* Sym Crypto */
	status = cryptodev_init();
	if (status) {
		printf("Error: Cryptodev initialization failed (%d)\n",
				status);
		return status;
	}

	/* Action */
	status = port_in_action_profile_init();
	if (status) {
		printf("Error: Input port action profile initialization failed (%d)\n", status);
		return status;
	}

	status = table_action_profile_init();
	if (status) {
		printf("Error: Action profile initialization failed (%d)\n",
			status);
		return status;
	}

	/* Pipeline */
	status = pipeline_init();
	if (status) {
		printf("Error: Pipeline initialization failed (%d)\n", status);
		return status;
	}

	/* Thread */
	status = thread_init();
	if (status) {
		printf("Error: Thread initialization failed (%d)\n", status);
		return status;
	}

	rte_eal_mp_remote_launch(
		thread_main,
		NULL,
		SKIP_MAIN);



	/* Script */
	if (app.script_name)
		cli_script_process(app.script_name,
			app.conn.msg_in_len_max,
			app.conn.msg_out_len_max);

	tot_pkts = 0;
	prev_tot_pkts = 0;
	tot_bytes = 0;
	cycles = 0;
	prev_cycles = 0;
	start = rte_rdtsc();

	FILE* file;

	/* Dispatch loop */
	for ( ; ; ) {
		conn_poll_for_conn(conn);

		conn_poll_for_msg(conn);

		kni_handle_request();

		
		cycles = rte_rdtsc() - start;
		if (cycles - prev_cycles > STATS_INTERVAL_CYCLES) {
			run_time = (double)(cycles - prev_cycles) / rte_get_timer_hz();
			rate = ((tot_pkts-prev_tot_pkts) / run_time)/ 1000.0;
			printf("%.4lf\tKpps\n",rate);

			file = fopen("target_perf","a");
			fprintf(file,"%.4lf\tKpps\n",rate);
			fclose(file);

			prev_cycles = cycles;
			prev_tot_pkts = tot_pkts;
		}
		
	}
}
