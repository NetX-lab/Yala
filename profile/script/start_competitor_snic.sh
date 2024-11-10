
# $2 core
# $2 competitor
# $3 folder
# $4 type
# $5 rate
# $6 size
# $7 nf
# $8 point_number

# $9 regexprofile (for regexbench and multibench)
# OR
# $9 port address (for nfs)

# echo $2
# echo $3
# echo $4
# echo $5
# echo $6
# echo $7
# echo $8

core0=$1
core1=$(echo $1 + 1 | bc)

if [ "$2" == "none" ]; then
    echo "[INFO] Starting competitor $2"
elif [ "$2" == "flowstats" ] || [ "$2" == "iprouter" ] || [ "$2" == "nat" ] || [ "$2" == "iptunnel" ] || [ "$2" == "test_recv" ]; then 
    2>/dev/null taskset -c $1 click1 --dpdk -l $1 -n 1 -a $9,class=net,rxq_cqe_comp_en=0 --file-prefix=dpdk$1 -- port=$9 ../workloads/click/"$2".click  > /dev/null &
    sleep 3 
elif [ "$2" == "flowmon" ] || [ "$2" == "nids" ] || [ "$2" == "ipsecgw" ] || [ "$2" == "test_nf_pl" ] || [ "$2" == "test_nf_rtc" ]; then
    if [ "$9" == "0000:03:00.0" ]; then
        2>/dev/null taskset -c $1 click1 --dpdk -l $1 -n 1 -a $9,class=net:regex,rxq_cqe_comp_en=0 --file-prefix=dpdk$1 -- port=$9 ../workloads/click/"$2".click  > /dev/null &
    else
        2>/dev/null taskset -c $1 click1 --dpdk -l $1 -n 1 -a $9,class=net,rxq_cqe_comp_en=0 -a 0000:03:00.0,class=regex --file-prefix=dpdk$1 -- port=$9 ../workloads/click/"$2".click  > /dev/null &
    fi
    sleep 3
elif [ "$2" == "ipcomp" ]; then
    if [ "$9" == "0000:03:00.0" ]; then
        2>/dev/null taskset -c $1 click1 --dpdk -l $1 -n 1 -a $9,class=net:compress,rxq_cqe_comp_en=0 --file-prefix=dpdk$1 -- port=$9 ../workloads/click/"$2".click  > /dev/null &
    else
        2>/dev/null taskset -c $1 click1 --dpdk -l $1 -n 1 -a $9,class=net,rxq_cqe_comp_en=0 -a 0000:03:00.0,class=compress --file-prefix=dpdk$1 -- port=$9 ../workloads/click/"$2".click > /dev/null &
    fi
    sleep 3
elif [ "$2" == "test_nf_mr_rtc" ] || [ "$2" == "test_nf_mr_pl" ]; then
    if [ "$9" == "0000:03:00.0" ]; then
        2>/dev/null taskset -c $1 click1 --dpdk -l $1 -n 1 -a $9,class=net:regex,rxq_cqe_comp_en=0 --file-prefix=dpdk$1 -- port=$9 type=0 oppp=500 size=5 ../workloads/click/"$2".click  > /dev/null &
    else
        2>/dev/null taskset -c $1 click1 --dpdk -l $1 -n 1 -a $9,class=net,rxq_cqe_comp_en=0 -a 0000:03:00.0,class=regex --file-prefix=dpdk$1 -- port=$9 type=0 oppp=500 size=5 ../workloads/click/"$2".click  > /dev/null &
    fi
    sleep 3
elif [ "$2" == "acl" ] || [ "$2" == "flow_classify" ]; then
    # not usable for cores other than 0-1
    2>/dev/null taskset -c ${core0}-${core1} ../workloads/dpdk/ip_pipeline/build/ip_pipeline1 -l ${core0}-${core1} -n 1 -a $9,class=net,rxq_cqe_comp_en=0 --file-prefix=dpdk$1 -- -s ../workloads/dpdk/ip_pipeline/examples/"$2".cli  > /dev/null &
    # requires some time to program
    sleep 50 
elif [ "$2" == "doca_flow" ] ; then
    2>/dev/null taskset -c $1 ../workloads/doca/samples/doca_flow/flow_pipeline/build/doca_flow_pipeline1 -l $1 -a $9,class=net,dv_flow_en=2,rxq_cqe_comp_en=0 --file-prefix=dpdk$1 -- -l 20  > /dev/null &
    sleep 15
elif [ "$2" == "membench" ]; then
    echo "[INFO] Starting synthetic competitor $2"
    taskset -c $1 ../workloads/synthetic/mbw/mbw -q -t"$4" -n 0 -r "$5" "$6" > /dev/null & 
elif [ "$2" == "regbench" ]; then
    echo "[INFO] Starting synthetic competitor $2"
    # taskset -c $1 ../workloads/accbench/build/accbench -D "-l$1 -n 1 -a 03:00.0,class=regex --file-prefix dpdk$1" --input-mode pcap_file -f ../traffic_profile/pcap/p0/l7_filter_78/"$9".pcap -d rxp -r ../rulesets/l7_filter/build/l7_filter_selected.rof2.binary  -c 1 -s 100 --rate "$5" --per-pkt-len  > /dev/null & 
    taskset -c $1 ../workloads/accbench/build/accbench -D "-l$1 -n 1 -a 03:00.0,class=regex --file-prefix dpdk$1" --input-mode pcap_file -f ../traffic_profile/pcap/p0/l7_filter_78/"$9".pcap -d rxp -r ../rulesets/l7_filter/build/l7_filter_selected.rof2.binary  -c 1 -s 100 --rate "$5" --per-pkt-len  > ./profile/"$7"/"$3"/comp_tput_"$8"  & 
elif [ "$2" == "compbench" ]; then
    echo "[INFO] Starting synthetic competitor $2"
    taskset -c $1 ../workloads/accbench/build/accbench -D "-l$1 -n 1 -a 03:00.0,class=compress --file-prefix dpdk$1" --input-mode pcap_file -f ../traffic_profile/pcap/p0/l7_filter_78/"$9".pcap -d comp_dpdk -r ../rulesets/l7_filter/build/l7_filter_selected.rof2.binary  -c 1 -s 100 --rate "$5" --per-pkt-len  > /dev/null & 
elif [ "$2" == "multibench" ]; then
    echo "[INFO] Starting synthetic competitor $2"
    taskset -c $1 ../workloads/multibench/build/multibench -D "-l$1 -n 1 -a 03:00.0,class=regex --file-prefix dpdk$1" --input-mode pcap_file -f ../traffic_profile/pcap/p0/l7_filter_78/"$9".pcap -d rxp -r ../rulesets/l7_filter/build/l7_filter_selected.rof2.binary  -c 1 -s 100 --rate 0 --type "$4" --oppp "$5" --size "$6" --per-pkt-len  > /dev/null & 
fi












