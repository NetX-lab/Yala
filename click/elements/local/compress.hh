/*
 * compress.{cc,hh} -- element uses hardware compression accelerator to conduct data compression 
 * Tomur authors
 */
#ifndef CLICK_COMPRESS_HH
#define CLICK_COMPRESS_HH
#include <click/element.hh>
#include <click/notifier.hh>
#include <click/task.hh>
#include <click/dpdkdevice.hh>
#include <signal.h>
#include <rte_compressdev.h>
#include <click/dpdkbfregex_conf.h>


CLICK_DECLS

#define COMPRESS_ERR_STR_SIZE	50
#define MAX_BATCH_SIZE 32
#define OUT_BUF_SIZE_COMPRESS 256


class Compress : public Element {
public:
    Compress();
    ~Compress();
    const char *class_name() const { return "Compress"; }
    const char *port_count() const { return PORTS_1_1; }
    const char *processing() const { return "h/h"; }
    int configure(Vector<String> &conf, ErrorHandler *errh);
    int initialize(ErrorHandler *);
    Packet *simple_action(Packet *p);
private:
    // struct compress_bf_conf _run_conf;

    struct rte_mbuf* _buf[MAX_BATCH_SIZE];
    struct rte_mbuf* _out_buf[OUT_BUF_SIZE_COMPRESS];
    int _count;
    int _batch_size;
    bool _lat_mode;
    bool _dummy;
    bool debug;
    rb_conf _run_conf;
};

CLICK_ENDDECLS
#endif
