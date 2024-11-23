
# $1 nf
# $2 competitor
# $3 folder to store
# $4 type
# $5 rate
# $6 size
# $7 flowsize
# $8 pktsize

# $9 regexprofile (for regexbench)

# echo $1
# echo $3
# echo $4
# echo $5
# echo $6
# echo $7
# echo $8

if [ "$2" == "none" ]; then
    echo "" > ./target_perf
    sleep 10
    cat ./target_perf > ./profile/"$1"/"$3"/target_perf_solo
    echo "[INFO] Nf $1 solo performance collected"
    echo ""
elif [ "$2" == "solo" ]; then 
    echo "[INFO] Starting target nf $1 solo"
    if [ "$1" == "flowstats" ] || [ "$1" == "iprouter" ] || [ "$1" == "nat" ] || [ "$1" == "iptunnel" ] || [ "$1" == "test_recv" ]; then 
        # warm up
        2>/dev/null taskset -c 0 click --dpdk -l 0 -n 1 -a 0000:03:00.0,class=net,rxq_cqe_comp_en=0 --file-prefix=dpdk0 -- port=0000:03:00.0 ../workloads/click/"$1".click > ./target_perf &
        # wait for steady performance
        sleep 10
        echo "" > ./target_perf
        sleep 10
    elif [ "$1" == "flowmon" ] || [ "$1" == "nids" ] || [ "$1" == "ipsecgw" ] || [ "$1" == "test_nf_pl" ] || [ "$1" == "test_nf_rtc" ]; then
        2>/dev/null taskset -c 0 click --dpdk -l 0 -n 1 -a 0000:03:00.0,class=net:regex,rxq_cqe_comp_en=0 --file-prefix=dpdk0 -- port=0000:03:00.0 ../workloads/click/"$1".click > ./target_perf &
        sleep 10
        echo "" > ./target_perf
        sleep 12
    elif [ "$1" == "ipcomp" ]; then
        2>/dev/null taskset -c 0 click --dpdk -l 0 -n 1 -a 0000:03:00.0,class=net:compress,rxq_cqe_comp_en=0 --file-prefix=dpdk0 -- port=0000:03:00.0 ../workloads/click/"$1".click > ./target_perf &
        sleep 10
        echo "" > ./target_perf
        sleep 12
    elif [ "$1" == "test_nf_mr_rtc" ] || [ "$1" == "test_nf_mr_pl" ]; then
        2>/dev/null taskset -c 0 click --dpdk -l 0 -n 1 -a 0000:03:00.0,class=net:regex,rxq_cqe_comp_en=0 --file-prefix=dpdk0 -- port=0000:03:00.0 type=0 oppp=500 size=5 ../workloads/click/"$1".click > ./target_perf &
        sleep 10
        echo "" > ./target_perf
        sleep 12
    elif [ "$1" == "test_nf_mrc_rtc" ] || [ "$1" == "test_nf_mrc_pl" ]; then
        2>/dev/null taskset -c 0 click --dpdk -l 0 -n 1 -a 0000:03:00.0,class=net:regex:compress,rxq_cqe_comp_en=0 --file-prefix=dpdk0 -- port=0000:03:00.0 type=0 oppp=500 size=5 ../workloads/click/"$1".click > ./target_perf &
        sleep 10
        echo "" > ./target_perf
        sleep 12
    elif [ "$1" == "acl" ] || [ "$1" == "flow_classify" ]; then
        # 2>/dev/null taskset -c 0-1 ../workloads/dpdk/ip_pipeline/build/ip_pipeline -l 0-1 -n 1 -a 0000:03:00.0,class=net,rxq_cqe_comp_en=0 --file-prefix=dpdk0 -- -s ../workloads/dpdk/ip_pipeline/examples/"$1".cli > /dev/null &
        2>/dev/null taskset -c 0-1 ../workloads/dpdk/ip_pipeline/build/ip_pipeline -l 0-1 -n 1 -a 0000:03:00.0,class=net,rxq_cqe_comp_en=0 --file-prefix=dpdk0 -- -s ../workloads/dpdk/ip_pipeline/examples/"$1".cli > /dev/null &
        sleep 50
        echo "" > ./target_perf
        sleep 10
    elif [ "$1" == "doca_flow" ] ; then
        2>/dev/null taskset -c 0 ../workloads/doca/samples/doca_flow/flow_pipeline/build/doca_flow_pipeline -l 0 -a 0000:03:00.0,class=net,dv_flow_en=2,rxq_cqe_comp_en=0 --file-prefix=dpdk0 -- -l 20 > /dev/null &
        sleep 15
        echo "" > ./target_perf
        sleep 10
    fi
    cat ./target_perf > ./profile/"$1"/"$3"/target_perf_solo
    echo "[INFO] Nf $1 solo performance collected"
    echo ""
    exit
elif [ "$2" == "membench" ]; then
    echo "[INFO] Starting synthetic competitor $2"
    taskset -c 3 ../workloads/synthetic/mbw/mbw -q -t"$4" -n 0 -r "$5" "$6" > /dev/null & 
    sleep 3
    echo "" > ./target_perf
    sleep 10
    cat ./target_perf > ./profile/"$1"/"$3"/target_perf_t"$4"_"$5"_"$6"_"$7"_"$8"
    echo "[INFO] Nf $1 performance collected with competitor t$4_$5_$6_$7_$8"
    echo ""
    echo "" >> ./profile/"$1"/"$3"/target_perf_t"$4"_"$5"_"$6"_"$7"_"$8"
    echo "t$4_$5_$6_$7_$8" >> ./profile/"$1"/"$3"/target_perf_t"$4"_"$5"_"$6"_"$7"_"$8"
    sudo pkill mbw
elif [ "$2" == "regbench" ]; then
    echo "[INFO] Starting synthetic competitor $2"
    taskset -c 3 ../workloads/accbench/build/accbench -D "-l3 -n 1 -a 03:00.0,class=regex --file-prefix dpdk1" --input-mode pcap_file -f ../traffic_profile/pcap/p0/l7_filter_78/"$9".pcap -d rxp -r ../rulesets/l7_filter/build/l7_filter_selected.rof2.binary  -c 1 -s 100 --rate "$5" --per-pkt-len > ./profile/"$1"/"$3"/comp_tput_"$5"_"$9" & 
    sleep 3
    echo "" > ./target_perf
    sleep 10
    cat ./target_perf > ./profile/"$1"/"$3"/target_perf_"$5"_"$9"
    echo "[INFO] Nf $1 performance collected with competitor accbench(regex) $5_$9"
    echo ""
    echo "" >> ./profile/"$1"/"$3"/target_perf_"$5"_"$9"
    echo "$5_$9" >> ./profile/"$1"/"$3"/target_perf_"$5"_"$9"
    sudo pkill accbench
fi












