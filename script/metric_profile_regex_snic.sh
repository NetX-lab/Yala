#!/bin/bash


echo "[INFO] Starting synthetic competitor $2_$4"

taskset -c 3 ../workloads/accbench/build/accbench -D "-l3 -n 1 -a 03:00.0,class=regex --file-prefix dpdk1" --input-mode pcap_file -f ../traffic_profile/pcap/p0/l7_filter_78/"$4".pcap -d rxp -r ../rulesets/l7_filter/build/l7_filter_selected.rof2.binary  -c 1 -s 100 --rate "$2" --per-pkt-len > ./profile/regbench/"$4"/comp_tput_"$2"_"$4" & 
sleep 10
echo "[INFO] Collecting synthetic competitor performance counters and throughput"

perf stat -p $(pidof -s accbench) -o ./profile/regbench/"$4"/comp_pc_"$2"_"$4" -e cycles,instructions,inst_retired,l2d_cache_rd,l2d_cache_wr,l2d_cache,mem_access_rd,mem_access_wr sleep 3
# working set size estimation
../tool/wss/wss.pl $(pidof -s accbench) 3 >> ./profile/regbench/"$4"/comp_pc_"$2"_"$4"
../tool/wss/wss.pl $(pidof -s accbench) 3 >> ./profile/regbench/"$4"/comp_pc_"$2"_"$4"

sudo pkill accbench
echo "" >> ./profile/regbench/"$4"/comp_pc_"$2"_"$4"
echo "t$2_$4" >> ./profile/regbench/"$4"/comp_pc_"$2"_"$4"
echo "t$2_$4" >> ./profile/regbench/"$4"/comp_tput_"$2"_"$4"

echo "[INFO] Synthetic competitor performance counters collected"
echo ""