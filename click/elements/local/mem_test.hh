/*
 * mem_test.{cc,hh} -- 
 * Tomur authors
 */
#ifndef CLICK_MEMTEST_HH
#define CLICK_MEMTEST_HH
#include <click/element.hh>
#include <click/notifier.hh>
#include <click/task.hh>
#include <click/dpdkdevice.hh>
#include <signal.h>


CLICK_DECLS

// #define DEFAULT_ARRAY_SIZE	50
// #define BATCH_SIZE_TEST_MODULE 32
// #define OUT_BUF_SIZE 256

/* test types */
#define TEST_DUMB_SEQ 0
#define TEST_DUMB_STEP 1
#define TEST_DUMB_RAND 2

#define TEST_READ_SEQ 3
#define TEST_READ_STEP 4
#define TEST_READ_RAND 5

#define TEST_WRITE_SEQ 6
#define TEST_WRITE_STEP 7
#define TEST_WRITE_RAND 8

#define TEST_MEMCPY 9
#define TEST_MCBLOCK 10


class MemTest : public Element {
public:
    MemTest();
    ~MemTest();
    const char *class_name() const { return "MemTest"; }
    const char *port_count() const { return PORTS_1_1; }
    const char *processing() const { return "h/h"; }
    int configure(Vector<String> &conf, ErrorHandler *errh);
    int initialize(ErrorHandler *);
    Packet *simple_action(Packet *p);
private:
    long _index;
    long _size;// in MBytes
    long _asize; // # of elements
    int _oppp;
    int _type;
    long *a_array;
    long *b_array;
};

CLICK_ENDDECLS
#endif
