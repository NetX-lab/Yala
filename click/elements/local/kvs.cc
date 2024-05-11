// /*
//  * kvs.{cc,hh} -- element keeps a small in-memory key-value storage database
//  * Shaofeng Wu
// */

// #ifndef CLICK_KVS_HH
// #define CLICK_KVS_HH
// #include <click/element.hh>
// #include <click/notifier.hh>
// #include <click/task.hh>
// #include <click/dpdkdevice.hh>
// #include <signal.h>
// #include <rte_regexdev.h>
// #include <click/dpdkbfregex_conf.h>

// #include <click/config.h>
// #include <click/glue.hh>
// #include <click/args.hh>
// #include <click/error.hh>
// #include <click/straccum.hh>
// #include <click/etheraddress.hh>
// #include <click/standard/scheduleinfo.hh>
// #ifdef CLICK_USERLEVEL
// #include <math.h>
// #endif
// #include "kvs.hh"

// CLICK_DECLS


// // KVS::KVS()
// // : _task(this), _count(0)
// // {
// // }

// KVS::KVS() : 
//     debug(0)
// {
//     _db_size = BATCH_SIZE_TEST_MODULE;
// }

// KVS::~KVS()
// {
// }





// /* Return the rte_mbuf pointer for a packet. If the buffer of the packet is
//  * from a DPDK pool, it will return the underlying rte_mbuf and remove the
//  * destructor. If it's a Click buffer, it will allocate a DPDK mbuf and copy
//  * the packet content to it if create is true. */
// inline struct rte_mbuf* get_mbuf(Packet* p, bool create=true) {
//     struct rte_mbuf* mbuf = 0;

//     if (likely(DPDKDevice::is_dpdk_packet(p))) {
//         mbuf = (struct rte_mbuf *) p->destructor_argument();

//         /* Here we need the address of the packet of a mbuf, we use dynfield1[8] to store this information */
//         Packet **dynfield_pointer = ((Packet **)(&mbuf->dynfield1[DYNFIELD_INDEX_PACKET]));
//         *dynfield_pointer = p;


//         //printf("IS dpdk packet\n");
//         // rte_pktmbuf_pkt_len(mbuf) = p->length();
//         // rte_pktmbuf_data_len(mbuf) = p->length();
//         // mbuf->data_off = p->headroom();
//         // if (p->shared()) {
//         //     /*Prevent DPDK from freeing the buffer. When all shared packet
//         //      * are freed, DPDKDevice::free_pkt will effectively destroy it.*/
//         //     rte_mbuf_refcnt_update(mbuf, 1);
//         // } else {
//         //     //Reset buffer, let DPDK free the buffer when it wants
//         //     p->reset_buffer();
//         // }
//     }
//     else{
//         //printf("NOT dpdk packet\n");
//         ;
//     } 
//     // else if (create) {
//     //     mbuf = rte_pktmbuf_alloc(DPDKDevice::get_mpool(rte_socket_id()));
//     //     memcpy((void*) rte_pktmbuf_mtod(mbuf, unsigned char *), p->data(),
//     //            p->length());
//     //     rte_pktmbuf_pkt_len(mbuf) = p->length();
//     //     rte_pktmbuf_data_len(mbuf) = p->length();
//     // }

//     return mbuf;
// }


// inline Packet* get_pkt(struct rte_mbuf* mbuf) {
//     Packet* p = NULL;

//     p = *((Packet**)(&mbuf->dynfield1[DYNFIELD_INDEX_PACKET]));

//     return p;
// }

// int 
// KVS::configure(Vector<String> &conf, ErrorHandler *errh)
// {
//     if (Args(conf, this, errh)
//         .read("SIZE", _db_size)
//         .read("DEBUG", _debug)
//         .complete() < 0)
//         return -1;
//     if(_db_size <=0){
//         return errh->error("Invalid db size %d", _db_size);
//     }
//     else{
//         if(debug){
//             click_chatter("Db size of KVS module is %d",_db_size);
//         }
//     }
    

//     return 0;
// }

// int
// KVS::initialize(ErrorHandler *)
// {   
//     for(int i=0; e = (struct entry *)malloc(sizeof(struct entry));
// ; )
//     pthread_mutex_init(&_lock, NULL);
//     return 0;

// }

// // bool
// // KVS::run_task(Task *t)
// // {
// // }


// void
// KVS::push(int port, Packet *p)
// {   
//     int id;

//     HASH_FIND_INT(mystate->mydb, &id, e);
//     output(0).push(p);
            
// }

// CLICK_ENDDECLS
// EXPORT_ELEMENT(KVS)