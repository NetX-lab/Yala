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

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <regex-compiler.h>
#include <rte_malloc.h>

#include "rules_file_utils.h"
#include "rxpb_log.h"
#include "utils.h"

#define ID_SEPARATOR_HS	 ':'
#define ID_SEPARATOR_RXP ','

/* Parse ID and separator to predict rules type. */
static bool
rules_file_utils_check_id_separator(char *rule, char separator)
{
	int rule_len;
	int i;

	rule_len = strlen(rule);
	if (!rule_len)
		return false;

	/* Check for series of digits followed by separator and fwd slash. */
	for (i = 0; i < rule_len; i++)
		if (rule[i] == separator && i > 0 && i < rule_len - 3 &&
		    ((rule[i + 1] == ' ' && rule[i + 2] == '/') || rule[i + 1] == '/'))
			return true;
		else if (!isdigit(rule[i]))
			return false;

	return false;
}

/* Predict if a rules file is HS or RXP format based on separator used. */
static enum rules_file_utils_type
rules_file_utils_determine_type(char *rule)
{
	if (rules_file_utils_check_id_separator(rule, ID_SEPARATOR_HS))
		return RULES_FILE_HS;
	if (rules_file_utils_check_id_separator(rule, ID_SEPARATOR_RXP))
		return RULES_FILE_RXP;

	return RULES_FILE_UNKNOWN;
}

/* Valididate rule as aligning to the given type and split into arrays. */
int
rules_file_utils_parse_rule(char *rule, char **exp, char **id, char **flags, enum rules_file_utils_type type)
{
	/*
	 * HS rules must be ID:/EXP/FLAGS.
	 * RXP rules are ID, /EXP/FLAGS with the optional space after 'ID,'.
	 */
	int rule_len = strlen(rule);
	char id_separator;
	int i;

	if (type == RULES_FILE_HS) {
		id_separator = ID_SEPARATOR_HS;
	} else if (type == RULES_FILE_RXP) {
		id_separator = ID_SEPARATOR_RXP;
	} else {
		return -ENOTSUP;
	}

	*id = NULL;
	/* Must be at least 3 bytes after id so no need to run full length. */
	for (i = 0; i < rule_len - 3; i++) {
		/* First separator should indicate the id. */
		if (rule[i] == id_separator) {
			rule[i] = '\0';
			*id = rule;

			if (rule[i + 1] == '/') {
				*exp = &rule[i + 2];
			} else if (type == RULES_FILE_RXP) {
				/* Allow a space in RXP rules. */
				if (rule[i + 1] == ' ' && rule[i + 2] == '/')
					*exp = &rule[i + 3];
			} else {
				return -EINVAL;
			}

			break;
		}
	}

	if (!*id)
		return -EINVAL;

	/* Find last slash to mark end of the expression. */
	for (i = rule_len - 1; i >= 0; i--) {
		if (rule[i] == '/') {
			if (&rule[i] <= *exp)
				return -EINVAL;

			rule[i] = '\0';
			*flags = &rule[i + 1];
			break;
		}
	}

	return 0;
}

static int
rules_file_utils_rxp_flags_to_hs(char *output_flags, char *input_flags, int *skipped_flags)
{
	size_t i;
	int j;

	j = 0;
	/* Parse RXP flags and convert to HS format. */
	for (i = 0; i < strlen(input_flags); i++) {
		switch (input_flags[i]) {
		case 'i': /* case insensitive. */
			output_flags[j++] = 'i';
			break;
		case 'm': /* multi-line matching. */
			output_flags[j++] = 'm';
			break;
		case 's': /* . as any character. */
			output_flags[j++] = 's';
			break;
		case 'x': /* extended syntax. */
			output_flags[j++] = 'x';
			break;
		case '\r':
			/* silently ignore carriage returns. */
			break;
		case 'c': /* subpattern matching. */
		case 'o': /* split alternations. */
		case 'O': /* no split alternations. */
		case 'q': /* strict quantifier mode. */
			(*skipped_flags)++;
			break;
		default:
			RXPB_LOG_ERR("Unrecognised RXP flag: %c,", input_flags[i]);
			return -ENOTSUP;
		}
	}

	output_flags[j] = '\0';

	return 0;
}

/* Convert RXP rules to HS format. */
static int
rules_file_utils_write_hs(FILE *file, char *exp, char *id, char *flags, int *skipped_flags)
{
	char hs_flags[10] = {0};
	int ret;

	if (!flags) {
		fprintf(file, "%s:/%s/\n", id, exp);
		return 0;
	}

	if (strlen(flags) > 9) {
		RXPB_LOG_ERR("Rule flags too long for HS: %s.", flags);
		return -ENOTSUP;
	}

	ret = rules_file_utils_rxp_flags_to_hs(&hs_flags[0], flags, skipped_flags);
	if (ret) {
		RXPB_LOG_ERR("Failed convert HS flags to RXP: %s.", flags);
		return ret;
	}

	fprintf(file, "%s:/%s/%s\n", id, exp, hs_flags);

	return 0;
}

static int
rules_file_utils_hs_flags_to_rxp(char *output_flags, char *input_flags, int *skipped_flags)
{
	size_t i;
	int j;

	j = 0;
	/* Parse HS flags and convert to RXP format. */
	for (i = 0; i < strlen(input_flags); i++) {
		switch (input_flags[i]) {
		case 'i': /* case insensitive. */
			output_flags[j++] = 'i';
			break;
		case 'm': /* multi-line matching. */
			output_flags[j++] = 'm';
			break;
		case 's': /* . as any character. */
			output_flags[j++] = 's';
			break;
		case 'x': /* extended syntax. */
			output_flags[j++] = 'x';
			break;
		case '\r':
			/* silently ignore carriage returns. */
			break;
		case 'H': /* single match. */
		case 'V': /* allow empty. */
		case '8': /* UTF8. */
		case 'W': /* unicode. */
			(*skipped_flags)++;
			break;
		default:
			RXPB_LOG_ERR("Unrecognised HS flag: %c.", input_flags[i]);
			return -ENOTSUP;
		}
	}

	output_flags[j] = '\0';

	return 0;
}

static int
rules_file_utils_escape_fwd_slash(char *exp, char **modified_exp)
{
	int rule_length = strlen(exp);
	int back_slash_cnt = 0;
	int fwd_slash_cnt = 0;
	int i, j = 0;

	/* Parse rule for unescaped forward slashes. */
	for (i = 0; i < rule_length; i++)
		if (exp[i] == '/') {
			if (back_slash_cnt % 2 == 0)
				fwd_slash_cnt++;
		} else if (exp[i] == '\\') {
			back_slash_cnt++;
		} else {
			back_slash_cnt = 0;
		}

	if (!fwd_slash_cnt)
		return 0;

	*modified_exp = rte_malloc(NULL, rule_length + fwd_slash_cnt, 0);
	if (!*modified_exp) {
		RXPB_LOG_ERR("Memory failure in rule formatting.");
		return -ENOMEM;
	}

	back_slash_cnt = 0;
	/* Copy expressions with forward slashes escaped. */
	for (i = 0; i < rule_length; i++) {
		if (exp[i] == '/' && (back_slash_cnt % 2 == 0)) {
			(*modified_exp)[j++] = '\\';
			(*modified_exp)[j++] = exp[i];
		} else {
			if (exp[i] == '\\')
				back_slash_cnt++;
			else
				back_slash_cnt = 0;
			(*modified_exp)[j++] = exp[i];
		}
	}

	return 0;
}

/* Convert HS rules to RXP format. */
static int
rules_file_utils_write_rxp(FILE *file, char *exp, char *id, char *flags, int *skipped_flags)
{
	char rxp_flags[10] = {0};
	char *modified_exp;
	int ret;

	if (!flags) {
		fprintf(file, "%s, /%s/\n", id, exp);
		return 0;
	}

	if (strlen(flags) > 9) {
		RXPB_LOG_ERR("Rule flags too long for RXP: %s.", flags);
		return -ENOTSUP;
	}

	ret = rules_file_utils_hs_flags_to_rxp(&rxp_flags[0], flags, skipped_flags);
	if (ret) {
		RXPB_LOG_ERR("Failed convert RXP flags to HS: %s.", flags);
		return ret;
	}

	modified_exp = NULL;
	/* HS may not escape '/' characters - RXP requires this. */
	ret = rules_file_utils_escape_fwd_slash(exp, &modified_exp);
	if (ret)
		return ret;

	if (modified_exp)
		exp = modified_exp;

	fprintf(file, "%s, /%s/%s\n", id, exp, rxp_flags);

	if (modified_exp)
		rte_free(modified_exp);

	return 0;
}

/* Convert raw_rules to type output - output_file is NULL if no conversion. */
int
rules_file_utils_convert_rules(rb_conf *run_conf, const char *raw_rules, const char **output_file,
			       enum rules_file_utils_type output)
{
	enum rules_file_utils_type type = RULES_FILE_UNKNOWN;
	FILE *converted_rules = NULL;
	char *rule_cpy = NULL;
	int skipped_flags = 0;
	uint64_t rules_len;
	char *flags;
	char *rules;
	char *rule;
	char *exp;
	char *id;
	int ret;

	ret = util_load_file_to_buffer(raw_rules, &rules, &rules_len, 0);
	if (ret) {
		RXPB_LOG_ERR("Failed to read in rule from %s.", raw_rules);
		return ret;
	}

	rule = strtok(rules, "\n");
	while (rule != NULL) {
		if (rule[0] == '#' || !strlen(rule)) {
			rule = strtok(NULL, "\n");
			continue;
		}

		flags = NULL;
		exp = NULL;
		id = NULL;

		/* Only true for the first rule. */
		if (type == RULES_FILE_UNKNOWN) {
			type = rules_file_utils_determine_type(rule);

			if (type == RULES_FILE_UNKNOWN) {
				RXPB_LOG_ERR("Rules format invalid: %s.", raw_rules);
				ret = -EINVAL;
				goto out;
			}

			/* Return if file is already in required format. */
			if (type == output) {
				*output_file = NULL;
				goto out;
			}

			converted_rules = fopen(*output_file, "w");
		}

		rule_cpy = strdup(rule);

		ret = rules_file_utils_parse_rule(rule, &exp, &id, &flags, type);
		if (ret) {
			RXPB_LOG_ERR("Syntax error detected in %s.", rule_cpy);
			goto out;
		}

		if (output == RULES_FILE_HS) {
			ret = rules_file_utils_write_hs(converted_rules, exp, id, flags, &skipped_flags);
			if (ret)
				goto out;
		} else if (output == RULES_FILE_RXP) {
			ret = rules_file_utils_write_rxp(converted_rules, exp, id, flags, &skipped_flags);
			if (ret)
				goto out;
		}

		free(rule_cpy);
		rule_cpy = NULL;
		rule = strtok(NULL, "\n");
	}

	if (skipped_flags) {
		if (output == RULES_FILE_HS)
			RXPB_LOG_WARN_REC(run_conf, "%d RXP flags [coOq] with no HS alternative ignored.",
					  skipped_flags);
		else
			RXPB_LOG_WARN_REC(run_conf, "%d HS flags [HV8W] with no RXP alternative ignored.",
					  skipped_flags);
	}

out:
	if (converted_rules)
		fclose(converted_rules);
	rte_free(rules);
	free(rule_cpy);

	return ret;
}

int
rules_file_compile_for_rxp(rb_conf *run_conf)
{
	struct regex_compilation_statistics *comp_stats = NULL;
	char file_loc[] = "rxp_tmp/rxp_tmp.rof2";
	uint32_t global_regex_opts = 0;
	struct regex_rof *rof = NULL;
	struct regex_ruleset ruleset;
	const char *converted_rules;
	char *ruleset_err = NULL;
	uint32_t compiler_output;
	uint32_t compiler_opts;
	uint32_t i, j;
	int ret = 0;

	if (run_conf->compiled_rules_file)
		return 0;

	if (!run_conf->raw_rules_file)
		return -EINVAL;

	/* Create new directory for rule conversion if one does not exist. */
	ret = mkdir("rxp_tmp", 0755);
	if (ret && errno != EEXIST) {
		RXPB_LOG_ERR("Failed creating rxp temp folder.");
		return -errno;
	}

	converted_rules = "rxp_tmp/rxp_tmp.rxp";

	/* Convert to rxp rules if in hyperscan format. */
	ret = rules_file_utils_convert_rules(run_conf, run_conf->raw_rules_file, &converted_rules, RULES_FILE_RXP);
	if (ret) {
		RXPB_LOG_ERR("Failed to convert raw rules to rxp format.");
		return ret;
	}

	/* Converted_rules is NULL if a new file was not required. */
	if (converted_rules) {
		free(run_conf->raw_rules_file);
		run_conf->raw_rules_file = strdup(converted_rules);
		if (!run_conf->raw_rules_file) {
			RXPB_LOG_ERR("Memory failure in rules compilation.");
			return -ENOMEM;
		}
	}

	ruleset.number_of_entries = 0;
	ret = regex_read_ruleset_file(run_conf->raw_rules_file, &ruleset, &ruleset_err);
	if (ret != REGEX_STATUS_OK) {
		RXPB_LOG_ERR("Failed reading ruleset from raw file.");
		ret = -ENOMEM;
		goto err_free_resources;
	}

	RXPB_LOG_ALERT("Compiling rule file with default params.\n"
		       "Better performance may be achieved by compiling separately with tailored inputs.");

	compiler_output = REGEX_COMPILER_OUTPUT_COMPILATION_STATISTICS;

	compiler_opts = REGEX_COMPILER_OPTIONS_DISABLE_BIDIRECTIONAL;
	if (run_conf->force_compile)
		compiler_opts |= REGEX_COMPILER_OPTIONS_FORCE;

	/* Always turn off single line mode unless the user specifically requests it. */
	if (!run_conf->single_line)
		global_regex_opts |= REGEX_GLOBAL_REGEX_OPTIONS_NO_SINGLE;
	if (run_conf->caseless)
		global_regex_opts |= REGEX_GLOBAL_REGEX_OPTIONS_CASELESS;
	if (run_conf->multi_line)
		global_regex_opts |= REGEX_GLOBAL_REGEX_OPTIONS_MULTILINE;
	if (run_conf->free_space)
		global_regex_opts |= REGEX_GLOBAL_REGEX_OPTIONS_FREE_SPACING;

	ret = regex_compile(&ruleset, NULL, compiler_opts, global_regex_opts, 1, REGEX_VERSION_V5_7,
			    REGEX_VERBOSE_LEVEL_1, compiler_output, &comp_stats, NULL, NULL, NULL,
			    &rof, NULL);
	if (ret != REGEX_STATUS_OK) {
		RXPB_LOG_ERR("Regex rules compilation error.");
		ret = -EINVAL;
		goto err_free_resources;
	}

	if (comp_stats->total_number_of_rules != comp_stats->rules_compiled)
		RXPB_LOG_WARN_REC(run_conf, "%u of %u rules compiled.", comp_stats->rules_compiled,
				  comp_stats->total_number_of_rules);

	ret = regex_write_rof_file(file_loc, rof);
	if (ret != REGEX_STATUS_OK) {
		RXPB_LOG_ERR("Failed to create rof2 file in rule compilation.");
		ret = -ENOMEM;
		goto err_free_resources;
	}

	run_conf->compiled_rules_file = strdup("rxp_tmp/rxp_tmp.rof2.binary");
	if (!run_conf->compiled_rules_file) {
		RXPB_LOG_ERR("Memory failure storing compiled file name.");
		ret = -ENOMEM;
		goto err_free_resources;
	}

err_free_resources:
	if (regex_free_structs(&comp_stats, NULL, NULL, NULL, &rof, NULL, NULL, NULL, 1) != REGEX_STATUS_OK) {
		RXPB_LOG_ERR("Failed to release regex_compiler resources.\n");
		ret = -ENOMEM;
	}

	if (ruleset_err)
		free(ruleset_err);

	if (ruleset.number_of_entries) {
		for (i = 0; i < ruleset.number_of_entries; i++) {
			for (j = 0; j < ruleset.rules[i].number_of_prefix_entries; j++)
				free(ruleset.rules[i].prefix[j]);
			free(ruleset.rules[i].prefix);
			free(ruleset.rules[i].rule);
		}

		free(ruleset.rules);
	}

	return ret;
}
