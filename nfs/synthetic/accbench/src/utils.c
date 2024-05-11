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

#include <rte_byteorder.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_malloc.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include "rxpb_log.h"
#include "utils.h"

/* Read all or max_len bytes of file into buf, returning length in buf_len. */
int
util_load_file_to_buffer(const char *file, char **buf, uint64_t *buf_len, uint32_t max_len)
{
	uint64_t data_len;
	FILE *data_file;
	size_t read_len;
	long file_size;
	int ret;

	data_file = fopen(file, "r");
	if (!data_file) {
		RXPB_LOG_ERR("Failed to read file: %s.", file);
		return -ENOTSUP;
	}

	if (fseek(data_file, 0L, SEEK_END)) {
		RXPB_LOG_ERR("Read error on file: %s.", file);
		ret = -EINVAL;
		goto error_file;
	}

	file_size = ftell(data_file);
	if (file_size < 0) {
		RXPB_LOG_ERR("Text stream error from file: %s.", file);
		ret = -EINVAL;
		goto error_file;
	}

	if (max_len > file_size) {
		RXPB_LOG_ERR("Requested %u bytes but %lu detected in file %s.", max_len, file_size, file);
		ret = -EINVAL;
		goto error_file;
	}

	data_len = max_len ? max_len : file_size;
	*buf = rte_malloc(NULL, sizeof(char) * (data_len + 1), 4096);
	if (!*buf) {
		RXPB_LOG_ERR("Memory failure when loading file.");
		ret = -ENOMEM;
		goto error_file;
	}

	if (fseek(data_file, 0L, SEEK_SET)) {
		ret = -EINVAL;
		goto error_buf;
	}

	read_len = fread(*buf, sizeof(char), data_len, data_file);
	if (read_len != (unsigned long)data_len) {
		ret = -EINVAL;
		goto error_buf;
	}

	(*buf)[data_len] = '\0';

	fclose(data_file);
	*buf_len = data_len;

	return 0;

error_buf:
	rte_free(*buf);
error_file:
	fclose(data_file);

	return ret;
}

char *
util_trim_whitespace(char *input)
{
	char *end;

	while (isspace(*input))
		input++;

	end = input + strlen(input) - 1;

	while (end > input && isspace(*end))
		end -= 1;

	*(end + 1) = '\0';

	return input;
}

/* Verify string is a decimal of < given bytes and convert to long. */
int
util_str_to_dec(char *str, long *output, int max_bytes)
{
	size_t byte_len;
	long ret, mask;
	char *end_ptr;

	if (!str || !strlen(str) || !max_bytes || max_bytes > 8)
		return -EINVAL;

	errno = 0;
	ret = strtol(str, &end_ptr, 10);
	byte_len = end_ptr - str;

	/* Check strtol has not errored and that all str characters are used. */
	if (((ret == LONG_MAX || ret == LONG_MIN) && errno == ERANGE) || byte_len != strlen(str))
		return -EINVAL;

	/* Verify max bytes are not exceeded. */
	if (max_bytes < 8) {
		mask = (1L << (max_bytes * 8)) - 1;
		if (ret & ~mask)
			return -EINVAL;
	}

	*output = ret;

	return 0;
}

static inline int
util_parse_udp(const unsigned char *packet __rte_unused, uint32_t *pay_len, int *rte_ptype)
{
	/* UDP has constant size. */
	uint16_t offset = sizeof(struct rte_udp_hdr);

	if (*pay_len < offset)
		return -EINVAL;

	*pay_len = *pay_len - offset;
	*rte_ptype |= RTE_PTYPE_L4_UDP;

	return offset;
}

static inline int
util_parse_tcp(const unsigned char *packet, uint32_t *pay_len, int *rte_ptype)
{
	const struct rte_tcp_hdr *tcp = (const struct rte_tcp_hdr *)packet;
	uint16_t offset = 0;

	offset = (tcp->data_off & 0xf0) >> 2;
	if (*pay_len < offset)
		return -EINVAL;

	*pay_len = *pay_len - offset;
	*rte_ptype |= RTE_PTYPE_L4_TCP;

	return offset;
}

static inline int
util_parse_ipv6(const unsigned char *packet, uint32_t *pay_len, int *rte_ptype)
{
	const struct rte_ipv6_hdr *ipv6 = (const struct rte_ipv6_hdr *)packet;
	uint16_t offset;
	int ret;

	/* Currently not supporting extension headers. */
	offset = sizeof(struct rte_ipv6_hdr);

	/* In IPv6 payload does not include the IP header. */
	*pay_len = rte_be_to_cpu_16(ipv6->payload_len);
	*rte_ptype |= RTE_PTYPE_L3_IPV6;

	switch (ipv6->proto) {
	case IPPROTO_TCP:
		ret = util_parse_tcp(&packet[offset], pay_len, rte_ptype);
		if (ret < 0)
			return ret;
		return offset += ret;
	case IPPROTO_UDP:
		ret = util_parse_udp(&packet[offset], pay_len, rte_ptype);
		if (ret < 0)
			return ret;
		return offset += ret;
	default:
		return -EINVAL;
	}
}

static inline int
util_parse_ipv4(const unsigned char *packet, uint32_t *pay_len, int *rte_ptype)
{
	const struct rte_ipv4_hdr *ipv4 = (const struct rte_ipv4_hdr *)packet;
	uint16_t offset;
	int ret;

	offset = ipv4->version_ihl & 0xf;
	if (offset < 5)
		return -EINVAL;

	offset *= 4;

	/* In IPv4 total length includes the IP header. */
	*pay_len = rte_be_to_cpu_16(ipv4->total_length) - offset;
	*rte_ptype |= RTE_PTYPE_L3_IPV4;

	switch (ipv4->next_proto_id) {
	case IPPROTO_TCP:
		ret = util_parse_tcp(&packet[offset], pay_len, rte_ptype);
		if (ret < 0)
			return ret;
		return offset += ret;

	case IPPROTO_UDP:
		*rte_ptype |= RTE_PTYPE_L3_IPV4;
		ret = util_parse_udp(&packet[offset], pay_len, rte_ptype);
		if (ret < 0)
			return ret;
		return offset += ret;

	default:
		return -EINVAL;
	}
}

static inline uint16_t
util_parse_vlan(const unsigned char *packet, uint16_t *proto, int *rte_ptype)
{
	const struct rte_vlan_hdr *vlan = (const struct rte_vlan_hdr *)packet;
	uint16_t offset;

	offset = sizeof(struct rte_vlan_hdr);
	*proto = vlan->eth_proto;

	/* Allow qnq with max of 2. */
	if (*proto == rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN)) {
		offset += sizeof(struct rte_vlan_hdr);
		*proto = vlan->eth_proto;
		*rte_ptype |= RTE_PTYPE_L2_ETHER_QINQ;
	} else {
		*rte_ptype |= RTE_PTYPE_L2_ETHER_VLAN;
	}

	return offset;
}

/* Parse layers 2-4 into mask and return payload pointer along with length. */
int
util_get_app_layer_payload(const unsigned char *packet, uint32_t *pay_len, int *rte_ptype)
{
	const struct rte_ether_hdr *eth = (const struct rte_ether_hdr *)packet;
	uint16_t ether_type;
	uint16_t offset;
	int ret;

	ether_type = eth->ether_type;
	offset = sizeof(struct rte_ether_hdr);

	if (ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN))
		offset += util_parse_vlan(&packet[offset], &ether_type, rte_ptype);

	if (ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
		ret = util_parse_ipv4(&packet[offset], pay_len, rte_ptype);
		if (ret < 0)
			return ret;
		return offset += ret;
	} else if (ether_type == rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV6)) {
		ret = util_parse_ipv6(&packet[offset], pay_len, rte_ptype);
		if (ret < 0)
			return ret;
		return offset += ret;
	} else {
		return -EINVAL;
	}
}
