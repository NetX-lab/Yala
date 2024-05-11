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

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <click/dpdkbfregex_rxpb_log.h>

#define ALERT_MARKER	"\n******************************************************************\n"

pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

/* Store any warnings for inclusion in end of run output. */
static void
rxpb_log_record(rb_conf *run_conf, const char *warning)
{
	uint32_t conf_pos = run_conf->no_conf_warnings;

	if (conf_pos == MAX_WARNINGS)
		return;

	if (conf_pos == MAX_WARNINGS - 1)
		run_conf->conf_warning[conf_pos] = strdup("-- warnings suppressed --");
	else
		run_conf->conf_warning[conf_pos] = strdup(warning);

	if (!run_conf->conf_warning[conf_pos]) {
		RXPB_LOG_WARN("Memory failure recording warning.");
		return;
	}

	run_conf->no_conf_warnings = conf_pos + 1;
}

static void
__rxpb_log(rb_conf *run_conf, enum rxpb_log_level level, const char *format, va_list params)
{
	FILE *output;

	/* Only allow recording of warnings in initialisation phase - thread safe. */
	if (run_conf && !run_conf->running) {
		char warn_str[MAX_WARNING_LEN];
		va_list params_copy = {0};
		int size;

		va_copy(params_copy, params);
		size = vsnprintf(warn_str, MAX_WARNING_LEN, format, params_copy);
		if (size < 0)
			RXPB_LOG_WARN("Failed to record warning message.");
		rxpb_log_record(run_conf, warn_str);
		va_end(params_copy);
	}

	output = level == RXPB_LOG_LEVEL_ERROR ? stderr : stdout;

	pthread_mutex_lock(&log_lock);

	switch (level) {
	case RXPB_LOG_LEVEL_ERROR:
		fprintf(output, "<< ERROR: ");
		vfprintf(output, format, params);
		fprintf(output, " >>\n");
		break;
	case RXPB_LOG_LEVEL_WARNING:
		fprintf(output, "<< WARNING: ");
		vfprintf(output, format, params);
		fprintf(output, " >>\n");
		break;
	case RXPB_LOG_LEVEL_INFO:
		fprintf(output, "INFO: ");
		vfprintf(output, format, params);
		fprintf(output, "\n");
		break;
	case RXPB_LOG_LEVEL_ALERT:
		fprintf(output, ALERT_MARKER"ALERT: ");
		vfprintf(output, format, params);
		fprintf(output, ALERT_MARKER);
		break;
	}

	pthread_mutex_unlock(&log_lock);
}

void
rxpb_log(rb_conf *run_conf, enum rxpb_log_level level, const char *message, ...)
{
	va_list params;

	va_start(params, message);
	__rxpb_log(run_conf, level, message, params);
	va_end(params);
}
