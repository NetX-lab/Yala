#!/bin/bash

# Note: currently the script requires root permission on target SmartNIC. 

echo "[INFO] Starting synthetic competitor $1_$2_$3"

taskset -c 3 ../nfs/synthetic/membench/membench -q -t"$1" -n 0 -r "$2" "$3" > /dev/null & 
sleep 1
echo "[INFO] Collecting synthetic competitor performance counters"

perf stat -p $(pidof -s membench) -o ./profile/membench/comp_pc_t"$1"_"$2"_"$3" -e cycles,instructions,inst_retired,l2d_cache_rd,l2d_cache_wr,l2d_cache,mem_access_rd,mem_access_wr sleep 3
# working set size estimation
../tool/wss/wss.pl $(pidof -s membench) 3 >> ./profile/membench/comp_pc_t"$1"_"$2"_"$3"
../tool/wss/wss.pl $(pidof -s membench) 3 >> ./profile/membench/comp_pc_t"$1"_"$2"_"$3"

echo "" >> ./profile/membench/comp_pc_t"$1"_"$2"_"$3"
echo "t$1_$2_$3" >> ./profile/membench/comp_pc_t"$1"_"$2"_"$3"

echo "[INFO] Synthetic competitor performance counters collected"
sudo pkill membench
echo ""






