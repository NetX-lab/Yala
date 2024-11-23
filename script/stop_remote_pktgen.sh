# $1 for control interface ip address

USERNAME=sfwu22
HOSTNAME=$1

ssh -l ${USERNAME} ${HOSTNAME} "sudo pkill pktgen; exit"  