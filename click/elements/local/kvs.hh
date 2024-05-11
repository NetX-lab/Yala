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
// #include <click/uthash.h>


// CLICK_DECLS

// #define ERR_STR_SIZE	50

// #define WORD_LEN 8

// struct entry {
//     int id;
//     char value[WORD_LEN];
//     UT_hash_handle hh;
// } __attribute__((packed));


// class KVS : public Element {
// public:
//     KVS();
//     ~KVS();
//     const char *class_name() const { return "KVS"; }
//     const char *port_count() const { return PORTS_1_1; }
//     const char *processing() const { return "h/h"; }
//     int configure(Vector<String> &conf, ErrorHandler *errh);
//     int initialize(ErrorHandler *);
//     //bool run_task(Task *);
//     void push(int port, Packet *p);
// private:
//     //Task _task;
//     struct entry *_db;
//     pthread_mutex_t _lock;
//     struct entry _result;
//     int _db_size;
//     bool _debug;
// };

// CLICK_ENDDECLS
// #endif