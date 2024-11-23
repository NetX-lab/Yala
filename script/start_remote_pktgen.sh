#!/bin/bash
# $1 for control interface ip address
# $2 for script file name
# $3 for script file type (pcap / lua)
USERNAME=sfwu22
HOSTNAME=$1


if [ "$3" == "lua" ]; then
    SCRIPT1="./start_pktgen.sh"   
else
    SCRIPT1="./start_pktgen_pcap.sh"    
fi
ssh -l ${USERNAME} ${HOSTNAME} "cd ~/Yala/pktgen-dpdk/ ; 2>/dev/null 1>/dev/null ${SCRIPT1} $1 $2 ; exit" & 