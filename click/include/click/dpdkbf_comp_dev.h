// #ifndef _DPDKBFCOMP_CONF_H
// #define _DPDKBFCOMP_CONF_H

// #include <stdint.h>
// #include <rte_mbuf.h>
// #include <rte_compressdev.h>

// // #include "dpdkcomp.h"
// #ifdef __cplusplus
// extern "C" {
// #endif

// // MACROS
// #define DPDK_COMP_OUTPUT_BUFS_MAX 1024
// #define DPDK_COMP_NUM_MAX_XFORMS 16
// #define DPDK_COMP_NUM_MAX_INFLIGHT_OPS 512
// #define DPDK_COMP_MAX_RANGE_LIST		32

// /* Cleanup state machine */
// enum cleanup_st {
// 	ST_CLEAR = 0,
// 	ST_TEST_DATA,
// 	ST_COMPDEV,
// 	ST_INPUT_DATA,
// 	ST_MEMORY_ALLOC,
// 	ST_DURING_TEST
// };

// enum comp_operation {
// 	COMPRESS_ONLY,
// 	DECOMPRESS_ONLY,
// 	COMPRESS_DECOMPRESS
// };

// struct range_list {
// 	uint8_t min;
// 	uint8_t max;
// 	uint8_t inc;
// 	uint8_t count;
// 	uint8_t list[DPDK_COMP_MAX_RANGE_LIST];
// };

// struct compress_bf_conf{
//     char driver_name[RTE_DEV_NAME_MAX_LEN];
//     int cdev_id;
//     unsigned int dev_qid;

//     /* enqueue batch size */
// 	int batch_size;

//     /* compression algorithm */
//     enum rte_comp_algorithm algo;

//     /* parameters for deflate and lz4 respectively */
//     union {
//         enum rte_comp_huffman huffman_enc;
//         uint8_t flags;
//     }algo_config;

//     /* compression operations buffer */
//     struct rte_comp_op **ops;
//     enum comp_operation test_op;
//     struct rte_comp_op **deq_ops;

//     /* ring buffer to store compressed result */
//     struct rte_mbuf *output_bufs[DPDK_COMP_OUTPUT_BUFS_MAX];
//     uint32_t buf_id; /* output buf index */
//     uint32_t out_seg_sz;

//     /* xform for compression */
//     void *priv_xform;
	
// 	uint16_t max_sgl_segs;
// 	uint32_t total_segs;

// 	int window_sz;
// 	struct range_list level_lst;
// 	uint8_t level;
// 	int use_external_mbufs;

// 	double ratio;
// 	enum cleanup_st cleanup;

// 	int wait_on_dequeue;

// 	bool latency_mode;

// 	int (*run)(struct compress_bf_conf *mystate, struct rte_mbuf **mbufs, int nb_enq,
//                             struct rte_mbuf **mbuf_out,
//                             int *nb_deq, int push_batch);
// };


// // functions
// // DPDK BF REGEX
// int dpdk_bf_comp_init_compressdev(struct compress_bf_conf *mystate);

// void dpdk_bf_comp_set_def_conf(struct compress_bf_conf *mystate);

// int dpdk_bf_comp_create_xform(struct compress_bf_conf *mystate);

// // int dpdk_bf_comp_run(struct compress_bf_conf *mystate, struct rte_mbuf **mbufs, int nb_enq,
// //                             struct rte_mbuf **mbuf_out,
// //                             int *nb_deq);

// int dpdk_bf_comp_register(struct compress_bf_conf *mystate);

// #ifdef __cplusplus
// }
// #endif

// #endif


#ifndef _INCLUDE_COMP_DEV_H_
#define _INCLUDE_COMP_DEV_H_


#define DPDK_COMP_OUTPUT_BUFS_MAX 1024
#define DPDK_COMP_NUM_MAX_XFORMS 16
#define DPDK_COMP_NUM_MAX_INFLIGHT_OPS 512
#define DPDK_COMP_MAX_RANGE_LIST		32

enum comp_operation {
	COMPRESS_ONLY,
	DECOMPRESS_ONLY,
	COMPRESS_DECOMPRESS
};

#endif /* _INCLUDE_COMP_DEV_H_ */
