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

#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include <rte_byteorder.h>
#include <rte_ethdev.h>

#include <doca_log.h>
#include <doca_flow.h>

#include "flow_common.h"

DOCA_LOG_REGISTER(FLOW_SHARED_COUNTER);

// uint64_t tot_pkts;
// uint64_t prev_tot_pkts;
// uint64_t tot_bytes;
// uint64_t prev_tot_bytes;
// struct timeval start;
// struct timeval end;
// double run_time;
// double prev_run_time;
// double rate;

#define AGE_TIME_SECONDS 7200
#define AGE_ENTRY_PER_GROUP 256
#define AGE_ENTRY_GROUPS 8
#define AGE_ENTRIES (AGE_ENTRY_PER_GROUP * AGE_ENTRY_GROUPS)

#define PACKET_BURST 128	/* The number of packets in the rx queue */

#define STATS_INTERVAL_SEC	1
#define RUN_TIME_SEC	20
#define STATS_INTERVAL_CYCLES	STATS_INTERVAL_SEC * rte_get_timer_hz()
#define RUN_TIME_CYCLES RUN_TIME_SEC * rte_get_timer_hz()

/* user data struct for each entry */
struct sample_user_data {
	int entry_num;				/* entry number */
	int port_id;				/* port ID of the entry */
	struct doca_flow_pipe_entry *entry;	/* entry pointer */
};


struct doca_flow_pipe_entry *age_entry[AGE_ENTRIES];

uint64_t tot_pkts;
uint64_t prev_tot_pkts;
uint64_t tot_bytes;
uint64_t cycles;
uint64_t prev_cycles;
uint64_t start;
double run_time;
double rate;
FILE *file;

/*
 * Dequeue packets from DPDK queues
 *
 * @ingress_port [in]: port id for dequeue packets
 */
int
process_packets(int ingress_port)
{
	struct rte_mbuf *packets[PACKET_BURST];
	int queue_index = 0;
	int nb_packets;
	int i;
	int delay_cnt = 0;
	uint64_t meta_sum = 0;
	int ret=0;

	nb_packets = rte_eth_rx_burst(ingress_port, queue_index, packets, PACKET_BURST);

	if (nb_packets){
		tot_pkts += nb_packets;
		cycles = rte_rdtsc() - start;
		if (cycles - prev_cycles > STATS_INTERVAL_CYCLES) {
			run_time = (double)(cycles - prev_cycles) / rte_get_timer_hz();
			rate = ((tot_pkts-prev_tot_pkts) / run_time)/ 1000.0;
			file = fopen("target_perf","a");
			fprintf(file,"%.4lf\tKpps\n",rate);
			fclose(file);
			prev_cycles = cycles;
			prev_tot_pkts = tot_pkts;
			ret = 1;
		}
	}

	/* software processing of packets */
	for (i = 0; i < nb_packets; i++) {
		/* Operation on meta */
		if (rte_flow_dynf_metadata_avail()){
			meta_sum += *RTE_FLOW_DYNF_METADATA(packets[i]);
			delay_cnt++;
		}	
	}

	/* Synthetic processing time */
	rte_delay_us_block(delay_cnt);

	/* Free the packets */
	for(i=0; i<nb_packets; i++){
		rte_pktmbuf_free(packets[i]);	
	}
	return ret;
}

/*
 * Create DOCA Flow pipe with 5 tuple match, changeable set meta action, and forward RSS
 *
 * @port [in]: port of the pipe
 * @pipe [out]: created pipe pointer
 * @error [out]: output error
 * @return: 0 on success, negative value otherwise and error is set.
 */
int
create_rss_meta_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe, struct doca_flow_error *error)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd fwd_miss;
	struct doca_flow_pipe_cfg pipe_cfg;
	uint16_t rss_queues[1];

	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&fwd_miss, 0, sizeof(fwd_miss));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	/* set mask value */
	actions.meta.pkt_meta = UINT32_MAX;

	pipe_cfg.attr.name = "RSS_META_PIPE";
	pipe_cfg.match = &match;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.port = port;

	/* 5 tuple match */
	match.out_l4_type = DOCA_PROTO_UDP;
	match.out_src_ip.ipv4_addr = 0;
	match.out_dst_ip.ipv4_addr = 0;
	match.out_src_port = 0;
	match.out_dst_port = 0;

	/* RSS queue - send matched traffic to queue 0  */
	rss_queues[0] = 0;
	fwd.type = DOCA_FLOW_FWD_RSS;
	fwd.rss_queues = rss_queues;
	fwd.rss_flags = DOCA_FLOW_RSS_IP;
	fwd.num_of_queues = 1;

	fwd_miss.type = DOCA_FLOW_FWD_DROP;

	*pipe = doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, error);
	if (*pipe == NULL)
		return -1;
	return 0;
}

/*
 * Add DOCA Flow pipe entry with example 5 tuple to match and set meta data value
 *
 * @pipe [in]: pipe of the entry
 * @port [in]: port of the entry
 * @error [out]: output error
 * @return: 0 on success, negative value otherwise and error is set.
 */
int
add_rss_meta_pipe_entry(struct doca_flow_pipe *pipe, struct doca_flow_port *port, struct doca_flow_error *error)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_pipe_entry *entry;
	int result;
	int num_of_entries = 1;

	/* example 5-tuple to drop */
	// doca_be32_t dst_ip_addr = BE_IPV4_ADDR(1, 1, 1, 1);
	doca_be32_t src_ip_addr = BE_IPV4_ADDR(100, 100, 100, 100);
	doca_be16_t dst_port = rte_cpu_to_be_16(1234);
	doca_be16_t src_port = rte_cpu_to_be_16(1234);


	memset(&match, 0, sizeof(match));
	memset(&actions, 0, sizeof(actions));

	//match.out_dst_ip.ipv4_addr = dst_ip_addr;
	match.out_src_ip.ipv4_addr = src_ip_addr;
	match.out_dst_port = dst_port;
	match.out_src_port = src_port;

	/* set meta value */
	actions.meta.pkt_meta = 10;
	actions.action_idx = 0;

	entry = doca_flow_pipe_add_entry(0, pipe, &match, &actions, NULL, NULL, 0, NULL, error);
	result = doca_flow_entries_process(port, 0, DEFAULT_TIMEOUT_US, num_of_entries);
	if (result != num_of_entries || doca_flow_pipe_entry_get_status(entry) != DOCA_FLOW_ENTRY_STATUS_SUCCESS)
		return -1;

	return 0;
}

/*
 * Create DOCA Flow pipe with five tuple match and monitor with aging flag
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @pipe [out]: created pipe pointer
 * @error [out]: output error
 * @return: 0 on success, negative value otherwise and error is set.
 */
int
create_aging_pipe(struct doca_flow_port *port, int port_id, struct doca_flow_pipe **pipe, struct doca_flow_pipe **next_pipe, struct doca_flow_error *error)
{
	struct doca_flow_match match;
	struct doca_flow_monitor monitor;
	struct doca_flow_actions actions;
	struct doca_flow_actions *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd fwd_miss;
	struct doca_flow_pipe_cfg pipe_cfg = {0};

	memset(&match, 0, sizeof(match));
	memset(&monitor, 0, sizeof(monitor));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&fwd, 0, sizeof(fwd_miss));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "AGING_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	pipe_cfg.monitor = &monitor;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.port = port;

	pipe_cfg.attr.nb_flows = 2 * AGE_ENTRIES;

	/* set monitor with aging */
	monitor.flags = DOCA_FLOW_MONITOR_AGING;
	monitor.flags |= DOCA_FLOW_MONITOR_COUNT;

	/* 5 tuple match */
	match.out_l4_type = DOCA_PROTO_UDP;
	match.out_src_ip.type = DOCA_FLOW_IP4_ADDR;
	match.out_src_ip.ipv4_addr = 0xffffffff;
	match.out_dst_ip.type = DOCA_FLOW_IP4_ADDR;
	match.out_dst_ip.ipv4_addr = 0xffffffff;
	match.out_src_port = 0xffff;
	match.out_dst_port = 0xffff;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = *next_pipe;
	
	fwd_miss.type = DOCA_FLOW_FWD_PIPE;
	fwd_miss.next_pipe = *next_pipe;

	*pipe = doca_flow_pipe_create(&pipe_cfg, &fwd, &fwd_miss, error);
	if (*pipe == NULL)
		return -1;
	return 0;
}

/*
 * Add DOCA Flow entries to the aging pipe, each entry with different aging time
 *
 * @pipe [in]: pipe of the entries
 * @user_data [in]: user data array
 * @port [in]: port of the entries
 * @port_id [in]: port ID of the entries
 * @num_of_aging_entries [in]: number of entries to add
 * @return: 0 on success and negative value otherwise.
 */
int
add_aging_pipe_entries(struct doca_flow_pipe *pipe, struct sample_user_data *user_data,
		struct doca_flow_port *port, int port_id, int num_of_aging_entries)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_monitor monitor;
	struct doca_flow_error error;
	int i, j, nb_rule, result;
	enum doca_flow_flags_type flags = DOCA_FLOW_NO_WAIT;
	doca_be32_t dst_ip_addr;/* set different dst ip per entry */
	doca_be16_t dst_port = rte_cpu_to_be_16(1234);
	doca_be16_t src_port = rte_cpu_to_be_16(1234);
	doca_be32_t src_ip_addr = BE_IPV4_ADDR(100, 100, 100, 100); 

	num_of_aging_entries = AGE_ENTRIES;

	for (i = 0; i < AGE_ENTRY_GROUPS - 1; i++) {
		for (j = 0; j < AGE_ENTRY_PER_GROUP - 1; j++) {
			nb_rule = i*AGE_ENTRY_PER_GROUP+j;
			dst_ip_addr = BE_IPV4_ADDR(0, 0, i, j);

			memset(&match, 0, sizeof(match));
			memset(&actions, 0, sizeof(actions));
			memset(&monitor, 0, sizeof(monitor));

			monitor.flags |= DOCA_FLOW_MONITOR_AGING;
			monitor.flags |= DOCA_FLOW_MONITOR_COUNT;
			/* set user data struct in monitor */
			monitor.user_data = (uint64_t)(&user_data[nb_rule]);
			monitor.aging = (uint32_t)AGE_TIME_SECONDS;

			match.out_dst_ip.ipv4_addr = dst_ip_addr;
			match.out_src_ip.ipv4_addr = src_ip_addr;
			match.out_dst_port = dst_port;
			match.out_src_port = src_port;
			/* fill user data with entry number and entry pointer */
			user_data[nb_rule].entry_num = nb_rule;
			user_data[nb_rule].port_id = port_id;

			user_data[nb_rule].entry = doca_flow_pipe_add_entry(0, pipe, &match, &actions, &monitor, NULL, flags, NULL, &error);
			age_entry[nb_rule] = user_data[nb_rule].entry;
			if (user_data[nb_rule].entry == NULL) {
				DOCA_LOG_ERR("Failed to add entry - %s (%u)", error.message, error.type);
				return -1;
			}
			result = doca_flow_entries_process(port, 0, DEFAULT_TIMEOUT_US, 1);
			if (result != 1 || doca_flow_pipe_entry_get_status(user_data[i].entry) != DOCA_FLOW_ENTRY_STATUS_SUCCESS){
				DOCA_LOG_ERR("Entry not offloaded successfully");
				return -1;
			}
		}
		
	}
	DOCA_LOG_INFO("Added %d flow aging rules",AGE_ENTRIES);
	
	return 0;
}

/*
 * Create DOCA Flow pipe with 5 tuple match and monitor with shared counter ID
 *
 * @port [in]: port of the pipe
 * @port_id [in]: port ID of the pipe
 * @out_l4_type [in]: l4 type to match: UDP/TCP
 * @pipe [out]: created pipe pointer
 * @error [out]: output error
 * @return: 0 on success, negative value otherwise and error is set.
 */
int
create_shared_counter_pipe(struct doca_flow_port *port, int port_id, uint8_t out_l4_type,
			   struct doca_flow_pipe **pipe, struct doca_flow_pipe **next_pipe, struct doca_flow_error *error)
{
	struct doca_flow_match match;
	struct doca_flow_monitor monitor;
	struct doca_flow_actions actions, *actions_arr[NB_ACTIONS_ARR];
	struct doca_flow_fwd fwd;
	struct doca_flow_fwd fwd_miss;
	struct doca_flow_pipe_cfg pipe_cfg = {0};

	memset(&match, 0, sizeof(match));
	memset(&monitor, 0, sizeof(monitor));
	memset(&actions, 0, sizeof(actions));
	memset(&fwd, 0, sizeof(fwd));
	memset(&fwd_miss, 0, sizeof(fwd_miss));
	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "SHARED_COUNTER_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_BASIC;
	pipe_cfg.match = &match;
	pipe_cfg.monitor = &monitor;
	actions_arr[0] = &actions;
	pipe_cfg.actions = actions_arr;
	pipe_cfg.attr.is_root = false;
	pipe_cfg.attr.nb_actions = NB_ACTIONS_ARR;
	pipe_cfg.port = port;

	pipe_cfg.attr.nb_flows = 65536*8;

	/* 5 tuple match */
	match.out_l4_type = out_l4_type;
	match.out_src_ip.type = DOCA_FLOW_IP4_ADDR;
	match.out_src_ip.ipv4_addr = 0xffffffff;
	match.out_dst_ip.type = DOCA_FLOW_IP4_ADDR;
	match.out_dst_ip.ipv4_addr = 0xffffffff;
	// match.out_dst_ip.ipv4_addr = 0;
	match.out_src_port = 0xffff;
	match.out_dst_port = 0xffff;

	/* monitor with changeable shared counter ID */
	monitor.shared_counter_id = 0xffffffff;

	/* forwarding traffic to next pipe */
	if (next_pipe == NULL){
		fwd.type = DOCA_FLOW_FWD_DROP;
		*pipe = doca_flow_pipe_create(&pipe_cfg, &fwd, NULL, error);
	}
	else{
		fwd.type = DOCA_FLOW_FWD_PIPE;
		fwd.next_pipe = *next_pipe;

		fwd_miss.type = DOCA_FLOW_FWD_PIPE;
		fwd_miss.next_pipe = *next_pipe;
		*pipe = doca_flow_pipe_create(&pipe_cfg, &fwd, &fwd_miss, error);
	}
	
	
	if (*pipe == NULL)
		return -1;
	return 0;
}



/*
 * Add DOCA Flow pipe entry to the shared counter pipe
 *
 * @pipe [in]: pipe of the entry
 * @port [in]: port of the entry
 * @shared_counter_id [in]: ID of the shared counter
 * @error [out]: output error
 * @return: 0 on success, negative value otherwise and error is set.
 */
int
add_shared_counter_pipe_entry(struct doca_flow_pipe *pipe, struct doca_flow_port *port, uint32_t shared_counter_id, struct doca_flow_error *error)
{
	struct doca_flow_match match;
	struct doca_flow_actions actions;
	struct doca_flow_monitor monitor;
	struct doca_flow_pipe_entry *entry;
	int result;
	int num_of_entries = 1;

	int rule_cnt=0;

	/* example 5-tuple to match */
	// doca_be32_t dst_ip_addr = BE_IPV4_ADDR(8, 8, 8, 8);
	
	for(int i = 0; i<256; i++){
		for(int j = 0; j<256; j++){
			doca_be32_t dst_ip_addr = BE_IPV4_ADDR(0, 0, i, j);
			doca_be32_t src_ip_addr = BE_IPV4_ADDR(100, 100, 100, 100);
			doca_be16_t dst_port = rte_cpu_to_be_16(1234);
			doca_be16_t src_port = rte_cpu_to_be_16(1234);

			memset(&match, 0, sizeof(match));
			memset(&actions, 0, sizeof(actions));
			memset(&monitor, 0, sizeof(monitor));

			/* set shared counter ID */
			if(shared_counter_id == -1){
				;
			}
			else{
				monitor.shared_counter_id = shared_counter_id;
			}


			match.out_dst_ip.ipv4_addr = dst_ip_addr;
			match.out_src_ip.ipv4_addr = src_ip_addr;
			match.out_dst_port = dst_port;
			match.out_src_port = src_port;

			entry = doca_flow_pipe_add_entry(0, pipe, &match, &actions, &monitor, NULL, 0, NULL, error);
			if (entry == NULL) {
				DOCA_LOG_ERR("Failed to add entry - %s (%u)", error->message, error->type);
				return -1;
			}

			result = doca_flow_entries_process(port, 0, DEFAULT_TIMEOUT_US, num_of_entries);
			if (result != num_of_entries || doca_flow_pipe_entry_get_status(entry) != DOCA_FLOW_ENTRY_STATUS_SUCCESS){
				return -1;
			}
			rule_cnt += result;				
		}
	}
	
	DOCA_LOG_INFO("Total %d rules added",rule_cnt);
	return 0;
}

/*
 * Add DOCA Flow control pipe
 *
 * @port [in]: port of the pipe
 * @pipe [out]: created pipe pointer
 * @error [out]: output error
 * @return: 0 on success, negative value otherwise and error is set.
 */
int
create_control_pipe(struct doca_flow_port *port, struct doca_flow_pipe **pipe, struct doca_flow_error *error)
{
	struct doca_flow_pipe_cfg pipe_cfg;

	memset(&pipe_cfg, 0, sizeof(pipe_cfg));

	pipe_cfg.attr.name = "CONTROL_PIPE";
	pipe_cfg.attr.type = DOCA_FLOW_PIPE_CONTROL;
	pipe_cfg.attr.is_root = true;
	pipe_cfg.port = port;

	*pipe = doca_flow_pipe_create(&pipe_cfg, NULL, NULL, error);
	if (*pipe == NULL)
		return -1;
	return 0;
}

/*
 * Add DOCA Flow pipe entries to the control pipe. First entry forwards UDP packets to udp_pipe and the second
 * forwards TCP packets to tcp_pipe
 *
 * @control_pipe [in]: pipe of the entries
 * @tcp_pipe [in]: pointer to the TCP pipe to forward packets to
 * @udp_pipe [in]: pointer to the UDP pipe to forward packets to
 * @error [out]: output error
 * @return: 0 on success and negative value otherwise and error is set.
 */
int
add_control_pipe_entries(struct doca_flow_pipe *control_pipe, struct doca_flow_pipe *tcp_pipe,
			 struct doca_flow_pipe *udp_pipe, struct doca_flow_error *error)
{
	struct doca_flow_match match;
	struct doca_flow_fwd fwd;
	struct doca_flow_pipe_entry *entry;
	uint8_t priority = 0;

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	match.out_dst_ip.type = DOCA_FLOW_IP4_ADDR;
	match.out_l4_type = DOCA_PROTO_UDP;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = udp_pipe;

	entry = doca_flow_pipe_control_add_entry(0, priority, control_pipe, &match,
						 NULL, NULL, NULL, NULL, &fwd, error);
	if (entry == NULL) {
		DOCA_LOG_ERR("Failed to add control pipe entry - %s (%u)", error->message, error->type);
		return -1;
	}

	memset(&match, 0, sizeof(match));
	memset(&fwd, 0, sizeof(fwd));

	match.out_dst_ip.type = DOCA_FLOW_IP4_ADDR;
	match.out_l4_type = DOCA_PROTO_TCP;

	fwd.type = DOCA_FLOW_FWD_PIPE;
	fwd.next_pipe = tcp_pipe;

	entry = doca_flow_pipe_control_add_entry(0, priority, control_pipe, &match,
						 NULL, NULL, NULL, NULL, &fwd, error);
	if (entry == NULL) {
		DOCA_LOG_ERR("Failed to add control pipe entry - %s (%u)", error->message, error->type);
		return -1;
	}
	return 0;
}

/*
 * Run flow_shared_counter sample
 *
 * @nb_queues [in]: number of queues the sample will use
 * @return: 0 on success and negative value otherwise.
 */
int
flow_shared_counter(int nb_queues)
{
	int nb_ports = 1;
	struct doca_flow_resources resource = {0};
	uint32_t nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_MAX] = {0};
	struct doca_flow_port *ports[nb_ports];
	struct doca_flow_pipe *tcp_pipe, *udp_pipe, *pipe;
	struct doca_flow_pipe *age_pipe, *rss_pipe;
	struct doca_flow_error error;
	int port_id;
	uint32_t shared_counter_ids[] = {0};
	int result;

	struct doca_flow_aged_query *aged_entries;
	struct sample_user_data *user_data[nb_ports];
	int num_of_aging_entries = AGE_ENTRIES;

	struct doca_flow_query query_stats;
	int flow_cnt = 0;

	resource.nb_counters = 4*AGE_ENTRIES;

	nr_shared_resources[DOCA_FLOW_SHARED_RESOURCE_COUNT] = 2;

	if (init_doca_flow(nb_queues, "vnf,hws", resource, nr_shared_resources, &error) < 0) {
		DOCA_LOG_ERR("Failed to init DOCA Flow - %s (%u)", error.message, error.type);
		return -1;
	}

	if (init_doca_flow_ports(nb_ports, ports, true)) {
		DOCA_LOG_ERR("Failed to init DOCA ports");
		doca_flow_destroy();
		return -1;
	}

	for (port_id = 0; port_id < nb_ports; port_id++) {

		result = create_rss_meta_pipe(ports[port_id], &rss_pipe, &error);
		if (result < 0) {
			DOCA_LOG_ERR("Failed to create pipe - %s (%u)", error.message, error.type);
			destroy_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return -1;
		}
		DOCA_LOG_INFO("Rss pipe created");

		result = add_rss_meta_pipe_entry(rss_pipe, ports[port_id], &error);
		if (result < 0) {
			DOCA_LOG_ERR("Failed to add entry - %s (%u)", error.message, error.type);
			destroy_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return -1;
		}

		/* Create aging pipe */
		result = create_aging_pipe(ports[port_id], port_id, &age_pipe, &rss_pipe, &error);
		if (result < 0) {
			DOCA_LOG_ERR("Failed to create pipe - %s (%u)", error.message, error.type);
			destroy_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return -1;
		}
		DOCA_LOG_INFO("Aging pipe created");

		user_data[port_id] = (struct sample_user_data *)malloc(
			num_of_aging_entries * sizeof(struct sample_user_data));
		if (user_data[port_id] == NULL) {
			DOCA_LOG_ERR("failed to allocate user data");
			destroy_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return -1;
		}

		result = add_aging_pipe_entries(age_pipe, user_data[port_id], ports[port_id], port_id, num_of_aging_entries);
		if (result < 0) {
			destroy_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return -1;
		}
		DOCA_LOG_INFO("Aging pipe configured");


		/* bind shared counter to port */
		result = doca_flow_shared_resources_bind(DOCA_FLOW_SHARED_RESOURCE_COUNT, &shared_counter_ids[port_id], 1, ports[port_id], &error);
		if (result != 0) {
			DOCA_LOG_ERR("Failed to bind shared counter to pipe - %s (%u)", error.message, error.type);
			destroy_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return -1;
		}

		/* shared counter pipes */
		result = create_shared_counter_pipe(ports[port_id], port_id, DOCA_PROTO_TCP, &tcp_pipe, NULL, &error);
		if (result < 0) {
			DOCA_LOG_ERR("Failed to create pipe - %s (%u)", error.message, error.type);
			destroy_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return -1;
		}
		DOCA_LOG_INFO("TCP pipe created");

		result = add_shared_counter_pipe_entry(tcp_pipe, ports[port_id], shared_counter_ids[port_id], &error);
		if (result < 0) {
			DOCA_LOG_ERR("Failed to add entry - %s (%u)", error.message, error.type);
			destroy_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return -1;
		}

		result = create_shared_counter_pipe(ports[port_id], port_id, DOCA_PROTO_UDP, &udp_pipe, &age_pipe, &error);
		if (result < 0) {
			DOCA_LOG_ERR("Failed to create pipe - %s (%u)", error.message, error.type);
			destroy_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return -1;
		}
		DOCA_LOG_INFO("UDP pipe created");

		result = add_shared_counter_pipe_entry(udp_pipe, ports[port_id], shared_counter_ids[port_id], &error);
		if (result < 0) {
			DOCA_LOG_ERR("Failed to add entry - %s (%u)", error.message, error.type);
			destroy_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return -1;
		}

		/* Create control pipe */
		result = create_control_pipe(ports[port_id], &pipe, &error);
		if (result < 0) {
			DOCA_LOG_ERR("Failed to create control pipe - %s (%u)", error.message, error.type);
			destroy_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy();
			return -1;
		}
 
		result = add_control_pipe_entries(pipe, tcp_pipe, udp_pipe, &error);
		if (result < 0) {
			destroy_doca_flow_ports(nb_ports, ports);
			doca_flow_destroy() ;
			return -1;
		}
	}

	DOCA_LOG_INFO("Wait few seconds for packets to arrive");
	aged_entries = (struct doca_flow_aged_query *)malloc(num_of_aging_entries * nb_ports *
							     sizeof(struct sample_user_data));
	if (aged_entries == NULL) {
		DOCA_LOG_ERR("failed to allocate aged entries array");
		destroy_doca_flow_ports(nb_ports, ports);
		doca_flow_destroy();
		return -1;
	}

	// gettimeofday(&start, NULL);
	tot_pkts = 0;
	prev_tot_pkts = 0;
	tot_bytes = 0;
	cycles = 0;
	prev_cycles = 0;
	start = rte_rdtsc();

	
	while(1){
		
		for (port_id = 0; port_id < nb_ports; port_id++) {
			/* meta processing */
			process_packets(port_id);

			/* flow processing */
			flow_cnt = 0;
			for(int i=0; i<AGE_ENTRIES; i++){
				
				if(doca_flow_query(age_entry[i], &query_stats)){
					continue;
				}
				if(query_stats.total_pkts != 0){
					flow_cnt++;
				}
			}
			/* synthetic processing time */
			rte_delay_us_block(flow_cnt/10);
		}
	}

	destroy_doca_flow_ports(nb_ports, ports);
	doca_flow_destroy();
	return 0;
}
