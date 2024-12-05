#!/bin/bash

# Note: currently the script uses root@dpu where root is the username and dpu is the hostname of the target SmartNIC. Pleae change the username and hostname accordingly. 

nf=$1
competitor=$2
folder=$3
type=$4
rate=$5
size=$6
flowsz=$7
pktsz=$8
mtbr=$9
# $9 for input traffic profile for comp and regex

# collect solo data
mkdir -p ./profile/"${nf}"/"${folder}"
ssh -l root dpu "cd ~/Yala/script ; mkdir -p ./profile/"${nf}"/"${folder}" ; 2>/dev/null ./sens_profile_snic.sh "${nf}" "${competitor}" "${folder}" "${type}" "${rate}" "${size}" "${flowsz}" "${pktsz}" "${mtbr}"  ; exit" 

    
















