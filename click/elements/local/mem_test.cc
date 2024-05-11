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
#include "mem_test.hh"

CLICK_DECLS


/* random register */
uint64_t x;


// long a_array[1024*1024*5/sizeof(long)];
// long b_array[1024*1024*5/sizeof(long)];

MemTest::MemTest() : 
    _index(0), _size(1), _oppp(0), _type(0)
{
}

MemTest::~MemTest()
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

/* worker function of each packets that enters MemTest */
/* asize: number of type 'long' elements in test arrays
 * long_size: sizeof(long) cached
 * 
 *
 * return value: new index
 */
long worker(unsigned long long asize, long *a, long *b, int type, unsigned long long block_size, long index, int oppp)
{
    unsigned long long t;
    int batch_cnt = 0;
    unsigned long long t1=0;


    unsigned int long_size=sizeof(long);
    /* array size in bytes */
    unsigned long long array_bytes=asize*long_size;

    if(type==TEST_MEMCPY) { /* memcpy test */
        ;
    } else if(type==TEST_MCBLOCK) { /* memcpy block test */ 
        ;
    } else if(type==TEST_DUMB_SEQ) { /* stream access, dump copy */
        for(t=index; t<asize && batch_cnt<oppp; t++,batch_cnt++) {
            b[t]=a[t];    
            //printf("%lld\n",t);   
        }
        /* wrap to the beginning, TODO: only support seq access now*/
        index = (index + oppp)%asize;
    } else if(type==TEST_DUMB_STEP) { /* step access, dump copy */
        ;
    } else if(type==TEST_DUMB_RAND) { /* random access, dump copy */
        for(; batch_cnt<oppp; batch_cnt++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            t1 = x%asize;
            b[t1]=a[t1];     
            //printf("%lld\n",t1);  
        }
    } else if(type==TEST_READ_SEQ) { /* seq read */
        ;
    } else if(type==TEST_READ_STEP) { /* seq read */
        ;
    } else if(type==TEST_READ_RAND) { /* rand read */
        ;
    } 
    else if(type==TEST_WRITE_SEQ) { /* seq write */
        ;
    } else if(type==TEST_WRITE_STEP) { /* step write */
        ;
    } else if(type==TEST_WRITE_RAND) { /* rand write */
        ;
    } 


    
    return index;
}

/* allocate a test array and fill it with data
 * so as to force Linux to _really_ allocate it */
long *make_array(unsigned long long asize)
{
    unsigned long long t;
    unsigned int long_size=sizeof(long);
    long *a;

    a=(long *)calloc(asize, long_size);

    if(NULL==a) {
        perror("Error allocating memory");
        exit(1);
    }

    for(t=0; t<asize; t++) {
        a[t]=0xaa;
    }
    return a;
}


int 
MemTest::configure(Vector<String> &conf, ErrorHandler *errh)
{
    if (Args(conf, this, errh)
        .read("TYPE", _type)
        .read("OPPP", _oppp)
        .read("SIZE", _size)
        .complete() < 0)
        return -1;
    
    

    return 0;
}

int
MemTest::initialize(ErrorHandler *)
{
    unsigned int long_size=0;
    long_size=sizeof(long); /* the size of long on this platform */
    _asize=(1024*1024*_size/long_size); /* how many longs then in one array? */
    a_array = make_array(_asize);
    x=0x11111111;
    // printf("Test type: %d\n",_type);
    switch(_type){
    case TEST_DUMB_SEQ:
    case TEST_DUMB_STEP:
    case TEST_DUMB_RAND:
    case TEST_MCBLOCK:
    case TEST_MEMCPY:
        b_array = make_array(_asize);
        break;
    /* no need for array b */
    case TEST_READ_SEQ:
    case TEST_READ_STEP:
    case TEST_READ_RAND:  
    case TEST_WRITE_SEQ:
    case TEST_WRITE_STEP:
    case TEST_WRITE_RAND:
        break;
    default:
        break;
    }
    return 0;
}


// void
// MemTest::push(int port, Packet *p)
// {
//     /* conduct some memory operations */
//     _index = worker(_asize, a_array, b_array, _type, 0, _index, _oppp);
//     // printf("_index=%ld, _oppp=%d\n",_index,_oppp);
//     output(0).push(p);
    
// }


Packet *
MemTest::simple_action(Packet *p){
    /* conduct some memory operations */
    _index = worker(_asize, a_array, b_array, _type, 0, _index, _oppp);
    // printf("_index=%ld, _oppp=%d\n",_index,_oppp);
    return p;     
}

CLICK_ENDDECLS
EXPORT_ELEMENT(MemTest)