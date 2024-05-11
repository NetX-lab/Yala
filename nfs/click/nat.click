AddressInfo(
    eth0-in     100.100.100.100     100.100.0.0/16          08:c0:eb:8e:d6:86,
    eth0-ex     66.58.65.90                     00:0d:87:9d:1c:e9,
    gw-addr     66.58.65.89                     00:20:6f:14:54:c2
);

in_device	:: FromDPDKDevice($port);
ctr0 :: Counter();
ctr1 :: AverageCounter();
classifier	:: Classifier(12/0800 /* IP packets */,
			      - /* everything else */); 
// Define pattern NAT

iprw :: IPRewriterPatterns(NAT 66.58.65.90 0-65535 - -);

// Rewriting rules for UDP/TCP packets
// output[0] rewritten to go into the wild
// output[1] rewritten to come back from the wild or no match
rw :: IPRewriter(pattern NAT 0 1,
                 pass 1,
                 TCP_TIMEOUT 3600,
                 UDP_TIMEOUT 3600,
                 REAP_INTERVAL 3600,
                 MAPPING_CAPACITY 16384
                 );

                 
in_device->classifier;

classifier[1] -> Discard;
// IP packets
classifier[0] -> Strip(14)
   -> CheckIPHeader
   -> ipclass :: IPClassifier(dst host eth0-ex,
                              dst host eth0-in,
                              src net eth0-in);

// Packets directed at eth0-ex.
// Send it through IPRewriter(pass).  If there was a mapping, it will be
// rewritten such that dst is eth0-in:net, otherwise dst will still be for
// eth0-ex.
ipclass[0] -> [1]rw;

ipclass[1] -> Discard;
ipclass[2] -> ctr0 ->ctr1 -> [0]rw;

// packets that were rewritten, heading into the wild world.
rw[0] -> Discard;  

// packets return rom an established connection or to the router
rw[1] -> Discard;   

// Note: the simplified traffic we use is all "src net eth0-in" 

                                
s10 :: Script(wait 1, set x1 $(ctr0.bit_rate), set y1 $(s10.mul $x1 0.000001), set x2 $(ctr0.rate), set y2 $(s10.mul $x2 0.001), , print $y1 " Mbps\t" $y2 " Kpps\t" , loop);

