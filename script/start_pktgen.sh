#!/bin/bash
sudo kill -9 $(pidof pktgen)
sudo ../pktgen-dpdk/Builddir/app/pktgen -l4-7 -n 4 -a 0000:41:00.0 --file-prefix=pktgen0 -- -N -T -P -g $1:22022 -m "[5-6:7].0" -f ../traffic_profile/pktgen/$2.lua &