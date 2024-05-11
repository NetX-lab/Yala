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

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

//#include <doca_version.h>

#include <rte_ethdev.h>
#include <rte_malloc.h>
#include <rte_string_fns.h>

#include <click/dpdkbfregex_conf.h>
#include <click/dpdkbfregex_dpdk_live_shared.h>
#include <click/dpdkbfregex_rxpb_log.h>
#include <click/dpdkbfregex_utils.h>

#define MODULE_VERSION       "22.10"

/* Default selections if user does not input values. */
#define DEFAULT_BATCH_SIZE     64
#define DEFAULT_BUF_LEN	       1024
#define DEFAULT_ITERATIONS     1
#define DEFAULT_CORES	       1
#define DEFAULT_SLIDING_WINDOW 32

#define CONFIG_FILE_LINE_LEN   200
#define CONFIG_FILE_MAX_ARGS   100

/* Default config file - can be overwritten from input parameters. */
static char *conf_file;

static bool raw_rule_cmd_line;

static void
conf_init(rb_conf *conf)
{
	/* User is required to specify device and input mode. */
	conf->regex_dev_type = REGEX_DEV_UNKNOWN;
	conf->input_mode = INPUT_UNKNOWN;

	conf_file = NULL;
}

static int
conf_extract_regex_pcie_addr(rb_conf *run_conf, char *dpdk_pcie)
{
	char *pcie = NULL;
	char *comma_ptr;

	/* Do not replace an already verified PCIe address. */
	if (run_conf->regex_pcie)
		return 0;

	/* DPDK PCIe may include the ,class=regex modifier. */
	comma_ptr = strchr(dpdk_pcie, ',');
	if (!comma_ptr)
		pcie = strdup(dpdk_pcie);
	else {
		int idx;

		idx = comma_ptr - dpdk_pcie + 1;
		pcie = strndup(dpdk_pcie, idx - 1);
	}

	if (!pcie) {
		RXPB_LOG_ERR("Memory failure getting regex pcie addr.");
		return -ENOMEM;
	}

	if (strlen(pcie) < 3) {
		RXPB_LOG_ERR("Invalid PCIe address extracted: %s.", pcie);
		free(pcie);
		return -EINVAL;
	}

	/* Function value for Regex should be 0. */
	if (pcie[strlen(pcie) - 2] != '.' || pcie[strlen(pcie) - 1] != '0') {
		free(pcie);
		/* Not a fail, just not the PCIe we're looking for. */
		return 0;
	}

	run_conf->regex_pcie = pcie;

	return 0;
}

static int
conf_parse_dpdk_params(rb_conf *run_conf, char *prgname, char *params)
{
	bool check_for_regex_dev = false;
	char *dpdk_arg;
	int ret;
	//printf("parsing dpdk parameters: %s\n",params);
	/* Do not overwrite already entered/higher priority params. */
	if (run_conf->dpdk_argc)
		return 0;

	if (!params) {
		RXPB_LOG_ERR("DPDK EAL options not detected.");
		return -EINVAL;
	}
	/* Remove quotes at start/end if they exist (e.g. in config file). */
	if (params[0] == '"' && params[strlen(params) - 1] == '"') {
		params[strlen(params) - 1] = '\0';
		params++;
	}
	run_conf->dpdk_argv[0] = prgname;
	run_conf->dpdk_argc = 1;
	dpdk_arg = strtok(params, " ");
	while (dpdk_arg != NULL) {
		if (check_for_regex_dev) {
			ret = conf_extract_regex_pcie_addr(run_conf, dpdk_arg);
			if (ret)
				return ret;
			check_for_regex_dev = false;
		} else if (strcmp(dpdk_arg, "-a") == 0) {
			check_for_regex_dev = true;
		}

		run_conf->dpdk_argv[run_conf->dpdk_argc] = strdup(dpdk_arg);
		if (!run_conf->dpdk_argv[run_conf->dpdk_argc]) {
			RXPB_LOG_ERR("Memory failure copying dpdk args.");
			return -ENOMEM;
		}

		run_conf->dpdk_argc++;
		dpdk_arg = strtok(NULL, " ");

		if (run_conf->dpdk_argc >= MAX_DPDK_ARGS) {
			RXPB_LOG_ERR("DPDK EAL options exceed max.");
			return -ENOTSUP;
		}
	}

	return 0;
}

/* Validate and convert optarg to uint32_t. */
static inline int
conf_set_uint32_t(uint32_t *dest, char opt, char *optarg)
{
	long tmp;

	/* If non zero then ignore as it has already been set. */
	if (*dest)
		return 0;

	if (util_str_to_dec(optarg, &tmp, sizeof(uint32_t))) {
		RXPB_LOG_ERR("invalid param -%c %s.", opt, optarg);
		return -EINVAL;
	}

	/* Prevent config values being set to 0. */
	if (!tmp)
		return 0;

	*dest = tmp;

	return 0;
}

/* Validate and store optarg as config string. */
static inline int
conf_set_string(char **dest, char *optarg)
{
	/* If non NULL then it has already been set so don't overwrite. */
	if (*dest)
		return 0;

	/* Remove quotes at start/end if they exist (e.g. in config file). */
	if (optarg[0] == '"' && optarg[strlen(optarg) - 1] == '"') {
		optarg[strlen(optarg) - 1] = '\0';
		optarg++;
	}

	*dest = strdup(optarg);
	if (!*dest) {
		RXPB_LOG_ERR("Memory failure copying optarg.");
		return -ENOMEM;
	}

	return 0;
}

/* Display usage. */
static void
rxpbench_usage(const char *prgname)
{
	fprintf(stdout,
		"%s -d REGEX_DEV -m INPUT [options...]\n"
		"General ops:\n"
		"\t--config-file (-C): conf file (default is rxpbench.conf)\n"
		"\t--dpdk-eal (-D): dpdk params as quoted string (\"..\")\n"
		"\t--verbose (-V): create match files (1: csv 2: hex 3: ascii)\n"
		"\t--cores (-c): number of CPU cores to use\n"
		"Configuration:\n"
		"\t--regex-dev (-d): 'regex_dpdk'/'rxp', 'hyperscan'/'hs' or 'doca_regex'/'doca'\n"
		"\t--input-mode (-m): 'dpdk_port', 'pcap_file', 'text_file', 'job_format' or 'remote_mmap'\n"
		"\t--input-file (-f): pcap, text file, job directory, or remote memory export definition to use\n"
		"\t--rules (-r): regex rules file (compiled)\n"
		"\t--raw-rules (-R): regex rules file (uncompiled)\n"
		"Run Specific:\n"
		"\t--run-time-secs (-s): time to run in secs\n"
		"\t--run-num-iterations (-n): num parses of file (file mode)\n"
		"\t--run-packets (-p): packets/jobs to read (pcap, live and job_format mode)\n"
		"\t--run-bytes (-b): max bytes to read in file or from network\n"
		"\t--run-app-layer (-A): use per packet app layer for buffers\n"
		"Search Specific:\n"
		"\t--buf-length (-l): buffer size to process (file mode)\n"
		"\t--buf-thres (-t): minimum buf size to process (live mode)\n"
		"\t--buf-overlap (-o): byte overlap in buffers (file mode)\n"
		"\t--buf-group (-g): num of buffers in group/batch to process\n"
		"\t--sliding-window (-w): overlap if job > max size and needs split (doca regex mode)\n"
		"Regex DPDK/DOCA Specific:\n"
		"\t--latency-mode (-8): run in mode focusing on latency over throughput (rxp or doca mode)\n"
		"Hyperscan Specific:\n"
		"\t--hs-singlematch (-H): (no arg) apply HS_FLAG_SINGLEMATCH\n"
		"\t--hs-leftmost (-L): (no arg) apply HS_FLAGS_SOM_LEFTMOST\n"
		"Regex Compilation (Globbal Settings):\n"
		"\t--force-compile (-F): (no arg) do not stop on compile fails\n"
		"\t--comp-single-line (-S): (no arg) turn on single-line mode (new line does not match .)\n"
		"\t--comp-caseless (-i): (no arg) turn on caseless mode (rules are case insensitive)\n"
		"\t--comp-multi-line (-u): (no arg) turn on multi-line mode (anchors are applied per line)\n"
		"\t--comp-free-space (-x): (no arg) turn on free-spacing mode (ignore whitespace in rules)\n"
		"DPDK Port Specific:\n"
		"\t--dpdk-primary-port (-1): dpdk port to use in live mode\n"
		"\t--dpdk-second-port (-2): second dpdk port to use\n"
		"Support:\n"
		"\t--help (-h): print rxpbench options\n"
		"\t--version (-v): return version information and exit\n"
		"\n",
		prgname);
}

/* Display version information. */
static void
rxpbench_version(void)
{
	//RXPB_LOG_INFO("RXPBENCH VERSION %s  (%s)", MODULE_VERSION, GIT_SHA);
	RXPB_LOG_INFO("RXPBENCH VERSION %s", MODULE_VERSION);
	RXPB_LOG_INFO("Build time - %s, %s", __DATE__, __TIME__);
	//RXPB_LOG_INFO("DOCA VERSION %s", DOCA_VER_STRING);
}

static struct option conf_opts_long[] = {
	/* general input. */
	{"config-file", required_argument, 0, 'C'},
	{"dpdk-eal", required_argument, 0, 'D'},
	{"verbose", required_argument, 0, 'V'},
	{"cores", required_argument, 0, 'c'},

	/* required input. */
	{"regex-dev", required_argument, 0, 'd'},
	{"input-mode", required_argument, 0, 'm'},
	{"input-file", required_argument, 0, 'f'},
	{"rules", required_argument, 0, 'r'},
	{"raw-rules", required_argument, 0, 'R'},

	/* run specific. */
	{"run-time-secs", required_argument, 0, 's'},
	{"run-num-iterations", required_argument, 0, 'n'},
	{"run-packets", required_argument, 0, 'p'},
	{"run-bytes", required_argument, 0, 'b'},
	{"run-app-layer", no_argument, 0, 'A'},

	/* search specific. */
	{"buf-length", required_argument, 0, 'l'},
	{"buf-thres", required_argument, 0, 't'},
	{"buf-overlap", required_argument, 0, 'o'},
	{"buf-group", required_argument, 0, 'g'},
	{"sliding-window", required_argument, 0, 'w'},

	/* RXP specific. */
	{"latency-mode", no_argument, 0, '8'},

	/* HS specific. */
	/* using HS syntax. */
	{"hs-singlematch", no_argument, 0, 'H'},
	{"hs-leftmost", no_argument, 0, 'L'},

	/* Regex compilation. */
	{"force-compile", no_argument, 0, 'F'},
	{"comp-single-line", no_argument, 0, 'S'},
	{"comp-caseless", no_argument, 0, 'i'},
	{"comp-multi-line", no_argument, 0, 'u'},
	{"comp-free-space", no_argument, 0, 'x'},

	/* DPDK live specific. */
	{"dpdk-primary-port", required_argument, 0, '1'},
	{"dpdk-second-port", required_argument, 0, '2'},

	{"help", no_argument, 0, 'h'},
	{"version", no_argument, 0, 'v'},

	/* required at end */
	{NULL, 0, NULL, 0}};

static const char *conf_opts_short = "C:D:FV:c:d:m:f:r:R:s:n:p:b:Al:t:o:g:w:8HLSiux1:2:hv";

/* Parse given args into the run_conf. */
static int
conf_parse_args(rb_conf *run_conf, int argc, char **argv)
{
	char *prgname = argv[0];
	static int idx;
	uint32_t *dest;
	int ret = 0;
	int opt;

	/* Reset global variable to allow multiple argv parses. */
	optind = 1;

	while ((opt = getopt_long(argc, argv, conf_opts_short, conf_opts_long, &idx)) != EOF) {
		//printf("opt:%c\n",opt);	
		switch (opt) {
		/* config-file */
		case 'C':
			ret = conf_set_string(&conf_file, optarg);
			break;

		/* dpdk-eal */
		case 'D':
			ret = conf_parse_dpdk_params(run_conf, prgname, optarg);
			break;

		/* verbose */
		case 'V':
			dest = &run_conf->verbose;
			ret = conf_set_uint32_t(dest, opt, optarg);
			break;

		/* cores */
		case 'c':
			dest = &run_conf->cores;
			ret = conf_set_uint32_t(dest, opt, optarg);
			break;

		/* regex-dev */
		case 'd':
			if (run_conf->regex_dev_type != REGEX_DEV_UNKNOWN)
				break;
			if (strcmp(optarg, "regex_dpdk") == 0 || strcmp(optarg, "rxp") == 0)
				run_conf->regex_dev_type = REGEX_DEV_DPDK_REGEX;
			else if (strcmp(optarg, "hyperscan") == 0 || strcmp(optarg, "hs") == 0)
				run_conf->regex_dev_type = REGEX_DEV_HYPERSCAN;
			else if (strcmp(optarg, "doca_regex") == 0 || strcmp(optarg, "doca") == 0)
				run_conf->regex_dev_type = REGEX_DEV_DOCA_REGEX;
			else if (strcmp(optarg, "comp_dpdk") == 0)
				run_conf->regex_dev_type = COMP_DEV_DPDK_COMP;
			else {
				RXPB_LOG_ERR("Invalid regex device.");
				rxpbench_usage(prgname);
				return -EINVAL;
			}
			break;

		/* input-type */
		case 'm':
			if (run_conf->input_mode != INPUT_UNKNOWN)
				break;
			if (strcmp(optarg, "dpdk_port") == 0)
				run_conf->input_mode = INPUT_LIVE;
			else if (strcmp(optarg, "pcap_file") == 0)
				run_conf->input_mode = INPUT_PCAP_FILE;
			else if (strcmp(optarg, "text_file") == 0)
				run_conf->input_mode = INPUT_TEXT_FILE;
			else if (strcmp(optarg, "job_format") == 0)
				run_conf->input_mode = INPUT_JOB_FORMAT;
			else if (strcmp(optarg, "remote_mmap") == 0)
				run_conf->input_mode = INPUT_REMOTE_MMAP;
			else {
				RXPB_LOG_ERR("Invalid input type.");
				rxpbench_usage(prgname);
				return -EINVAL;
			}
			break;

		/* input-file */
		case 'f':
			ret = conf_set_string(&run_conf->input_file, optarg);
			break;

		/* rules */
		case 'r':
			if (raw_rule_cmd_line)
				break;
			ret = conf_set_string(&run_conf->compiled_rules_file, optarg);
			break;

		/* raw-rules */
		case 'R':
			ret = conf_set_string(&run_conf->raw_rules_file, optarg);
			break;

		/* run-time-secs */
		case 's':
			dest = &run_conf->input_duration;
			ret = conf_set_uint32_t(dest, opt, optarg);
			break;
		/* run-num-iterations */
		case 'n':
			dest = &run_conf->input_iterations;
			ret = conf_set_uint32_t(dest, opt, optarg);
			break;

		/* run-packets */
		case 'p':
			dest = &run_conf->input_packets;
			ret = conf_set_uint32_t(dest, opt, optarg);
			break;
		/* run-bytes */
		case 'b':
			dest = &run_conf->input_bytes;
			ret = conf_set_uint32_t(dest, opt, optarg);
			break;

		/* run-app-layer */
		case 'A':
			run_conf->input_app_mode = true;
			break;

		/* buf-length */
		case 'l':
			dest = &run_conf->input_buf_len;
			ret = conf_set_uint32_t(dest, opt, optarg);
			break;

		/* buf-thres */
		case 't':
			dest = &run_conf->input_len_threshold;
			ret = conf_set_uint32_t(dest, opt, optarg);
			break;

		/* buf-overlap */
		case 'o':
			dest = &run_conf->input_overlap;
			ret = conf_set_uint32_t(dest, opt, optarg);
			break;

		/* buf-group */
		case 'g':
			dest = &run_conf->input_batches;
			ret = conf_set_uint32_t(dest, opt, optarg);
			break;

		/* sliding-window */
		case 'w':
			dest = &run_conf->sliding_window;
			ret = conf_set_uint32_t(dest, opt, optarg);
			break;

		/* latency-mode */
		case '8':
			run_conf->latency_mode = true;
			break;

		/* hs-singlematch */
		case 'H':
			run_conf->hs_singlematch = true;
			break;

		/* hs-leftmost */
		case 'L':
			run_conf->hs_leftmost = true;
			break;

		/* force-compile */
		case 'F':
			run_conf->force_compile = true;
			break;

		/* comp-single */
		case 'S':
			run_conf->single_line = true;
			break;

		/* comp-caseless */
		case 'i':
			run_conf->caseless = true;
			break;

		/* comp-multi-line */
		case 'u':
			run_conf->multi_line = true;
			break;

		/* comp-free-space */
		case 'x':
			run_conf->free_space = true;
			break;

		/* dpdk-primary-port */
		case '1':
			ret = conf_set_string(&run_conf->port1, optarg);
			break;

		/* dpdk-second-port */
		case '2':
			ret = conf_set_string(&run_conf->port2, optarg);
			break;

		/* help */
		case 'h':
			rxpbench_usage(prgname);
			rte_exit(EXIT_SUCCESS, NULL);

		/* version */
		case 'v':
			rxpbench_version();
			rte_exit(EXIT_SUCCESS, NULL);

		default:
			rxpbench_usage(prgname);
			return -ENOTSUP;
		}

		if (ret)
			return ret;
	}

	return 0;
}

static int
conf_parse_file(rb_conf *run_conf, char *prgname)
{
	char *conf_argv[CONFIG_FILE_MAX_ARGS];
	char conf[CONFIG_FILE_LINE_LEN];
	char *opt_formatted;
	FILE *config_file;
	char *fields[2];
	int conf_argc;
	char *optarg;
	int ret = 0;
	char *opt;
	int i;

	conf_argv[0] = prgname;
	conf_argc = 1;

	config_file = fopen(conf_file, "r");
	if (!config_file) {
		//RXPB_LOG_WARN("No config file at  %s.", conf_file);
		return 0;
	}

	/* Convert config file to opt/optargs. */
	while (fgets(conf, CONFIG_FILE_LINE_LEN, config_file) != NULL) {
		fields[0] = NULL;
		fields[1] = NULL;

		ret = rte_strsplit(conf, strlen(conf), fields, 2, ':');
		if (ret < 0) {
			RXPB_LOG_ERR("Failed reading config file line: %s.", conf);
			goto out;
		}

		opt = util_trim_whitespace(fields[0]);
		const size_t opt_len = strlen(opt);

		if (!opt_len || opt[0] == '#')
			continue;

		/* ensure there are 2 spaces to write to. */
		if (conf_argc >= CONFIG_FILE_MAX_ARGS - 2) {
			RXPB_LOG_WARN("Max config file fields reached.");
			goto process_args;
		}

		/* Store config entry opt as short or long form. */
		if (opt_len == 1) {
			opt_formatted = malloc(opt_len + 2);
			if (!opt_formatted) {
				RXPB_LOG_ERR("Memory failure copying config file short opt.");
				ret = -ENOMEM;
				goto out;
			}
			opt_formatted[0] = '-';
			strncpy(&opt_formatted[1], opt, opt_len);
			opt_formatted[opt_len + 1] = '\0';
		} else {
			opt_formatted = malloc(opt_len + 3);
			if (!opt_formatted) {
				RXPB_LOG_ERR("Memory failure copying config file long opt.");
				ret = -ENOMEM;
				goto out;
			}
			opt_formatted[0] = '-';
			opt_formatted[1] = '-';
			strncpy(&opt_formatted[2], opt, opt_len);
			opt_formatted[opt_len + 2] = '\0';
		}
		conf_argv[conf_argc] = opt_formatted;
		conf_argc++;

		optarg = NULL;
		if (fields[1]) {
			optarg = util_trim_whitespace(fields[1]);
			conf_argv[conf_argc] = strdup(optarg);
			conf_argc++;
		}
	}

process_args:
	/* Process config file inputs as if command line params. */
	ret = conf_parse_args(run_conf, conf_argc, conf_argv);
out:
	fclose(config_file);

	/* Free any generated ops/optargs. */
	for (i = 1; i < conf_argc; i++)
		free(conf_argv[i]);

	return ret;
}

/* Trigger warning that param is not applicable to a given regex device. */
static void
conf_validation_dev_warning(rb_conf *run_conf, const char *dev, const char *param)
{
	RXPB_LOG_WARN_REC(run_conf, "%s not applicable to %s regex device.", param, dev);
}

/* Trigger warning that param is not applicable in given mode. */
static void
conf_validation_mode_warning(rb_conf *run_conf, const char *mode, const char *param)
{
	RXPB_LOG_WARN_REC(run_conf, "%s not applicable to %s mode.", param, mode);
}

/* Check user inputs for invalid or conflicting settings. */
static int
conf_validate(rb_conf *run_conf)
{
	if (run_conf->cores >= RTE_MAX_LCORE) {
		RXPB_LOG_ERR("Input cores out of range.");
		return -EINVAL;
	}

	if (run_conf->verbose > 3) {
		RXPB_LOG_ERR("Verbose value out of range.");
		return -EINVAL;
	}

	if (run_conf->input_batches > TX_RING_SIZE) {
		RXPB_LOG_ERR("Buf-group too large (max: %u).", TX_RING_SIZE);
		return -EINVAL;
	}

	if (run_conf->input_mode == INPUT_PCAP_FILE || run_conf->input_mode == INPUT_TEXT_FILE ||
	    run_conf->input_mode == INPUT_JOB_FORMAT || run_conf->input_mode == INPUT_REMOTE_MMAP) {
		uint32_t conf_buf_len;

		conf_buf_len = run_conf->input_buf_len ? run_conf->input_buf_len : DEFAULT_BUF_LEN;
		if (run_conf->input_overlap >= conf_buf_len) {
			RXPB_LOG_ERR("buf-overlap >= buf-length.");
			return -EINVAL;
		}
		if (!run_conf->input_file) {
			RXPB_LOG_ERR("Input file not specified.");
			return -EINVAL;
		}
		if (run_conf->input_duration && run_conf->input_iterations)
			RXPB_LOG_WARN_REC(run_conf, "conflicting iteration and time limits.");
	}

	if (run_conf->input_mode == INPUT_TEXT_FILE) {
		if (run_conf->input_packets)
			conf_validation_mode_warning(run_conf, "text_file", "run-packets");
		if (run_conf->input_app_mode)
			conf_validation_mode_warning(run_conf, "text_file", "run-app-layer");
		if (run_conf->input_len_threshold)
			conf_validation_mode_warning(run_conf, "text_file", "buf-thres");
	} else if (run_conf->input_mode == INPUT_PCAP_FILE) {
		if (run_conf->input_app_mode && run_conf->input_buf_len)
			conf_validation_mode_warning(run_conf, "pcap_file", "buf-length");
		if (run_conf->input_app_mode && run_conf->input_overlap)
			conf_validation_mode_warning(run_conf, "pcap_file", "buf-overlap");
	} else if (run_conf->input_mode == INPUT_JOB_FORMAT) {
		if (run_conf->input_buf_len)
			conf_validation_mode_warning(run_conf, "job_format", "buf-length");
		if (run_conf->input_overlap)
			conf_validation_mode_warning(run_conf, "job_format", "buf-overlap");
		if (run_conf->input_app_mode)
			conf_validation_mode_warning(run_conf, "job_format", "run-app-layer");
		if (run_conf->input_len_threshold)
			conf_validation_mode_warning(run_conf, "job_format", "buf-thres");
	} else if (run_conf->input_mode == INPUT_REMOTE_MMAP) {
		if (run_conf->input_packets)
			conf_validation_mode_warning(run_conf, "text_file", "run-packets");
		if (run_conf->input_app_mode)
			conf_validation_mode_warning(run_conf, "text_file", "run-app-layer");
		if (run_conf->input_len_threshold)
			conf_validation_mode_warning(run_conf, "text_file", "buf-thres");
		if (run_conf->regex_dev_type != REGEX_DEV_DOCA_REGEX) {
			RXPB_LOG_ERR("Remote mmap mode is only supported with DOCA regex");
			return -EINVAL;
		}
	} else if (run_conf->input_mode == INPUT_LIVE) {
		if (!run_conf->port1) {
			RXPB_LOG_ERR("No specified primary port.");
			return -EINVAL;
		}
		if (run_conf->input_batches && run_conf->input_batches < 4) {
			RXPB_LOG_ERR("A minimum batch size of 4 is required in live mode - this sets rx queue size.");
			return -EINVAL;
		}
		if (run_conf->input_iterations)
			conf_validation_mode_warning(run_conf, "dpdk_port", "run-iterations");
		if (run_conf->input_buf_len)
			conf_validation_mode_warning(run_conf, "dpdk_port", "buf-length");
		if (run_conf->input_overlap)
			conf_validation_mode_warning(run_conf, "dpdk_port", "buf-overlap");
	}

	if (run_conf->regex_dev_type == REGEX_DEV_HYPERSCAN) {
		if (run_conf->input_mode == INPUT_JOB_FORMAT) {
			RXPB_LOG_ERR("Hyperscan does not currently support job format input.");
			return -ENOTSUP;
		}
		if (run_conf->free_space)
			conf_validation_dev_warning(run_conf, "hyperscan", "comp-free-space");
		if (run_conf->hs_singlematch && run_conf->hs_leftmost) {
			RXPB_LOG_ERR("Hyperscan leftmost and single incompatible.");
			return -EINVAL;
		}
		if (run_conf->latency_mode)
			conf_validation_dev_warning(run_conf, "hyperscan", "latency-mode");

	} else if (run_conf->regex_dev_type == REGEX_DEV_DPDK_REGEX ||
		   run_conf->regex_dev_type == REGEX_DEV_DOCA_REGEX) {
		if (run_conf->hs_singlematch)
			conf_validation_dev_warning(run_conf, "NON hyperscan", "hs_singlematch");
		if (run_conf->hs_leftmost)
			conf_validation_dev_warning(run_conf, "NON hyperscan", "hs_leftmost");
	}

	if (run_conf->regex_dev_type != REGEX_DEV_DOCA_REGEX && run_conf->sliding_window)
		conf_validation_dev_warning(run_conf, "NON DOCA", "sliding-window");

	if (run_conf->sliding_window >= MAX_REGEX_BUF_SIZE) {
		RXPB_LOG_ERR("sliding-window %u exceeds max buf size of %u.", run_conf->sliding_window,
			     MAX_REGEX_BUF_SIZE);
		return -EINVAL;
	}

	/* Doca regex impliments sliding window so can have input buffers > MAX job size. */
	if (run_conf->regex_dev_type != REGEX_DEV_DOCA_REGEX && run_conf->input_buf_len > MAX_REGEX_BUF_SIZE) {
		RXPB_LOG_ERR("buf-length %u exceeds max of %u.", run_conf->input_buf_len, MAX_REGEX_BUF_SIZE);
		return -EINVAL;
	}

	return 0;
}

/* Set defaults in required config entries where user has not supplied input. */
static void
conf_set_defaults(rb_conf *run_conf)
{
	if (!run_conf->input_iterations) {
		/* If a time is set, give it priority by maxing iterations. */
		if (run_conf->input_duration)
			run_conf->input_iterations = UINT_MAX;
		else
			run_conf->input_iterations = DEFAULT_ITERATIONS;
	}

	if (!run_conf->input_buf_len)
		run_conf->input_buf_len = DEFAULT_BUF_LEN;

	if (!run_conf->input_batches)
		run_conf->input_batches = DEFAULT_BATCH_SIZE;

	if (!run_conf->cores)
		run_conf->cores = DEFAULT_CORES;

	if (!run_conf->sliding_window)
		run_conf->sliding_window = DEFAULT_SLIDING_WINDOW;
}

int
conf_setup(rb_conf *run_conf, int argc, char **argv)
{	
	char *default_conf;
	int ret = 0;

	//printf("Configuring run_conf...\n");
	conf_init(run_conf);

	raw_rule_cmd_line = false;
	/* Parse command line params as priority inputs. */
	//printf("Argument parsing...\n");
	ret = conf_parse_args(run_conf, argc, argv);
	//printf("Argument parsed...\n");
	if (ret){
		printf("argument parse error!\n");
		return ret;
	}


	/* Give cmd line raw file priority over compiled file in conf file. */
	if (run_conf->raw_rules_file)
		raw_rule_cmd_line = true;

	/* Set conf file to default if it is not passed as a param. */
	default_conf = strdup("rxpbench.conf");
	if (!default_conf) {
		RXPB_LOG_ERR("Memory failure copying conf file location.");
		return -ENOMEM;
	}
	ret = conf_set_string(&conf_file, default_conf);
	free(default_conf);
	if (ret)
		return ret;

	/* Parse config file - will not overwrite fields set by command line. */
	ret = conf_parse_file(run_conf, argv[0]);
	if (ret)
		return ret;

	/* Validate user selections. */
	ret = conf_validate(run_conf);
	if (ret)
		return ret;

	/* Set required default values for fields not yet configured. */
	conf_set_defaults(run_conf);

	return ret;
}

void
conf_clean(rb_conf *run_conf)
{
	uint32_t i;

	for (i = 0; i < run_conf->no_conf_warnings; i++)
		free(run_conf->conf_warning[i]);
	free(run_conf->regex_pcie);
	free(run_conf->input_file);
	free(run_conf->compiled_rules_file);
	free(run_conf->raw_rules_file);
	free(run_conf->port1);
	free(run_conf->port2);
	free(conf_file);
}
