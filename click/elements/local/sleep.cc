#include <click/config.h>
#include <click/glue.hh>
#include <click/args.hh>
#include <click/error.hh>
#include <click/straccum.hh>
#include <click/etheraddress.hh>
#include <click/standard/scheduleinfo.hh>
#ifdef CLICK_USERLEVEL
#include <math.h>
#include <time.h>
#endif
#include "sleep.hh"

CLICK_DECLS


Sleep::Sleep() : 
_stime(0),_ntime(0)
{
}

Sleep::~Sleep()
{
}


/* signal */

// static void
// signal_handler(int signum)
// {
// 	if (signum == SIGINT || signum == SIGTERM) {
// 		printf("Signal %d received, preparing to exit...", signum);
// 	}
// }


int 
Sleep::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read("STIME", _stime)
        .read("NTIME", _ntime)
        .complete() < 0)
        return -1;

    return 0;
}

int
Sleep::initialize(ErrorHandler *)
{
    return 0;
}

// bool
// Sleep::run_task(Task *t)
// {
//     // receive packets from input port 0
//     Packet *p = input(0).push();
//     if (p) {
//         // store the packet in the buffer
//         _buffer[_count++] = p;
//         // if the buffer is full, process the batch
//         if (_count == BATCH_SIZE_TEST_MODULE) {
//             for (int i = 0; i < BATCH_SIZE_TEST_MODULE; i++) {
//                 // do some operations on each packet
//                 // for example, add a timestamp annotation
//                 //_buffer[i]->timestamp_anno().assign_now();
// 				printf("Test batch\n");
//                 // output the packet to output port 0
//                 output(0).push(_buffer[i]);
//             }
//             // reset the buffer count
//             _count = 0;
//         }
//         // reschedule the task
//         t->fast_reschedule();
//         return true;
//     } else {
//         // no packet available, stop the task
// 		t->fast_reschedule();
//         return false;
//     }
// }


void
Sleep::push(int port, Packet *p)
{
    
    struct timespec spec;
    struct timespec rem;
    spec.tv_nsec = _ntime;
    spec.tv_sec = _stime;
    nanosleep(&spec,&rem);

    output(0).push(p);
    
}

CLICK_ENDDECLS
EXPORT_ELEMENT(Sleep)