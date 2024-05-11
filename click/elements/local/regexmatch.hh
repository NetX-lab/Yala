/*
 * regexmatch.{cc,hh} -- element uses hardware regex accelerator to conduct pattern matching 
 * Shaofeng Wu
 */
#ifndef CLICK_REGEXMATCH_HH
#define CLICK_REGEXMATCH_HH
#include <click/element.hh>
#include <click/notifier.hh>
#include <click/task.hh>
#include <click/dpdkdevice.hh>
#include <signal.h>
#include <rte_regexdev.h>
#include <click/dpdkbfregex_conf.h>


CLICK_DECLS

#define ERR_STR_SIZE	50
#define MAX_BATCH_SIZE 32
#define OUT_BUF_SIZE 256


class RegexMatch : public Element {
public:
    RegexMatch();
    ~RegexMatch();
    const char *class_name() const { return "RegexMatch"; }
    const char *port_count() const { return PORTS_1_1; }
    const char *processing() const { return "h/h"; }
    int configure(Vector<String> &conf, ErrorHandler *errh);
    int initialize(ErrorHandler *);
    // void push(int port, Packet *p);
    Packet *simple_action(Packet *);
private:
    struct rte_mbuf* _buf[MAX_BATCH_SIZE];
    struct rte_mbuf* _out_buf[OUT_BUF_SIZE];
    int _count;
    int _batch_size;
    bool _lat_mode;
    bool _dummy;
    bool debug;
    rb_conf _run_conf;
};

CLICK_ENDDECLS
#endif
