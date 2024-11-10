#!/bin/bash
# $9 for input traffic profile for comp and regex
# $10 
nf=$1
competitor=$2
folder=$3
type=$4
rate=$5
size=$6
flowsz=$7
pktsz=$8
mtbr=$9
num=$10


# collect solo data
mkdir -p ./profile/"${nf}"/"${folder}"
ssh -l root dpu "cd ~/PerfNIC/adaptive_profile ; mkdir -p ./profile/"${nf}"/"${folder}" ; 2>/dev/null ./mem_sens_profile_snic.sh "${nf}" "${competitor}" "${folder}" "${type}" "${rate}" "${size}" "${flowsz}" "${pktsz}" "${mtbr}"  ; exit" 

    
















