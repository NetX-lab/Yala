/*
 * vim: ai ts=4 sts=4 sw=4 cinoptions=>4 expandtab
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

/* how many runs to average by default */
#define DEFAULT_NR_LOOPS 10

/* we have 7 tests at the moment */
#define MAX_TESTS 11

/* default block size, in bytes */
#define DEFAULT_BLOCK_SIZE 0
//#define DEFAULT_BLOCK_SIZE 262144

/* cache line size(in bytes) of L1/L2 cache of Cortex-A72 processor on Bluefield2 */
#define DEFAULT_CACHE_LINE_SIZE 64

/* default step size(in # of long elements) of TEST_DUMB_STEP */
#define DEFAULT_STEP_SIZE 8

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

/* random number generator using xor */
#define SEED 0x11111111

#define BATCH_SIZE 16384


/* version number */
#define VERSION "1.0"

/* random register */
uint64_t x;

/* array pos */
uint64_t a_pos;
/* step pos */
uint64_t step_pos;

unsigned long long temp_value;



/*
 * MBW memory bandwidth benchmark
 *
 * 2006, 2012 Andras.Horvath@gmail.com
 * 2013 j.m.slocum@gmail.com
 * (Special thanks to Stephen Pasich)
 *
 * http://github.com/raas/mbw
 *
 * compile with:
 *			gcc -O -o mbw mbw.c
 *
 * run with eg.:
 *
 *			./mbw 300
 *
 * or './mbw -h' for help
 *
 * watch out for swap usage (or turn off swap)
 */

void usage()
{
    printf("mbw memory benchmark v%s, https://github.com/raas/mbw\n", VERSION);
    printf("Usage: mbw [options] array_size_in_MiB\n");
    printf("Options:\n");
    printf("	-n: number of runs per test (0 to run forever)\n");
    printf("	-a: Don't display average\n");
    printf("	-t%d: memcpy test\n", TEST_MEMCPY);
    printf("	-t%d: dumb (b[i]=a[i] style) test\n", TEST_DUMB_SEQ);
    printf("	-t%d: memcpy test with fixed block size\n", TEST_MCBLOCK);
    printf("	-b <size>: block size in bytes for -t2 (default: %d)\n", DEFAULT_BLOCK_SIZE);
    printf("	-q: quiet (print statistics only)\n");
    printf("(will then use two arrays, watch out for swapping)\n");
    printf("'Bandwidth' is amount of data copied over the time this operation took.\n");
    printf("\nThe default is to run all tests available.\n");
}

/* ------------------------------------------------------ */

/* allocate a test array and fill it with data
 * so as to force Linux to _really_ allocate it */
long *make_array(unsigned long long asize)
{
    unsigned long long t;
    unsigned int long_size=sizeof(long);
    long *a;

    a=calloc(asize, long_size);

    if(NULL==a) {
        perror("Error allocating memory");
        exit(1);
    }

    /* make sure both arrays are allocated, fill with pattern */
    for(t=0; t<asize; t++) {
        a[t]=0xaa;
    }
    return a;
}

/* actual benchmark */
/* asize: number of type 'long' elements in test arrays
 * long_size: sizeof(long) cached
 * type: 0=use memcpy, 1=use dumb copy loop (whatever GCC thinks best)
 *
 * return value: elapsed time in seconds
 */
double worker(unsigned long long asize, long *a, long *b, int type, unsigned long long block_size)
{
    unsigned long long t;
    unsigned long long batch_cnt = 0;
    unsigned long long t1=0;
    // unsigned long long t2=0;
    unsigned long long t_start;
    struct timeval starttime, endtime;
    double te;
    unsigned int long_size=sizeof(long);
    /* array size in bytes */
    unsigned long long array_bytes=asize*long_size;

    if(type==TEST_MEMCPY) { /* memcpy test */
        // memcpy(b, a, array_bytes);
        ;
    } else if(type==TEST_MCBLOCK) { /* memcpy block test */
        // char* src = (char*)a;
        // char* dst = (char*)b;
        // for (t=array_bytes; t >= block_size; t-=block_size, src+=block_size){
        //     dst=(char *) memcpy(dst, src, block_size) + block_size;
        // }
        // if(t) {
        //     dst=(char *) memcpy(dst, src, t) + t;
        // }
        ;
    } else if(type==TEST_DUMB_SEQ) { /* steam access, dump copy */
        for(t=a_pos; t<asize && batch_cnt<BATCH_SIZE; t++,batch_cnt++) {
            b[t]=a[t];    
            //printf("%lld\n",t);   
        }      
    } else if(type==TEST_DUMB_STEP) { /* step access, dump copy */
        // for(t_start=0; t_start<DEFAULT_STEP_SIZE; t_start++) {
        for(t=a_pos; t<asize && batch_cnt<BATCH_SIZE; t= t + DEFAULT_STEP_SIZE,batch_cnt++) {
            b[t]=a[t];   
            //printf("%lld\n",t);    
        }     
        //} 
    } else if(type==TEST_DUMB_RAND) { /* random access, dump copy */
        for(; batch_cnt<BATCH_SIZE; batch_cnt++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            t1 = x%asize;
            b[t1]=a[t1];     
            //printf("%lld\n",t1);  
        }    
    } else if(type==TEST_READ_SEQ) { /* seq read */
        for(t=a_pos; t<asize && batch_cnt<BATCH_SIZE; t++,batch_cnt++) {
            temp_value=a[t];   
            //printf("%lld\n",t);    
        }    
    } else if(type==TEST_READ_STEP) { /* seq read */
        for(t=a_pos; t<asize && batch_cnt<BATCH_SIZE; t= t + DEFAULT_STEP_SIZE,batch_cnt++) {
            temp_value=a[t];   
            //printf("%lld\n",t);    
        }     
    } else if(type==TEST_READ_RAND) { /* rand read */
        for(; batch_cnt<BATCH_SIZE; batch_cnt++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            t1 = x%asize;
            temp_value=a[t1];    
            //printf("%lld\n",t1);   
        }   
    } 
    else if(type==TEST_WRITE_SEQ) { /* seq write */
        for(t=a_pos; t<asize && batch_cnt<BATCH_SIZE; t++,batch_cnt++) {
            a[t]=0;   
            //printf("%lld\n",t);    
        }    
    } else if(type==TEST_WRITE_STEP) { /* step write */
        for(t=a_pos; t<asize && batch_cnt<BATCH_SIZE; t= t + DEFAULT_STEP_SIZE,batch_cnt++) {
            a[t]=0;   
            //printf("%lld\n",t);    
        }     
    } else if(type==TEST_WRITE_RAND) { /* rand write */
        for(; batch_cnt<BATCH_SIZE; batch_cnt++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            t1 = x%asize;
            a[t1]=0;      
            //printf("%lld\n",t1); 
        }   
    }



    
    /* wrap to the beginning */
    if(type!=TEST_DUMB_RAND && type!=TEST_READ_RAND && type!=TEST_WRITE_RAND){
        a_pos = t % asize;
        /* if in step mode, then increment the initial position for step */
        if(a_pos < DEFAULT_STEP_SIZE && (type==TEST_DUMB_STEP || type==TEST_READ_STEP || type==TEST_WRITE_STEP)){
            step_pos = (step_pos + 1)%DEFAULT_STEP_SIZE;
            a_pos = step_pos;
        }
    }
    
    return te;
}

/* ------------------------------------------------------ */

/* pretty print worker's output in human-readable terms */
/* te: elapsed time in seconds
 * mt: amount of transferred data in MiB
 * type: see 'worker' above
 *
 * return value: -
 */
void printout(double te, double mt, int type)
{
    switch(type) {
        case TEST_MEMCPY:
            printf("Method: MEMCPY\t");
            break;
        case TEST_DUMB_SEQ:
            printf("Method: DUMB\t");
            break;
        case TEST_MCBLOCK:
            printf("Method: MCBLOCK\t");
            break;
    }
    printf("Elapsed: %.5f\t", te);
    printf("MiB: %.5f\t", mt);
    printf("Copy: %.3f MiB/s\n", mt/te);
    return;
}

/* ------------------------------------------------------ */

int main(int argc, char **argv)
{
    unsigned int long_size=0;
    double te, te_sum; /* time elapsed */
    struct timespec starttime, endtime, endtime_last; /* time in nanoseconds */
    double totaltime; /* time in nanoseconds */
    double totaltime_last; /* time in nanoseconds */
    double avg_rate, split_rate; /* strict operation average rate */

    unsigned long long asize=0; /* array size (elements in array) */
    int i;
    int cnt, cnt_last;
    long *a, *b; /* the two arrays to be copied from/to */
    int o; /* getopt options */
    unsigned long testno;
    double pps = 1;

    char *endptr;

    /* options */

    /* how many runs to average? */
    int nr_loops=DEFAULT_NR_LOOPS;
    /* fixed memcpy block size for -t2 */
    unsigned long long block_size=DEFAULT_BLOCK_SIZE;
    /* show average, -a */
    int showavg=1;
    /* what tests to run (-t x) */
    int tests[MAX_TESTS] = {0};
    double mt=0; /* MiBytes transferred == array size in MiB */
    double mt_test=0; /* MiBytes transferred == array size in MiB */
    int quiet=0; /* suppress extra messages */

    // for(i=0;i<MAX_TESTS;i++){
    //     tests[i]=0;
    // }

    while((o=getopt(argc, argv, "haqn:t:b:r:")) != EOF) {
        switch(o) {
            case 'h':
                usage();
                exit(1);
                break;
            case 'a': /* suppress printing average */
                showavg=0;
                break;
            case 'n': /* no. loops */
                nr_loops=strtoul(optarg, (char **)NULL, 10);
                break;
            case 't': /* test to run */
                testno=strtoul(optarg, (char **)NULL, 10);
                // printf("testno=%ld\n",testno);
                if(testno>MAX_TESTS-1) {
                    printf("Error: test number must be between 0 and %d\n", MAX_TESTS-1);
                    exit(1);
                }
                tests[testno]=1;
                break;
            case 'b': /* block size in bytes*/
                block_size=strtoull(optarg, (char **)NULL, 10);
                if(0>=block_size) {
                    printf("Error: what block size do you mean?\n");
                    exit(1);
                }
                break;
            case 'q': /* quiet */
                quiet=1;
                break;
            case 'r': /* rate in kpps */
                pps= strtod(optarg, &endptr);
                //printf("%s:%f\n",optarg,pps);
                if(0>pps) {
                    printf("Error: operation rate can not be negative\n");
                    exit(1);
                }
                break;
            default:
                break;
        }
    }

    /* default is to run all tests if no specific tests were requested */
    // if( (tests[0]+tests[1]+tests[2]) == 0) {
    //     tests[0]=1;
    //     tests[1]=1;
    //     tests[2]=1;
    // }

    // if( nr_loops==0 && ((tests[0]+tests[1]+tests[2]) != 1) ) {
    //     printf("Error: nr_loops can be zero if only one test selected!\n");
    //     exit(1);
    // }

    if(optind<argc) {
        // printf("%s\n",argv[optind]);
        mt=strtod(argv[optind++], &endptr);
    } else {
        printf("Error: no array size given!\n");
        exit(1);
    }

    if(0>=mt) {
        printf("Error: array size wrong!\n");
        exit(1);
    }

    /* ------------------------------------------------------ */
    // for debug purpose
    // mt = 0.01;
    long_size=sizeof(long); /* the size of long on this platform */
    asize=(1024*1024*mt/long_size); /* how many longs then in one array? */

    if(asize*long_size < block_size) {
        printf("Error: array size larger than block size (%llu bytes)!\n", block_size);
        exit(1);
    }

    if(!quiet) {
        printf("Long uses %d bytes. ", long_size);
        printf("Allocating 2*%lld elements = %lld bytes of memory.\n", asize, 2*asize*long_size);
        if(tests[2]) {
            printf("Using %lld bytes as blocks for memcpy block copy test.\n", block_size);
        }
    }
   

    /* ------------------------------------------------------ */
    if(!quiet) {
        printf("Getting down to business... Doing %d runs per test.\n", nr_loops);
        printf("Rate limiting: %f pps\n", pps);
    }

    /* run all tests requested, the proper number of times */
    for(testno=0; testno<MAX_TESTS; testno++) {
        te_sum=0;
        //printf("tests[testno]=%d\n",tests[testno]);
        if(tests[testno]) {
            x = SEED;
            a_pos = 0;
            step_pos = 0;
            a=make_array(asize);
            printf("Allocating array a...\n");
            switch(testno){
                case TEST_DUMB_SEQ:
                    printf("Dumb-style sequence test running\n");
                    b=make_array(asize);
                    break;
                case TEST_DUMB_STEP:
                    printf("Dumb-style step test running\n");
                    b=make_array(asize);
                    break;
                case TEST_DUMB_RAND:
                    printf("Dumb-style random test running\n");
                    b=make_array(asize);
                    break;
                case TEST_MCBLOCK:
                    printf("Memcpy-block test running\n");
                    b=make_array(asize);
                    break;
                case TEST_MEMCPY:
                    printf("Memcpy test running\n");
                    b=make_array(asize);
                    break;  
                /* no need for array b */
                case TEST_READ_SEQ:
                    printf("Read seq test running\n");
                    break;
                case TEST_READ_STEP:
                    printf("Read step test running\n");
                    break;
                case TEST_READ_RAND:
                    printf("Read rand test running\n");
                    break;
                case TEST_WRITE_SEQ:
                    printf("Write seq test running\n");
                    break;
                case TEST_WRITE_STEP:
                    printf("Write step test running\n");
                    break;
                case TEST_WRITE_RAND:
                    printf("Write rand test running\n");
                    break;
                default:
                    printf("Unknown test type\n");  
            }
            printf("source array size: %lld long elements, %f MB\n", asize, mt);
            cnt = 0;
            cnt = cnt_last;
            totaltime_last = 0;
            clock_gettime(CLOCK_MONOTONIC, &starttime);
            endtime_last = starttime;
            for (i=0; nr_loops==0 || i<nr_loops; i++) {
                clock_gettime(CLOCK_MONOTONIC, &endtime);

                totaltime = (endtime.tv_sec - starttime.tv_sec) * 1000000000.0 + (endtime.tv_nsec - starttime.tv_nsec);
                
                
                totaltime_last = (endtime.tv_sec - endtime_last.tv_sec) * 1000000000.0 + (endtime.tv_nsec - endtime_last.tv_nsec);
                
                //avg_rate = (cnt* asize * 1000000000.0)/(1000000.0 * totaltime); // Mops/s
                avg_rate = (cnt* BATCH_SIZE * 1000000000.0)/(1000.0 * totaltime); // kops/s
                if(totaltime_last > 1000000000.0){
                    /* totaltime last in ns */
                    split_rate =  ((cnt-cnt_last)* BATCH_SIZE * 1000000000.0)/ (totaltime_last);/* ops/s */
                    endtime_last = endtime;
                    cnt_last = cnt;
                    //printf("split rate: %f calls/s, %f Mrefs/s\n",split_rate, split_rate*asize/1000000.0);
                    printf("avg: %f Mops/s, split: %f Mops/s\n",avg_rate/1000.0, split_rate/1000000.0);
                }
                
                if(avg_rate >= pps){
                    continue;
                }
                
                cnt ++; /* cnt refers to the number of calls to worker */
                te=worker(asize, a, b, testno, block_size);
                // te_sum+=te;
                // printf("%d\t", i);
                // printout(te, mt, testno);
                
            }
            // if(showavg) {
            //     printf("AVG\t");
            //     printout(te_sum/nr_loops, mt, testno);
            // }
        }
    }

    free(a);
    free(b);
    return 0;
}

