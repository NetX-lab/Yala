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
#include "regexmatch.hh"
#include <click/dpdkbfregex_conf.h>
#include <click/dpdkbfregex_rxpb_log.h>
#include <click/dpdkbfregex_utils.h>
#include <click/dpdkbfregex_stats.h>
#include <click/dpdkbf_acc_dev.h>
#include <click/dpdkbfregex_input.h>
#include <click/dpdkbfregex_run_mode.h>
#include <rte_malloc.h>

CLICK_DECLS


RegexMatch::RegexMatch() : 
    _count(0), _run_conf({0}),
    _lat_mode(0),_dummy(0),debug(0)
{
    _batch_size = MAX_BATCH_SIZE;
}

RegexMatch::~RegexMatch()
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
RegexMatch::configure(Vector<String> &conf, ErrorHandler *errh)
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
            click_chatter("Batch size of DPDK Regex module is %d",_run_conf.input_batches);
            click_chatter("Latency mode of DPDK Regex module is %d",_run_conf.latency_mode);
        }
    }
    

    return 0;
}

int
RegexMatch::initialize(ErrorHandler *)
{
	char err[ERR_STR_SIZE] = {0};
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
    char str8[] = "rxp";
    char str9[] = "-r";
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
		snprintf(err, ERR_STR_SIZE, "Failed initialising stats");
		goto clean_conf;
	}
    // printf("stats_init() finished\n");
    // printf("input_register()...\n");
	ret = input_register(&_run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Input registration error");
		goto clean_stats;
	}
    // printf("input_register() finished\n");


    // printf("acc_dev_register()...\n");
	ret = acc_dev_register(&_run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Regex dev registration error");
		goto clean_input;
	}
    // printf("acc_dev_register() finished\n");


    // printf("regex_dev_compile_rules()...\n");
	ret = regex_dev_compile_rules(&_run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Regex dev rule compilation error");
		goto clean_input;
	}
    // printf("regex_dev_compile_rules() finished\n");

    // printf("acc_dev_init()...\n");
	ret = acc_dev_init(&_run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Failed initialising regex device");
		goto clean_input;
	}
    // printf("acc_dev_init() finished\n");

    // printf("run_mode_register()...\n");
	ret = run_mode_register(&_run_conf);
	if (ret) {
		snprintf(err, ERR_STR_SIZE, "Run mode registration error");
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
// RegexMatch::push(int port, Packet *p){
//     int nb_dequeued_op = 0;
//     int batch_cnt=0;
//     Packet *p_out;
//     // Store the packet in the buffer
//     _buf[_count] = get_mbuf(p);
//     _count++;

//     // If the buffer is full, process the batch
//     if (_count == _run_conf.input_batches) {
//         // Push one batch and pull one batch
//         // The processing could be both pipelining and run to completion
//         _run_conf.run_funcs->run(&_run_conf, 0, _buf, &nb_dequeued_op, _out_buf, 1);
//         // All enqueued, reset the input buffer count
//         _count = 0;
//         if(_lat_mode){
//             for(int i=0; i<nb_dequeued_op; i++){
//                 p_out = get_pkt(_out_buf[i]);
//                 output(0).push(p_out);   
//             }   
//         }
//     }else if(!_lat_mode){
//         // When batch is not ready, pull one batch (pipeline mode) 
//         _run_conf.run_funcs->run(&_run_conf, 0, _buf, &nb_dequeued_op, _out_buf, 0);
//         for(int i=0; i<nb_dequeued_op; i++){
//             p_out = get_pkt(_out_buf[i]);
//             output(0).push(p_out);   
//         }
//     }
//     else if(_lat_mode){
//         // For run-to-completion mode, no need to pull since all batches have been pulled
//         ;
//     }        
// }

Packet *
RegexMatch::simple_action(Packet *p){
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
EXPORT_ELEMENT(RegexMatch)
