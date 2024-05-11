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
#include "ddos.hh"
#include <click/dpdkbfcomp.h>


CLICK_DECLS


// DDoS::DDoS()
// : _task(this), _count(0)
// {
// }

DDoS::DDoS() : 
    _p_set(NULL), _p_tot(NULL), _p_entropy(NULL),
    _head(0), _packet_count(0), _ddos_signal(0),
    debug(0)
{  
    _p_window = DDOS_DEFAULT_WINDOW;
    _threshold = DDOS_DEFAULT_THRESH;
}

DDoS::~DDoS()
{
}



int 
DDoS::configure(Vector<String> &conf, ErrorHandler *errh)
{   
    // TODOs: add more config options for this module
    if (Args(conf, this, errh)
        .read("WINDOW", _p_window)
        .read("THRES", _threshold)
        .complete() < 0)
        return -1;
    if(_p_window <=0){
        return errh->error("Invalid window size %d", _p_window);
    }
    if(_threshold <=0){
        return errh->error("Invalid threshold value %d", _threshold);
    } 
    
    return 0;
}

int
DDoS::initialize(ErrorHandler *)
{

    _p_set = (uint32_t *)calloc(_p_window, sizeof(uint32_t));
    _p_tot = (uint32_t *)calloc(_p_window, sizeof(uint32_t));
    _p_entropy = (uint32_t *)calloc(_p_window, sizeof(uint32_t));
    _head = 0;
    _packet_count = 0;
    _ddos_signal = 0;

    return 0;
}

// bool
// DDoS::run_task(Task *t)
// {
// }

static uint32_t 
count_bits_64(uint8_t *packet, 
              uint32_t p_len)
{
    uint64_t v, set_bits = 0;
    uint64_t *ptr = (uint64_t *) packet;
    uint64_t *end = (uint64_t *) (packet + p_len);

    while(end > ptr){
        v = *ptr++;
        v = v - ((v >> 1) & 0x5555555555555555);
        v = (v & 0x3333333333333333) + ((v >> 2) & 0x3333333333333333);
        v = (v + (v >> 4)) & 0x0F0F0F0F0F0F0F0F;
        set_bits += (v * 0x0101010101010101) >> (sizeof(v) - 1) * CHAR_BIT;
    }
    return set_bits;
}

static uint32_t
simple_entropy(uint32_t set_bits, 
               uint32_t total_bits)
{
    uint32_t ret;

    ret = (-set_bits) * (log2(set_bits) - log2(total_bits)) -
          (total_bits - set_bits) * (log2(total_bits - set_bits) - 
          log2(total_bits)) + log2(total_bits);

    return ret;
}



/* we assume packets have been stripped beforehand */
void
DDoS::push(int port, Packet *p)
{
    char *payload = NULL;
    uint32_t p_len = 0;

    uint32_t bits;
    uint32_t set;
    uint32_t k, total_set = 0, total_bits = 0, sum_entropy = 0;


    payload = (char *)p->data();
    p_len = p->length();

    bits = p_len * 8;
    set = count_bits_64((uint8_t *)payload, p_len);

    _p_tot[_head] = bits;
    _p_set[_head] = set;
    _p_entropy[_head] = simple_entropy(set, bits);
    _packet_count++;

    if (_packet_count >= _p_window) {
        total_set = 0;
        total_bits = 0;
        sum_entropy = 0;

        for (k = 0; k < _p_window; k++) {
            total_set += _p_set[k];
            total_bits += _p_tot[k];
            sum_entropy += _p_entropy[k];
        }

        uint32_t joint_entropy = simple_entropy(total_set, total_bits);
        if (_threshold < (sum_entropy - joint_entropy)) {
            _ddos_signal++;
        }
    }
    _head = (_head + 1) % _p_window;
    
    // output the packet to output port 0
    output(0).push(p);
}

CLICK_ENDDECLS
EXPORT_ELEMENT(DDoS)