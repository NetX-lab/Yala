/*
 * ddos.{cc,hh} -- element uses hardware compression accelerator to conduct data compression 
 * Shaofeng Wu
 */
#ifndef CLICK_DDOS_HH
#define CLICK_DDOS_HH
#include <click/element.hh>
#include <click/notifier.hh>
#include <click/task.hh>
#include <click/dpdkdevice.hh>
#include <signal.h>
#include <rte_compressdev.h>

#include <click/dpdkbfcomp.h>


CLICK_DECLS

#define DDOS_DEFAULT_WINDOW 0xFF
#define DDOS_DEFAULT_THRESH 1200


class DDoS : public Element {
public:
    DDoS();
    ~DDoS();
    const char *class_name() const { return "DDoS"; }
    const char *port_count() const { return PORTS_1_1; }
    const char *processing() const { return "h/h"; }
    int configure(Vector<String> &conf, ErrorHandler *errh);
    int initialize(ErrorHandler *);
    //bool run_task(Task *);
    void push(int port, Packet *p);
private:
    //Task _task;
    bool debug;
    uint32_t _p_window;
    uint32_t _threshold;

    uint32_t *_p_set;
    uint32_t *_p_tot;
    uint32_t *_p_entropy;
    uint32_t _head;
    uint32_t _packet_count;

    int _ddos_signal; /* true or detected attack */
};

CLICK_ENDDECLS
#endif
