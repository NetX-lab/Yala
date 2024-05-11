/*
 * sleep.{cc,hh} -- 
 * Tomur authors
 */
#ifndef CLICK_SLEEP_HH
#define CLICK_SLEEP_HH
#include <click/element.hh>
#include <click/notifier.hh>
#include <click/task.hh>
#include <click/dpdkdevice.hh>
#include <signal.h>


CLICK_DECLS

// #define DEFAULT_ARRAY_SIZE	50
// #define BATCH_SIZE_TEST_MODULE 32
// #define OUT_BUF_SIZE 256


class Sleep : public Element {
public:
    Sleep();
    ~Sleep();
    const char *class_name() const { return "Sleep"; }
    const char *port_count() const { return PORTS_1_1; }
    const char *processing() const { return "h/h"; }
    int configure(Vector<String> &conf, ErrorHandler *errh);
    int initialize(ErrorHandler *);
    //bool run_task(Task *);
    void push(int port, Packet *p);
private:
    //Task _task;
    int _stime;
    long _ntime;
};

CLICK_ENDDECLS
#endif
