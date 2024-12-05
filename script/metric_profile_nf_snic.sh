#!/bin/bash

# Note: currently the script requires root permission on target SmartNIC. 

# $1: nf app name
# $2: nf
# $3: flowsz
# $4: pktsze

echo "[INFO] Collecting nf performance counters"

perf stat -p $(pidof -s "$1") -o ./profile/"$2"/pc/comp_pc_"$3"_"$4" -e cycles,instructions,inst_retired,l2d_cache_rd,l2d_cache_wr,l2d_cache,mem_access_rd,mem_access_wr sleep 3
# working set size estimation
../tool/wss/wss.pl $(pidof -s "$1") 3 >> ./profile/"$2"/pc/comp_pc_"$3"_"$4"
../tool/wss/wss.pl $(pidof -s "$1") 3 >> ./profile/"$2"/pc/comp_pc_"$3"_"$4"

echo "" >> ./profile/"$2"/pc/comp_pc_"$3"_"$4"
echo "$3_$4" >> ./profile/"$2"/pc/comp_pc_"$3"_"$4"

echo "[INFO] Nf performance counters collected"
echo ""






