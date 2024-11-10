#!/bin/bash
# $9 for input traffic profile for comp and regex
core=$1
competitor=$2
folder=$3
type=$4
rate=$5
size=$6
flowsz=$7
pktsz=$8
mtbr=$9


        
# start one competitor
ssh -l root dpu "cd ~/PerfNIC/adaptive_profile ; 2>/dev/null ./start_competitor_snic.sh "${core}" "${competitor}" "${folder}" "${type}" "${rate}" "${size}" "${flowsz}" "${pktsz}" "${mtbr}"  ; exit" 
    
















