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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rte_malloc.h>
#include <rte_timer.h>

#include "rxpb_log.h"
#include "stats.h"

#define GIGA			1000000000.0
#define MEGA			1000000.0
#define KILO			1000.0

#define STATS_BANNER_LEN	80
#define STATS_BORDER		"+------------------------------------------------------------------------------+\n"

#define STATS_UPDATE_BANNER_LEN 38
#define STATS_UPDATE_BORDER	"+------------------------------------+"

static uint64_t split_bytes;
static uint64_t split_bufs;
static double split_duration;
static double max_split_perf;
static double max_split_rate;

/* Print banner of total_length characters with str center aligned. */
static inline void
stats_print_banner(const char *str, int total_length)
{
	int pad_left, pad_right;

	/* Remove one for the | characters at start and end. */
	pad_left = (total_length - strlen(str)) / 2 - 1;
	pad_right = (total_length - strlen(str)) % 2 ? pad_left + 1 : pad_left;

	fprintf(stdout, STATS_BORDER "|%*s%s%*s|\n" STATS_BORDER, pad_left, "", str, pad_right, "");
}

/* Print single column update banner. */
static inline void
stats_print_update_banner(const char *str, int total_length)
{
	int pad_left, pad_right;

	/* Remove one for the | characters at start and end. */
	pad_left = (total_length - strlen(str)) / 2 - 1;
	pad_right = (total_length - strlen(str)) % 2 ? pad_left + 1 : pad_left;

	fprintf(stdout, STATS_UPDATE_BORDER "\n|%*s%s%*s|\n" STATS_UPDATE_BORDER "\n", pad_left, "", str, pad_right,
		"");
}

/* Print update banner containing 2 columns. */
static inline void
stats_print_update_banner2(const char *str, const char *str2, int total_length)
{
	int pad_left, pad_right, pad_left2, pad_right2;

	/* remove one for the | characters at start and end. */
	pad_left = (total_length - strlen(str)) / 2 - 1;
	pad_right = (total_length - strlen(str)) % 2 ? pad_left + 1 : pad_left;
	pad_left2 = (total_length - strlen(str2)) / 2 - 1;
	pad_right2 = (total_length - strlen(str2)) % 2 ? pad_left2 + 1 : pad_left2;

	fprintf(stdout,
		STATS_UPDATE_BORDER "    " STATS_UPDATE_BORDER "\n|%*s%s%*s|    |%*s%s%*s|\n" STATS_UPDATE_BORDER
				    "    " STATS_UPDATE_BORDER "\n",
		pad_left, "", str, pad_right, "", pad_left, "", str2, pad_right2, "");
}

/* Store common stats per queue. */
int
stats_init(rb_conf *run_conf)
{
	const int nq = run_conf->cores;
	rb_stats_t *stats;
	int i, j;

	run_conf->input_pkt_stats = rte_zmalloc(NULL, sizeof(pkt_stats_t), 0);
	if (!run_conf->input_pkt_stats)
		goto err_input_pkt_stats;

	stats = rte_malloc(NULL, sizeof(*stats), 0);
	if (!stats)
		goto err_stats;

	stats->rm_stats = rte_zmalloc(NULL, sizeof(run_mode_stats_t) * nq, 128);
	if (!stats->rm_stats)
		goto err_rm_stats;

	stats->regex_stats = rte_zmalloc(NULL, sizeof(regex_stats_t) * nq, 64);
	if (!stats->regex_stats)
		goto err_regex_stats;

	for (i = 0; i < nq; i++) {
		regex_stats_t *reg = &stats->regex_stats[i];

		if (run_conf->regex_dev_type == REGEX_DEV_DPDK_REGEX ||
		    run_conf->regex_dev_type == REGEX_DEV_DOCA_REGEX) {
			reg->custom = rte_zmalloc(NULL, sizeof(rxp_stats_t), 0);
			if (!reg->custom)
				goto err_custom;
		} else if (run_conf->regex_dev_type == REGEX_DEV_HYPERSCAN) {
			reg->custom = rte_zmalloc(NULL, sizeof(hs_stats_t), 0);
			if (!reg->custom)
				goto err_custom;
		}
		else if (run_conf->regex_dev_type == COMP_DEV_DPDK_COMP) {
			reg->custom = rte_zmalloc(NULL, sizeof(comp_dpdk_stats_t), 0);
			if (!reg->custom)
				goto err_custom;	
		}
	}

	run_conf->stats = stats;

	return 0;

err_custom:
	for (j = 0; j < i; j++)
		rte_free(stats->regex_stats[j].custom);
	rte_free(stats->regex_stats);
err_regex_stats:
	rte_free(stats->rm_stats);
err_rm_stats:
	rte_free(stats);
err_stats:
	rte_free(run_conf->input_pkt_stats);
err_input_pkt_stats:
	RXPB_LOG_ERR("Memory failure when allocating stats.");

	return -ENOMEM;
}

static const char *
stats_regex_dev_to_str(enum rxpbench_acc_dev dev)
{
	if (dev == REGEX_DEV_DPDK_REGEX)
		return "DPDK Regex";
	if (dev == REGEX_DEV_HYPERSCAN)
		return "Hyperscan";
	if (dev == REGEX_DEV_DOCA_REGEX)
		return "Doca Regex";

	return "-";
}

static const char *
stats_input_type_to_str(enum rxpbench_input_type input)
{
	if (input == INPUT_PCAP_FILE)
		return "PCAP File";
	if (input == INPUT_TEXT_FILE)
		return "Text File";
	if (input == INPUT_LIVE)
		return "DPDK Live";
	if (input == INPUT_REMOTE_MMAP)
		return "Remote mmap";

	return "-";
}

static void
stats_print_config(rb_conf *run_conf)
{
	const char *dpdk_app_mode;
	const char *rxp_prefixes;
	const char *rxp_latency;
	pkt_stats_t *pkt_stats;
	const char *input_file;
	const char *rules_file;
	const char *hs_single;
	const char *app_mode;
	const char *hs_mode;
	const char *hs_left;
	uint32_t iterations;
	uint32_t buf_length;
	uint32_t rxp_match;
	uint32_t slid_win;
	const char *regex;
	const char *input;
	const char *port1;
	const char *port2;
	uint32_t i;

	pkt_stats = run_conf->input_pkt_stats;
	iterations = run_conf->input_iterations;
	buf_length = run_conf->input_buf_len;
	slid_win = run_conf->sliding_window;

	if (run_conf->input_mode == INPUT_LIVE) {
		buf_length = 0;
		iterations = 0;
	}

	if (run_conf->input_mode == INPUT_PCAP_FILE && run_conf->input_app_mode) {
		app_mode = "True";
		buf_length = 0;
	} else {
		app_mode = "False";
	}

	if (run_conf->regex_dev_type != REGEX_DEV_DOCA_REGEX)
		slid_win = 0;

	regex = stats_regex_dev_to_str(run_conf->regex_dev_type);
	input = stats_input_type_to_str(run_conf->input_mode);

	input_file = run_conf->input_mode != INPUT_LIVE ? run_conf->input_file : "-";
	port1 = run_conf->input_mode == INPUT_LIVE ? run_conf->port1 : "-";
	port2 = run_conf->input_mode == INPUT_LIVE && run_conf->port2 ? run_conf->port2 : "-";
	dpdk_app_mode = run_conf->input_mode == INPUT_LIVE && run_conf->input_app_mode ? "True" : "False";
	rules_file = run_conf->raw_rules_file ? run_conf->raw_rules_file : run_conf->compiled_rules_file;

	/* Trim the file names if more than 52 characters. */
	if (strlen(input_file) > 52) {
		input_file += (strlen(input_file) - 52);
	}

	if (strlen(rules_file) > 52) {
		rules_file += (strlen(rules_file) - 52);
	}

	if (run_conf->regex_dev_type == REGEX_DEV_HYPERSCAN) {
		rxp_prefixes = "-";
		rxp_latency = "-";
		rxp_match = 0;
		hs_mode = "HS_MODE_BLOCK";
		hs_single = run_conf->hs_singlematch ? "True" : "False";
		hs_left = run_conf->hs_leftmost ? "True" : "False";
	} else {
		rxp_prefixes = "N/A";
		rxp_latency = "N/A";
		rxp_match = run_conf->rxp_max_matches;
		hs_mode = "-";
		hs_single = "-";
		hs_left = "-";
	}

	stats_print_banner("CONFIGURATION", STATS_BANNER_LEN);

	fprintf(stdout,
		"|%*s|\n"
		"| - RUN SETTINGS -       %*s|\n"
		"|%*s|\n"
		"| INPUT MODE:         %-56s |\n"
		"| REGEX DEV:          %-56s |\n"
		"| INPUT FILE:         %-56s |\n"
		"| RULES INPUT:        %-56s |\n"
		"|%*s|\n"
		"|%*s|\n"
		"| - DPDK LIVE CONFIG -   %*s|\n"
		"|%*s|\n"
		"| DPDK PRIMARY PORT:  %-16s  "
		"  DPDK SECOND  PORT:  %-16s |\n"
		"| APP LAYER MODE:     %-56s |\n"
		"|%*s|\n"
		"|%*s|\n"
		"| - RUN/SEARCH PARAMS -  %*s|\n"
		"|%*s|\n"
		"| INPUT DURATION:     %-16u  "
		"  BUFFER LENGTH:      %-16u |\n"
		"| INPUT PACKETS:      %-16u  "
		"  BUFFER THRESHOLD:   %-16u |\n"
		"| INPUT BYTES:        %-16u  "
		"  BUFFER OVERLAP:     %-16u |\n"
		"| INPUT ITERATIONS:   %-16u  "
		"  GROUP/BATCH SIZE:   %-16u |\n"
		"| SLIDING WINDOW:     %-56u |\n"
		"|%*s|\n"
		"|%*s|\n"
		"| - PRELOADED DATA INFO -%*s|\n"
		"|%*s|\n"
		"| DATA LENGTH:        %-56lu |\n"
		"| APP LAYER MODE:     %-56s |\n"
		"| VALID PACKETS:      %-16lu  "
		"  VLAN/QNQ:           %-16lu |\n"
		"| INVALID LENGTH:     %-16lu  "
		"  IPV4:               %-16lu |\n"
		"| UNSUPPORTED PROT:   %-16lu  "
		"  IPV6:               %-16lu |\n"
		"| NO PAYLOAD:         %-16lu  "
		"  TCP:                %-16lu |\n"
		"| THRESHOLD DROP:     %-16lu  "
		"  UDP:                %-16lu |\n"
		"|%*s|\n"
		"|%*s|\n"
		"| - REGEX DEVICE CONFIG -%*s|\n"
		"|%*s|\n"
		"| RXP MAX MATCHES:    %-16u  "
		"  HS MODE:            %-16s |\n"
		"| RXP MAX PREFIXES:   %-16s  "
		"  HS SINGLE MATCH:    %-16s |\n"
		"| RXP MAX LATENCY:    %-16s  "
		"  HS LEFTMOST MATCH:  %-16s |\n"
		"|%*s|\n"
		"|%*s|\n"
		"| - REGEX COMPILATION -  %*s|\n"
		"|%*s|\n"
		"| SINGLE LINE:        %-16s  "
		"  CASE INSENSITIVE:   %-16s |\n"
		"| MULTI-LINE:         %-16s  "
		"  FREE SPACE:         %-16s |\n"
		"| FORCE COMPILE:      %-56s |\n"
		"|%*s|\n"
		"|%*s|\n"
		"| - PERFORMANCE CONFIG - %*s|\n"
		"|%*s|\n"
		"| NUMBER OF CORES:    %-56u |\n"
		"|%*s|\n",
		78, "", 54, "", 78, "", input, regex, input_file, rules_file, 78, "", 78, "", 54, "", 78, "", port1,
		port2, dpdk_app_mode, 78, "", 78, "", 54, "", 78, "", run_conf->input_duration, buf_length,
		run_conf->input_packets, run_conf->input_len_threshold, run_conf->input_bytes, run_conf->input_overlap,
		iterations, run_conf->input_batches, slid_win, 78, "", 78, "", 54, "", 78, "", run_conf->input_data_len,
		app_mode, pkt_stats->valid_pkts, pkt_stats->vlan + pkt_stats->qnq, pkt_stats->invalid_pkt,
		pkt_stats->ipv4, pkt_stats->unsupported_pkts, pkt_stats->ipv6, pkt_stats->no_payload, pkt_stats->tcp,
		pkt_stats->thres_drop, pkt_stats->udp, 78, "", 78, "", 54, "", 78, "", rxp_match, hs_mode, rxp_prefixes,
		hs_single, rxp_latency, hs_left, 78, "", 78, "", 54, "", 78, "",
		run_conf->single_line ? "True" : "False", run_conf->caseless ? "True" : "False",
		run_conf->multi_line ? "True" : "False", run_conf->free_space ? "True" : "False",
		run_conf->force_compile ? "True" : "False", 78, "", 78, "", 54, "", 78, "", run_conf->cores, 78, "");

	/* Report warnings if any exist. */
	if (run_conf->no_conf_warnings) {
		fprintf(stdout,
			"|%*s|\n"
			"| - CONFIG WARNINGS -    %*s|\n"
			"|%*s|\n",
			78, "", 54, "", 78, "");
		for (i = 0; i < run_conf->no_conf_warnings; i++)
			fprintf(stdout, "| * %-74s |\n", run_conf->conf_warning[i]);
		fprintf(stdout, "|%*s|\n", 78, "");
	}

	fprintf(stdout, STATS_BORDER "\n");
}

static void
stats_print_common_stats(rb_stats_t *stats, int num_queues, double time)
{
	regex_stats_t *regex_stats = stats->regex_stats;
	run_mode_stats_t *rm_stats = stats->rm_stats;
	run_mode_stats_t total_rm;
	regex_stats_t total_regex;
	pkt_stats_t *pkt_stats;
	pkt_stats_t *total_pkt;
	double ave_length;
	double byte_match;
	double reg_perf;
	double reg_rate;
	double rx_perf;
	double rx_rate;
	int i;

	memset(&total_rm, 0, sizeof(total_rm));
	memset(&total_regex, 0, sizeof(total_regex));
	total_pkt = &total_rm.pkt_stats;

	for (i = 0; i < num_queues; i++) {
		pkt_stats = &rm_stats[i].pkt_stats;

		total_rm.rx_buf_cnt += rm_stats[i].rx_buf_cnt;
		total_rm.rx_buf_bytes += rm_stats[i].rx_buf_bytes;
		total_rm.tx_buf_cnt += rm_stats[i].tx_buf_cnt;
		total_rm.tx_buf_bytes += rm_stats[i].tx_buf_bytes;
		total_rm.tx_batch_cnt += rm_stats[i].tx_batch_cnt;

		total_regex.rx_valid += regex_stats[i].rx_valid;
		total_regex.rx_buf_match_cnt += regex_stats[i].rx_buf_match_cnt;
		total_regex.rx_total_match += regex_stats[i].rx_total_match;

		total_pkt->valid_pkts += pkt_stats->valid_pkts;
		total_pkt->unsupported_pkts += pkt_stats->unsupported_pkts;
		total_pkt->no_payload += pkt_stats->no_payload;
		total_pkt->invalid_pkt += pkt_stats->invalid_pkt;
		total_pkt->thres_drop += pkt_stats->thres_drop;
		total_pkt->vlan += pkt_stats->vlan;
		total_pkt->qnq += pkt_stats->qnq;
		total_pkt->ipv4 += pkt_stats->ipv4;
		total_pkt->ipv6 += pkt_stats->ipv6;
		total_pkt->tcp += pkt_stats->tcp;
		total_pkt->udp += pkt_stats->udp;
	}

	ave_length = total_rm.tx_buf_cnt ? (double)total_rm.tx_buf_bytes / total_rm.tx_buf_cnt : 0;
	byte_match = total_regex.rx_total_match ? (double)total_rm.tx_buf_bytes / total_regex.rx_total_match : 0;
	reg_perf = ((total_rm.tx_buf_bytes * 8) / time) / GIGA;
	rx_perf = ((total_rm.rx_buf_bytes * 8) / time) / GIGA;
	reg_rate = (total_rm.tx_buf_cnt / time) / MEGA;
	rx_rate = (total_rm.rx_buf_cnt / time) / MEGA;

	stats_print_banner("RUN OVERVIEW", STATS_BANNER_LEN);
	fprintf(stdout,
		"|%*s|\n"
		"| - RAW DATA PROCESSING -%*s|\n"
		"|%*s|\n"
		"| TOTAL PKTS:     %-20lu  "
		"  QNQ:            %-20lu |\n"
		"| TOTAL BYTES     %-20lu  "
		"  VLAN:           %-20lu |\n"
		"| VALID PKTS:     %-20lu  "
		"  IPV4:           %-20lu |\n"
		"| UNSUPPORTED:    %-20lu  "
		"  IPV6:           %-20lu |\n"
		"| NO PAYLOAD:     %-20lu  "
		"  TCP:            %-20lu |\n"
		"| UNDER THRES:    %-20lu  "
		"  UDP:            %-20lu |\n"
		"|%*s|\n",
		78, "", 54, "", 78, "", total_rm.rx_buf_cnt, total_pkt->qnq, total_rm.rx_buf_bytes, total_pkt->vlan,
		total_pkt->valid_pkts, total_pkt->ipv4, total_pkt->unsupported_pkts, total_pkt->ipv6,
		total_pkt->no_payload, total_pkt->tcp, total_pkt->thres_drop, total_pkt->udp, 78, "");

	/* Only report throughput stats if duration is long enough. */
	if (time > 0.1)
		fprintf(stdout,
			"| PACKET PROCESSING RATE (Mpps):    %-42.4f |\n"
			"| PACKET PROCESSING PERF (Gb/s):    %-42.4f |\n",
			rx_rate, rx_perf);
	else
		fprintf(stdout, "| PACKET PROCESSING RATE (Mpps):    N/A (run time must be > 0.1 secs)          |\n"
				"| PACKET PROCESSING PERF (Gb/s):    N/A (run time must be > 0.1 secs)          |\n");

	fprintf(stdout,
		"|%*s|\n"
		"|%*s|\n"
		"| - REGEX PROCESSING -   %*s|\n"
		"|%*s|\n"
		"| TOTAL REGEX BUFFERS:              %-42lu |\n"
		"| TOTAL REGEX BYTES:                %-42lu |\n"
		"| TOTAL REGEX BATCHES:              %-42lu |\n"
		"| VALID REGEX RESPONSES:            %-42lu |\n"
		"| REGEX RESPONSES WITH MATCHES:     %-42lu |\n"
		"| TOTAL REGEX MATCHES:              %-42lu |\n"
		"|%*s|\n"
		"| AVERAGE REGEX BUFFER LENGTH:      %-42.2f |\n"
		"| MATCH TO BYTE RATIO:              %-42.2f |\n"
		"|%*s|\n",
		78, "", 78, "", 54, "", 78, "", total_rm.tx_buf_cnt, total_rm.tx_buf_bytes, total_rm.tx_batch_cnt,
		total_regex.rx_valid, total_regex.rx_buf_match_cnt, total_regex.rx_total_match, 78, "", ave_length,
		byte_match, 78, "");

	/* Only report throughput stats if duration is long enough. */
	if (time > 0.1)
		fprintf(stdout,
			"| REGEX BUFFER RATE (Mbps):         %-42.4f |\n"
			"| REGEX PERFORMANCE (Gb/s):         %-42.4f |\n"
			"|%*s|\n"
			"| MAX REGEX BUFFER RATE (Mbps):     %-42.4f |\n"
			"| MAX REGEX PERFORMANCE (Gb/s):     %-42.4f |\n",
			reg_rate, reg_perf, 78, "", max_split_rate, max_split_perf);
	else
		fprintf(stdout,
			"| REGEX BUFFER RATE (Mbps):         "
			"N/A (run time must be > 0.1 secs)          |\n"
			"| REGEX PERFORMANCE (Gb/s):         "
			"N/A (run time must be > 0.1 secs)          |\n"
			"|%*s|\n"
			"| MAX REGEX BUFFER RATE (Mbps):     "
			"N/A (run time must be > 0.1 secs)          |\n"
			"| MAX REGEX PERFORMANCE (Gb/s):     "
			"N/A (run time must be > 0.1 secs)          |\n",
			78, "");

	fprintf(stdout,
		"|%*s|\n"
		"| TOTAL DURATION (secs):            %-42.4f |\n"
		"|%*s|\n" STATS_BORDER "\n",
		78, "", time, 78, "");
}

static inline void
stats_print_update_single_simple(run_mode_stats_t *rm1, regex_stats_t *reg1, run_mode_stats_t *rm2, regex_stats_t *reg2,
			  bool total, double duration)
{
	double perf, split_perf, split_rate;

	if (total) {
		if (duration) {
			perf = ((rm1->tx_buf_bytes * 8) / duration) / GIGA;
			split_perf = (((rm1->tx_buf_bytes - split_bytes) * 8) / (duration - split_duration)) / MEGA;
			split_rate = ((rm1->tx_buf_cnt - split_bufs) / (duration - split_duration)) / KILO;

			if (split_perf > max_split_perf)
				max_split_perf = split_perf;
			if (split_rate > max_split_rate)
				max_split_rate = split_rate;
		} else {
			perf = 0.0;
			split_perf = 0.0;
			max_split_perf = 0.0;
			max_split_rate = 0.0;
		}

		split_duration = duration;
		split_bytes = rm1->tx_buf_bytes;
		split_bufs = rm1->tx_buf_cnt;
	} 

	if (!rm2) {
		if (total) {
			fprintf(stdout,"%.4f  Mbps\t %.4f  Kpps\n",split_perf,split_rate);
		}
	} 
}

static inline void
stats_print_update_single(run_mode_stats_t *rm1, regex_stats_t *reg1, run_mode_stats_t *rm2, regex_stats_t *reg2,
			  bool total, double duration)
{
	double perf, split_perf, split_rate;

	if (total) {
		if (duration) {
			perf = ((rm1->tx_buf_bytes * 8) / duration) / GIGA;
			split_perf = (((rm1->tx_buf_bytes - split_bytes) * 8) / (duration - split_duration)) / GIGA;
			split_rate = ((rm1->tx_buf_cnt - split_bufs) / (duration - split_duration)) / MEGA;

			if (split_perf > max_split_perf)
				max_split_perf = split_perf;
			if (split_rate > max_split_rate)
				max_split_rate = split_rate;
		} else {
			perf = 0.0;
			split_perf = 0.0;
			max_split_perf = 0.0;
			max_split_rate = 0.0;
		}

		split_duration = duration;
		split_bytes = rm1->tx_buf_bytes;
		split_bufs = rm1->tx_buf_cnt;
		stats_print_update_banner("TOTAL", STATS_UPDATE_BANNER_LEN);
	} else {
		char core1[17];
		char core2[17];

		sprintf(core1, "CORE %02d", rm1->lcore_id);
		if (!rm2) {
			stats_print_update_banner(core1, STATS_UPDATE_BANNER_LEN);
		} else {
			sprintf(core2, "CORE %02d", rm2->lcore_id);
			stats_print_update_banner2(core1, core2, STATS_UPDATE_BANNER_LEN);
		}
	}

	if (!rm2) {
		fprintf(stdout,
			"| Recv Bytes:   %20lu |\n"
			"| Regex Bytes:  %20lu |\n"
			"| Recv Bufs:    %20lu |\n"
			"| Regex Bufs:   %20lu |\n"
			"| Matches:      %20lu |\n",
			rm1->rx_buf_bytes, rm1->tx_buf_bytes, rm1->rx_buf_cnt, rm1->tx_buf_cnt, reg1->rx_total_match);

		if (total) {
			fprintf(stdout,
				"|%*s|\n"
				"| Duration:     %20.4f |\n"
				"| Regex Perf (total): %14.4f |\n"
				"| Regex Perf (split): %14.4f |\n",
				STATS_UPDATE_BANNER_LEN - 2, "", duration, perf, split_perf);
		}
		fprintf(stdout, STATS_UPDATE_BORDER "\n\n");
	} else {
		fprintf(stdout,
			"| Recv Bytes:   %20lu |    | Recv Bytes:   %20lu |\n"
			"| Regex Bytes:  %20lu |    | Regex Bytes:  %20lu |\n"
			"| Recv Bufs:    %20lu |    | Recv Bufs:    %20lu |\n"
			"| Regex Bufs:   %20lu |    | Regex Bufs:   %20lu |\n"
			"| Matches:      %20lu |    | Matches:      %20lu |\n" STATS_UPDATE_BORDER
			"    " STATS_UPDATE_BORDER "\n\n",
			rm1->rx_buf_bytes, rm2->rx_buf_bytes, rm1->tx_buf_bytes, rm2->tx_buf_bytes, rm1->rx_buf_cnt,
			rm2->rx_buf_cnt, rm1->tx_buf_cnt, rm2->tx_buf_cnt, reg1->rx_total_match, reg2->rx_total_match);
	}
}


void
stats_print_update_simple(rb_stats_t *stats, int num_queues, double time, bool end)
{
	run_mode_stats_t total_rm;
	regex_stats_t total_regex;

	memset(&total_rm, 0, sizeof(run_mode_stats_t));
	memset(&total_regex, 0, sizeof(regex_stats_t));

	regex_stats_t *regex_stats = stats->regex_stats;
	run_mode_stats_t *rm_stats = stats->rm_stats;
	int i;

	for (i = 0; i < num_queues; i++) {
		total_rm.rx_buf_bytes += rm_stats[i].rx_buf_bytes;
		total_rm.rx_buf_cnt += rm_stats[i].rx_buf_cnt;
		total_rm.tx_buf_bytes += rm_stats[i].tx_buf_bytes;
		total_rm.tx_buf_cnt += rm_stats[i].tx_buf_cnt;
		total_regex.rx_total_match += regex_stats[i].rx_total_match;
	}


	stats_print_update_single_simple(&total_rm, &total_regex, NULL, NULL, true, time);
}


void
stats_print_update(rb_stats_t *stats, int num_queues, double time, bool end)
{
	run_mode_stats_t total_rm;
	regex_stats_t total_regex;

	memset(&total_rm, 0, sizeof(run_mode_stats_t));
	memset(&total_regex, 0, sizeof(regex_stats_t));

	regex_stats_t *regex_stats = stats->regex_stats;
	run_mode_stats_t *rm_stats = stats->rm_stats;
	int i;

	/* Clear terminal and move cursor to (0, 0). */
	// fprintf(stdout, "\033[2J");
	// fprintf(stdout, "\033[%d;%dH", 0, 0);

	if (end)
		stats_print_banner("END OF RUN PER QUEUE STATS", STATS_BANNER_LEN);
	else
		stats_print_banner("SPLIT PER QUEUE STATS", STATS_BANNER_LEN);
	fprintf(stdout, "\n");

	for (i = 0; i < num_queues; i++) {
		total_rm.rx_buf_bytes += rm_stats[i].rx_buf_bytes;
		total_rm.rx_buf_cnt += rm_stats[i].rx_buf_cnt;
		total_rm.tx_buf_bytes += rm_stats[i].tx_buf_bytes;
		total_rm.tx_buf_cnt += rm_stats[i].tx_buf_cnt;
		total_regex.rx_total_match += regex_stats[i].rx_total_match;

		/* Only print on even queue numbers. */
		if (!(i % 2) && i + 1 < num_queues)
			stats_print_update_single(&rm_stats[i], &regex_stats[i], &rm_stats[i + 1], &regex_stats[i + 1],
						  false, 0.0);
		else if (!(i % 2))
			stats_print_update_single(&rm_stats[i], &regex_stats[i], NULL, NULL, false, 0.0);
	}

	stats_print_update_single(&total_rm, &total_regex, NULL, NULL, true, time);
}

int cmpfunc (const void * a, const void * b)
{
   return ( *(int*)a - *(int*)b );
}

static void
stats_print_custom_rxp(rb_stats_t *stats, int num_queues, bool print_exp_matches, enum rxpbench_acc_dev dev,
		       uint32_t batches, bool lat_mode)
{
	regex_stats_t *regex_stats = stats->regex_stats;
	run_mode_stats_t *run_stats = stats->rm_stats;
	rxp_stats_t *rxp_stats;
	rxp_stats_t rxp_total;
	uint64_t total_bufs;
	int i;

	memset(&rxp_total, 0, sizeof(rxp_total));
	rxp_total.min_lat = UINT64_MAX;
	total_bufs = 0;

	for (i = 0; i < num_queues; i++) {
		rxp_stats = (rxp_stats_t *)regex_stats[i].custom;

		total_bufs += run_stats->tx_buf_cnt;

		rxp_total.rx_invalid += rxp_stats->rx_invalid;
		rxp_total.rx_timeout += rxp_stats->rx_timeout;
		rxp_total.rx_max_match += rxp_stats->rx_max_match;
		rxp_total.rx_max_prefix += rxp_stats->rx_max_prefix;
		rxp_total.rx_resource_limit += rxp_stats->rx_resource_limit;
		rxp_total.tx_busy += rxp_stats->tx_busy;
		rxp_total.rx_idle += rxp_stats->rx_idle;
		rxp_total.tot_lat += rxp_stats->tot_lat;
		if (rxp_stats->min_lat < rxp_total.min_lat)
			rxp_total.min_lat = rxp_stats->min_lat;
		if (rxp_stats->max_lat > rxp_total.max_lat)
			rxp_total.max_lat = rxp_stats->max_lat;

		rxp_total.exp.score7 += rxp_stats->exp.score7;
		rxp_total.exp.score6 += rxp_stats->exp.score6;
		rxp_total.exp.score4 += rxp_stats->exp.score4;
		rxp_total.exp.score0 += rxp_stats->exp.score0;
		rxp_total.exp.false_positives += rxp_stats->exp.false_positives;

		rxp_total.max_exp.score7 += rxp_stats->max_exp.score7;
		rxp_total.max_exp.score6 += rxp_stats->max_exp.score6;
		rxp_total.max_exp.score4 += rxp_stats->max_exp.score4;
		rxp_total.max_exp.score0 += rxp_stats->max_exp.score0;
		rxp_total.max_exp.false_positives += rxp_stats->max_exp.false_positives;
	}

	/* Get per core average for some of the total stats. */
	rxp_total.tx_busy /= num_queues;
	rxp_total.rx_idle /= num_queues;
	rxp_total.tot_lat = total_bufs ? rxp_total.tot_lat / total_bufs : 0;
	if (rxp_total.min_lat == UINT64_MAX)
		rxp_total.min_lat = 0;

	if (dev == REGEX_DEV_DPDK_REGEX)
		stats_print_banner("DPDK REGEX STATS", STATS_BANNER_LEN);
	else
		stats_print_banner("DOCA REGEX STATS", STATS_BANNER_LEN);
	fprintf(stdout,
		"|%*s|\n"
		"| INVALID RESPONSES:                %-42lu |\n"
		"| - TIMEOUT:                        %-42lu |\n"
		"| - MAX MATCHES:                    %-42lu |\n"
		"| - MAX PREFIXES:                   %-42lu |\n"
		"| - RESOURCE LIMIT:                 %-42lu |\n"
		"|%*s|\n"
		"| TX BUSY - AVE PER CORE (secs):    %-42.4f |\n"
		"|%*s|\n"
		"| RX IDLE - AVE PER CORE (secs):    %-42.4f |\n"
		"|%*s|\n",
		78, "", rxp_total.rx_invalid, rxp_total.rx_timeout, rxp_total.rx_max_match, rxp_total.rx_max_prefix,
		rxp_total.rx_resource_limit, 78, "", (double)rxp_total.tx_busy / rte_get_timer_hz(), 78, "",
		(double)rxp_total.rx_idle / rte_get_timer_hz(), 78, "");

	if (!lat_mode)
		fprintf(stdout,
			"| ** NOTE: NOT RUNNING IN LATENCY MODE (CAN TURN ON WITH --latency-mode) **    |\n"
			"|%*s|\n",
			78, "");

	/* only print tail lat from queue 0 */
	int tail_99_index = (int)(NUMBER_OF_SAMPLE*99)/100 ;
	int tail_999_index = (int)(NUMBER_OF_SAMPLE*999)/1000 ;
	rxp_stats = (rxp_stats_t *)regex_stats[0].custom;
	fprintf(stdout,
		"| PER PACKET LATENCY 						 	       |\n"
		"| - BATCH SIZE:  		    %-42.4u |\n"
		"| - # OF SAMPLES FOR TAIL:  	    %-42.4u |\n"
		"| - MAX LATENCY (usecs):            %-42.4f |\n"
		"| - MIN LATENCY (usecs):            %-42.4f |\n"
		"| - AVERAGE LATENCY (usecs):        %-42.4f |\n"
		"|%*s|\n" STATS_BORDER "\n",
		batches, NUMBER_OF_SAMPLE,(double)rxp_total.max_lat / rte_get_timer_hz() * 1000000.0,
		(double)rxp_total.min_lat / rte_get_timer_hz() * 1000000.0,
		(double)rxp_total.tot_lat / rte_get_timer_hz() * 1000000.0, 
		78, "");

	if (print_exp_matches) {
		fprintf(stdout, "\nExpected Match Report:\n");
		fprintf(stdout, "Info: Score_table(7:0) = {%lu, %lu, 0, %lu, 0, 0, 0, %lu}, false_positives = %lu\n",
			rxp_total.exp.score7, rxp_total.exp.score6, rxp_total.exp.score4, rxp_total.exp.score0,
			rxp_total.exp.false_positives);
		fprintf(stdout,
			"Info: Score_table_max(7:0) = {%lu, %lu, 0, %lu, 0, 0, 0, %lu}, false_positives = %lu\n\n",
			rxp_total.max_exp.score7, rxp_total.max_exp.score6, rxp_total.max_exp.score4,
			rxp_total.max_exp.score0, rxp_total.max_exp.false_positives);
	}
}

static void
stats_print_custom_hs(rb_stats_t *stats, int num_queues, uint32_t batches)
{
	regex_stats_t *regex_stats = stats->regex_stats;
	run_mode_stats_t *run_stats = stats->rm_stats;
	hs_stats_t *hs_stats;
	hs_stats_t hs_total;
	uint64_t total_bufs;
	int i;

	memset(&hs_total, 0, sizeof(hs_total));
	hs_total.min_lat = UINT64_MAX;
	total_bufs = 0;

	for (i = 0; i < num_queues; i++) {
		hs_stats = (hs_stats_t *)regex_stats[i].custom;
		total_bufs += run_stats->tx_buf_cnt;

		hs_total.tot_lat += hs_stats->tot_lat;
		if (hs_stats->min_lat < hs_total.min_lat)
			hs_total.min_lat = hs_stats->min_lat;
		if (hs_stats->max_lat > hs_total.max_lat)
			hs_total.max_lat = hs_stats->max_lat;
	}

	hs_total.tot_lat = total_bufs ? hs_total.tot_lat / total_bufs : 0;
	if (hs_total.min_lat == UINT64_MAX)
		hs_total.min_lat = 0;

	stats_print_banner("HYPERSCAN STATS", STATS_BANNER_LEN);
	fprintf(stdout,
		"|%*s|\n"
		"| PER PACKET LATENCY - BATCH SIZE:  %-42u |\n"
		"| - MAX LATENCY (usecs):            %-42.4f |\n"
		"| - MIN LATENCY (usecs):            %-42.4f |\n"
		"| - AVERAGE LATENCY (usecs):        %-42.4f |\n"
		"|%*s|\n" STATS_BORDER "\n",
		78, "", batches, (double)hs_total.max_lat / rte_get_timer_hz() * 1000000.0,
		(double)hs_total.min_lat / rte_get_timer_hz() * 1000000.0,
		(double)hs_total.tot_lat / rte_get_timer_hz() * 1000000.0, 78, "");
}



static void
stats_print_custom_comp_dpdk(rb_stats_t *stats, int num_queues, uint32_t batches)
{
	regex_stats_t *regex_stats = stats->regex_stats;
	run_mode_stats_t *run_stats = stats->rm_stats;
	comp_dpdk_stats_t *comp_stats;
	comp_dpdk_stats_t comp_total;
	uint64_t total_bufs;
	uint64_t total_src_bytes = 0;
	uint64_t total_dst_bytes = 0;
	int i;
	double ratio;

	memset(&comp_total, 0, sizeof(comp_total));
	comp_total.min_lat = UINT64_MAX;
	total_bufs = 0;

	for (i = 0; i < num_queues; i++) {
		comp_stats = (comp_dpdk_stats_t *)regex_stats[i].custom;
		total_bufs += run_stats->tx_buf_cnt;
		total_src_bytes += run_stats->tx_buf_bytes;
		total_dst_bytes += comp_stats->dst_buf_bytes;

		comp_total.tot_lat += comp_stats->tot_lat;
		if (comp_stats->min_lat < comp_total.min_lat)
			comp_total.min_lat = comp_stats->min_lat;
		if (comp_stats->max_lat > comp_total.max_lat)
			comp_total.max_lat = comp_stats->max_lat;
	}
	ratio = (double) total_dst_bytes/total_src_bytes;

	comp_total.tot_lat = total_bufs ? comp_total.tot_lat / total_bufs : 0;
	if (comp_total.min_lat == UINT64_MAX)
		comp_total.min_lat = 0;

	stats_print_banner("DPDK COMPRESS STATS", STATS_BANNER_LEN);
	fprintf(stdout,
		"|%*s|\n"
		"| BYTE RATIO (DST/SRC):  %-42.4f |\n"
		"| PER PACKET LATENCY - BATCH SIZE:  %-42u |\n"
		"| - MAX LATENCY (usecs):            %-42.4f |\n"
		"| - MIN LATENCY (usecs):            %-42.4f |\n"
		"| - AVERAGE LATENCY (usecs):        %-42.4f |\n"
		"|%*s|\n" STATS_BORDER "\n",
		78, "", ratio, batches, (double)comp_total.max_lat / rte_get_timer_hz() * 1000000.0,
		(double)comp_total.min_lat / rte_get_timer_hz() * 1000000.0,
		(double)comp_total.tot_lat / rte_get_timer_hz() * 1000000.0, 78, "");
}

static void
stats_print_custom(rb_conf *run_conf, rb_stats_t *stats, int num_queues)
{
	if (run_conf->regex_dev_type == REGEX_DEV_DPDK_REGEX || run_conf->regex_dev_type == REGEX_DEV_DOCA_REGEX)
		stats_print_custom_rxp(stats, num_queues, run_conf->input_exp_matches, run_conf->regex_dev_type,
				       run_conf->input_batches, run_conf->latency_mode);
	else if (run_conf->regex_dev_type == REGEX_DEV_HYPERSCAN){
		stats_print_custom_hs(stats, num_queues, run_conf->input_batches);
	}	
	else if (run_conf->regex_dev_type == COMP_DEV_DPDK_COMP){
		stats_print_custom_comp_dpdk(stats, num_queues, run_conf->input_batches);
	}
}

void
stats_print_end_of_run(rb_conf *run_conf, double run_time)
{
	rb_stats_t *stats = run_conf->stats;

	stats_print_update(stats, run_conf->cores, run_time, true);
	// stats_print_config(run_conf);
	// stats_print_common_stats(stats, run_conf->cores, run_time);
	stats_print_custom(run_conf, stats, run_conf->cores);
}

void
stats_clean(rb_conf *run_conf)
{
	rb_stats_t *stats = run_conf->stats;
	uint32_t i;

	for (i = 0; i < run_conf->cores; i++)
		rte_free(stats->regex_stats[i].custom);

	rte_free(stats->rm_stats);
	rte_free(stats->regex_stats);
	rte_free(stats);
	rte_free(run_conf->input_pkt_stats);
}
