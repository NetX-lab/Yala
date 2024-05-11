#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>
#include <click/etheraddress.hh>
#include <click/standard/scheduleinfo.hh>
#ifdef CLICK_USERLEVEL
#include <math.h>
#endif
#include "compress.hh"
#include <click/dpdkbfregex_conf.h>
#include <click/dpdkbfregex_rxpb_log.h>
#include <click/dpdkbfregex_utils.h>
#include <click/dpdkbfregex_stats.h>
#include <click/dpdkbf_acc_dev.h>
#include <click/dpdkbfregex_input.h>
#include <click/dpdkbfregex_run_mode.h>
#include <rte_malloc.h>

CLICK_DECLS


Compress::Compress() : 
    _count(0), _run_conf({0}),
    _lat_mode(0),_dummy(0),debug(0)
{
    _batch_size = MAX_BATCH_SIZE;
}

Compress::~Compress()
{
}



#define DYNFIELD_INDEX_PACKET 6


/* signal */

// static void
// signal_handler(int signum)
// {
// 	if (signum == SIGINT || signum == SIGTERM) {
// 		printf("Signal %d received, preparing to exit...", signum);
// 	}
// }

/* Return the rte_mbuf pointer for a packet. If the buffer of the packet is
 * from a DPDK pool, it will return the underlying rte_mbuf and remove the
 * destructor. If it's a Click buffer, it will allocate a DPDK mbuf and copy
 * the packet content to it if create is true. */
inline struct rte_mbuf* get_mbuf(Packet* p) {
    struct rte_mbuf* mbuf = 0;

    if (likely(DPDKDevice::is_dpdk_packet(p))) {
        mbuf = (struct rte_mbuf *) p->destructor_argument();

        /* Here we need the address of the packet of a mbuf, we use dynfield1[8] to store this information */
        Packet **dynfield_pointer = ((Packet **)(&mbuf->dynfield1[DYNFIELD_INDEX_PACKET]));
        *dynfield_pointer = p;


        //printf("IS dpdk packet\n");
        // rte_pktmbuf_pkt_len(mbuf) = p->length();
        // rte_pktmbuf_data_len(mbuf) = p->length();
        // mbuf->data_off = p->headroom();
        // if (p->shared()) {
        //     /*Prevent DPDK from freeing the buffer. When all shared packet
        //      * are freed, DPDKDevice::free_pkt will effectively destroy it.*/
        //     rte_mbuf_refcnt_update(mbuf, 1);
        // } else {
        //     //Reset buffer, let DPDK free the buffer when it wants
        //     p->reset_buffer();
        // }
    }
    else{
        return NULL;
    }

    return mbuf;
}


inline Packet* get_pkt(struct rte_mbuf* mbuf) {
    Packet* p = NULL;

    p = *((Packet**)(&mbuf->dynfield1[DYNFIELD_INDEX_PACKET]));

    return p;
}

int 
Compress::configure(Vector<String> &conf, ErrorHandler *errh)
{   
    if (Args(conf, this, errh)
        .read("BATCH", _batch_size)
        .read("LATMODE", _lat_mode)
        .read("DUMMY", _dummy)
        .read("DEBUG", debug)
        .complete() < 0)
        return -1;
    if(_batch_size <=0){
        return errh->error("Invalid batch size %d", _batch_size);
    }
    else{
        _run_conf.input_batches = _batch_size;
        _run_conf.latency_mode = _lat_mode;
        if(_dummy){
            _run_conf.latency_mode = -1;    
        }
        if(debug){
            click_chatter("Batch size of DPDK Compress module is %d",_run_conf.input_batches);
            click_chatter("Latency mode of DPDK Compress module is %d",_run_conf.latency_mode);
        }
    }
    

    return 0;
}

// int
// Compress::initialize(ErrorHandler *)
// {
//     /* initialize fields */
// 	int nb_compressdevs = 0;
//     int ret;

//     struct compress_bf_conf *_p_conf = &_run_conf;

//     /* set default configuration */
// 	dpdk_bf_comp_set_def_conf(_p_conf);

//     /* check capabilities of conpress device, configure device, set up queue pairs and start the device */
// 	nb_compressdevs = dpdk_bf_comp_init_compressdev(_p_conf);

// 	if (nb_compressdevs < 1) {
// 		printf("No available compression device\n");
// 		return -EINVAL;
// 	}
	
//     // // create xform
//     // ret = dpdk_bf_comp_create_xform(_p_conf);
//     // if(ret){
//     //     return ret;
//     // }
    

// 	/* allocate space for compression operations, including ops to be enqueued and ops dequeued */
// 	_p_conf->ops = (struct rte_comp_op **)rte_zmalloc_socket(NULL,
// 					sizeof(struct rte_comp_op *) * _p_conf->batch_size, 0, rte_socket_id());
// 	_p_conf->deq_ops = (struct rte_comp_op **)rte_zmalloc_socket(NULL,
// 				sizeof(struct rte_comp_op *) * _p_conf->batch_size, 0, rte_socket_id());
	

// 	for(int i=0; i<_p_conf->batch_size ; i++){
// 		_p_conf->ops[i] = (struct rte_comp_op *)rte_malloc(NULL, sizeof(struct rte_comp_op), 0);
// 		if (!_p_conf->ops[i]){
// 			printf("Allocation of input compression operations failed\n");
// 			return -ENOMEM;
// 		}	
// 	}


// 	char pool_name[128];
// 	struct rte_mempool *mbuf_pool = NULL;
// 	/* allocate space for destination bufs for storing compressed/decompressed result */
// 	snprintf(pool_name, sizeof(pool_name), "comp_buf_pool_%u_qp_%u",
// 			0, 0);

// 	mbuf_pool = rte_pktmbuf_pool_create(pool_name,
// 				2047,
// 				256, 0,
// 				2048,
// 				rte_socket_id());
	
//     /* allocate space for dequeued bufs(maybe compressed/decompressed result) */
// 	for(int i=0; i<DPDK_COMP_OUTPUT_BUFS_MAX ; i++){
// 		_p_conf->output_bufs[i] = rte_pktmbuf_alloc(mbuf_pool);
// 		if (!_p_conf->output_bufs[i]){
// 			printf("Allocation of output compressed/decompressed bufs failed\n");
// 			return -ENOMEM;
// 		}	
//         /* set buf length */
// 		_p_conf->output_bufs[i]->data_len = _p_conf->out_seg_sz;
// 		_p_conf->output_bufs[i]->pkt_len = _p_conf->out_seg_sz;
// 	}

//     ret = dpdk_bf_comp_register(_p_conf);
//     if(ret){
//         return -EINVAL;   
//     }

//     /* a debug field */
// 	_p_conf->wait_on_dequeue = 0;
//     return 0;
// }

int
Compress::initialize(ErrorHandler *)
{
	char err[COMPRESS_ERR_STR_SIZE] = {0};
	rb_stats_t *stats;
	int ret;


    // printf("regex device count=%d\n",rte_regexdev_count());

    // signal(SIGINT, signal_handler);
	// signal(SIGTERM, signal_handler);
    char str0[] = "nothing";
    char str1[] = "-D";
    /* dpdk eal has already been initialized by click, this option is useless */
    char str2[] = "-l0 -n 1 -a 0000:03:00.0,class=net:regex --file-prefix dpdk0";

    char str3[] = "--input-mode";
    char str4[] = "dpdk_port";
    char str5[] = "--dpdk-primary-port";
    char str6[] = "0000:03:00.0";
    char str7[] = "-d";
    char str8[] = "comp_dpdk";
    /* unused */
    char str9[] = "-r";
    /* unused */
    char str10[] = "../rulesets/l7_filter/build/l7_filter.rof2.binary";
    char str11[] = "-c";
    char str12[] = "1";
    char str13[] = "-s";
    char str14[] = "180";
    int argc = 15;
    char *argv[15] = {
        str0,str1,str2,str3,str4,str5,str6,str7,str8,str9,str10,str11,str12,str13,str14
    };

    // printf("argc=%d\n",argc);
	// for(int i=0;i<argc;i++){
	// 	printf("%s\n",argv[i]);
	// }
    

    /* only populate field2 of run_conf */
	ret = conf_setup(&_run_conf, argc, argv);
	if (ret)
		rte_exit(EXIT_FAILURE, "Configuration error\n");


	/* Confirm there are enough DPDK lcores for user core request. */
	if (_run_conf.cores > rte_lcore_count()) {
		RXPB_LOG_WARN_REC(&_run_conf, "requested cores (%d) > dpdk lcores (%d) - using %d.", _run_conf.cores,
				  rte_lcore_count(), rte_lcore_count());
		_run_conf.cores = rte_lcore_count();
	}

    //printf("stats_init()...\n");
	ret = stats_init(&_run_conf);
	if (ret) {
		snprintf(err, COMPRESS_ERR_STR_SIZE, "Failed initialising stats");
		goto clean_conf;
	}
    // printf("stats_init() finished\n");
    // printf("input_register()...\n");
	ret = input_register(&_run_conf);
	if (ret) {
		snprintf(err, COMPRESS_ERR_STR_SIZE, "Input registration error");
		goto clean_stats;
	}
    // printf("input_register() finished\n");


    // printf("acc_dev_register()...\n");
	ret = acc_dev_register(&_run_conf);
	if (ret) {
		snprintf(err, COMPRESS_ERR_STR_SIZE, "Regex dev registration error");
		goto clean_input;
	}
    // printf("acc_dev_register() finished\n");


    // printf("regex_dev_compile_rules()...\n");
	// ret = regex_dev_compile_rules(&_run_conf);
	// if (ret) {
	// 	snprintf(err, COMPRESS_ERR_STR_SIZE, "Regex dev rule compilation error");
	// 	goto clean_input;
	// }
    // printf("regex_dev_compile_rules() finished\n");

    // printf("acc_dev_init()...\n");
	ret = acc_dev_init(&_run_conf);
	if (ret) {
		snprintf(err, COMPRESS_ERR_STR_SIZE, "Failed initialising compress device");
		goto clean_input;
	}
    // printf("acc_dev_init() finished\n");

    // printf("run_mode_register()...\n");
	ret = run_mode_register(&_run_conf);
	if (ret) {
		snprintf(err, COMPRESS_ERR_STR_SIZE, "Run mode registration error");
		goto clean_regex;
	}
    // printf("run_mode_register() finished\n");

	// /* Main core gets regex queue 0 and stats position 0. */
	stats = _run_conf.stats;
	stats->rm_stats[0].lcore_id = rte_get_main_lcore();

	// //worker_qid = 1;

	// printf("Beginning Processing...");
	_run_conf.running = true;
	// start_cycles = rte_get_timer_cycles();


	// printf("initialization finished\n");	


    return 0;

    clean_regex:
	    regex_dev_clean_regex(&_run_conf);
    clean_input:
        input_clean(&_run_conf);
    clean_stats:
        stats_clean(&_run_conf);
    clean_conf:
        conf_clean(&_run_conf);

	if (ret){
        rte_exit(EXIT_FAILURE, "%s\n", err);
    }
    // else
	// 	rte_eal_cleanup();
}



// void
// Compress::push(int port, Packet *p)
// {
//     int nb_dequeued_op = 0;
//     Packet *p_out;
//     // store the packet in the buffer
//     _buf[_count] = get_mbuf(p);
//     _count++;
//     // if the buffer is full, process the batch
//     if (_count == _run_conf.batch_size) {
//         //printf("Entering batch processing function...\n");
//         // push one batch and pull batches out 
//         // _run_conf.run_funcs->run(&_run_conf, 0, _buf, &nb_dequeued_op, _out_buf);
//         dpdk_bf_comp_run(&_run_conf, _buf, _count, _out_buf, &nb_dequeued_op);
//         // printf("[main module]dequeued_op: %d\n",nb_dequeued_op);
//         // printf("Process complete, pushing packets to next element\n");
//         for (int i = 0; i < nb_dequeued_op; i++) {
//             // output the packet to output port 0
//             p_out = get_pkt(_out_buf[i]);
//             output(0).push(p_out);
//             //rte_pktmbuf_free(_out_buf[i]);
//         }
//         // all enqueued, reset the input buffer count
//         _count = 0;
//         //printf("batch complete all processing\n");
//     }
// }

// Packet *
// Compress::simple_action(Packet *p){
//     int nb_dequeued_op = 0;
//     int batch_cnt=0;
//     int push_batch = 0;
//     Packet *p_out;
//     // Store the packet in the buffer
//     _buf[_count] = get_mbuf(p);
//     _count++;

//     // If the buffer is full, process the batch
//     if (_count == _run_conf.batch_size) {
//         // Push one batch and pull one batch
//         // The processing could be both pipelining and run to completion
        
//         _run_conf.run(&_run_conf, _buf, _count, _out_buf, &nb_dequeued_op, 1);
//         // All enqueued, reset the input buffer count
//         _count = 0;
//     }else{
//         // When batch is not ready, pull one batch (pipeline mode) 
//         // For run-to-completion mode, no need to pull since all batches have been pulled
        
//         _run_conf.run(&_run_conf, _buf, _count, _out_buf, &nb_dequeued_op, 0);
//     }

//     return p;     
// }

Packet *
Compress::simple_action(Packet *p){
    int nb_dequeued_op = 0;
    int batch_cnt=0;
    Packet *p_out;
    // Store the packet in the buffer
    _buf[_count] = get_mbuf(p);
    _count++;

    // If the buffer is full, process the batch
    if (_count == _run_conf.input_batches) {
        // Push one batch and pull one batch
        // The processing could be both pipelining and run to completion
        _run_conf.run_funcs->run(&_run_conf, 0, _buf, &nb_dequeued_op, _out_buf, 1);
        // All enqueued, reset the input buffer count
        _count = 0;
        // if(_lat_mode){
        //     printf("%d\n",nb_dequeued_op);
        //     for(int i=0; i<32; i++){
        //         printf("%d/%d\n",i,nb_dequeued_op);
        //         // p_out = get_pkt(_out_buf[i]);
        //         // output(0).push(p_out);
                
        //     }   
        //     sleep(1);   
        // }
    }else if(!_lat_mode){
        // When batch is not ready, pull one batch (pipeline mode) 
        _run_conf.run_funcs->run(&_run_conf, 0, _buf, &nb_dequeued_op, _out_buf, 0);
    }
    else if(_lat_mode){
        ;// For run-to-completion mode, no need to pull since all batches have been pulled
    }  
    return p;    
}

CLICK_ENDDECLS
EXPORT_ELEMENT(Compress)
