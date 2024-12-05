#!/bin/bash

# $1 the competitor to be profiled
# $1 can be 1) membench 2) nf

# $2 flowsz
# $3 pktsz

# type - memory operation type 
# oppp - operation per packet 
# size - allocated buffer size
# regex_profile - different regex input profile with different MtBR
# rate - traffic rate for remote pktgen


if [ $1 == "membench" ] ; then 
    # membench can be run locally
    for type in 0 1 
    do
        if [ "${type}" == 0 ]; then
            rate_min=5000
            rate_max=160000 
            rate_step=5000
        elif [ "${type}" == 1 ]; then
            rate_min=5000
            rate_max=90000
            rate_step=5000
        elif [ "${type}" == 2 ]; then
            rate_min=5000
            rate_max=30000
            rate_step=10000
        elif [ "${type}" == 6 ]; then
            rate_min=5000
            rate_max=200000
            rate_step=20000
            size_step=1
        elif [ "${type}" == 7 ]; then
            rate_min=5000
            rate_max=75000
            rate_step=20000
            size_step=1
        else
            rate_min=5000
            rate_max=200000
            rate_step=5000
        fi
        for ((rate=${rate_min}; rate<=${rate_max}; rate=rate+${rate_step})); do
            for size in $(seq 0.5 0.5 6); do
                # performance counter collecting, working set size estimation
                ssh -l root dpu "cd ~/Yala/script ; mkdir -p ./profile/membench; 2>/dev/null ./metric_profile_mem_snic.sh ${type} ${rate} ${size} ; exit"
                ssh -l root dpu "sudo pkill membench ; exit"
            done
        done
    done    
elif [ $1 == "regbench" ] ; then
    for mtbr in "raw_20_1500" "raw_80_1500" "raw_160_1500"
    do
        ssh -l root dpu "cd ~/Yala/script ; mkdir -p ./profile/regbench/${mtbr}; exit"
        for ((rate=100; rate<=100; rate=rate+1)); do
            ssh -l root dpu "cd ~/Yala/script ; 2>/dev/null ./metric_profile_regex_snic.sh 0 ${rate} 0 ${mtbr} ; exit"
            ssh -l root dpu "sudo pkill regbench ; exit"
        done
    done
elif [ "$1" == "flowstats" ] || [ "$1" == "iprouter" ] || [ "$1" == "nat" ] || [ "$1" == "iptunnel" ] || [ "$1" == "test_recv" ] || [ "$1" == "flowmon" ] || [ "$1" == "nids" ] || [ "$1" == "ipsecgw" ] || [ "$1" == "test_nf_pl" ] || [ "$1" == "test_nf_rtc" ] || [ "$1" == "ipcomp" ] || [ "$1" == "test_nf_mr_rtc" ] || [ "$1" == "test_nf_mr_pl" ]; then 
    # ssh -l root dpu "cd ~/Yala/script ; mkdir -p ./profile/"$2"/pc/; exit"
    ssh -l root dpu "cd ~/Yala/script ; 2>/dev/null ./metric_profile_nf_snic.sh click "$1" "$2" "$3" ; exit"
elif [ "$1" == "acl" ] || [ "$1" == "flow_classify" ]; then 
    # ssh -l root dpu "cd ~/Yala/script ; mkdir -p ./profile/"$2"/pc/; exit"
    ssh -l root dpu "cd ~/Yala/script ; 2>/dev/null ./metric_profile_nf_snic.sh "ip_pipeline" "$1" "$2" "$3" ; exit"
elif [ "$1" == "doca_flow" ] ; then 
    # ssh -l root dpu "cd ~/Yala/script ; mkdir -p ./profile/"$2"/pc/; exit"
    ssh -l root dpu "cd ~/Yala/script ; 2>/dev/null ./metric_profile_nf_snic.sh "doca_flow_pipeline" "$1" "$2" "$3" ; exit"
else
    echo "No profiling target specified, ending..."
fi




