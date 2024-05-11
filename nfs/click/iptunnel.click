// This network function implements IP tunneling with fragmentation and reassembly
// between two networks that have different MTUs
/* packet ingress */ 
in_device	:: FromDPDKDevice($port);

/* rate stats */ 
ctr0 :: Counter();
ctr1 :: AverageCounter();

classifier	:: Classifier(12/0800 /* IP packets */,
			      - /* everything else */); 

in_device->classifier;

classifier[1] -> Discard;
// IP packets
classifier[0] 
-> Strip(14)
-> CheckIPHeader() // check IP header validity
-> ctr0
-> ctr1
-> IPFragmenter(512)  // fragment packets that are larger than x bytes
-> IPEncap(0x04, 10.42.1.1, 10.42.2.1)  // encapsulate packets with IP header
-> Discard; 

s10 :: Script(wait 1, set x1 $(ctr0.bit_rate), set y1 $(s10.mul $x1 0.000001), set x2 $(ctr0.rate), set y2 $(s10.mul $x2 0.001), , print $y1 " Mbps\t" $y2 " Kpps\t" , loop);
