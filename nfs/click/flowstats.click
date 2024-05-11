in_device	:: FromDPDKDevice($port);
ctr0 :: Counter();


af :: AggregateIPFlows(UDP_TIMEOUT 3600, TCP_TIMEOUT 3600);

classifier	:: Classifier(12/0800 /* IP packets */,
			      - /* everything else */); 


in_device 
    -> classifier
    -> Strip(14)
    -> CheckIPHeader(0, CHECKSUM false) // don't check checksum for speed
    -> SetTimestamp
    -> af[0]
    -> AggregateCounter
    -> ctr0
	-> Discard;

/* not ip packets */      
classifier[1] -> Discard;

/* not udp/tcp packets */    
af[1] -> Print -> Discard;

s10 :: Script(wait 1, set x1 $(ctr0.bit_rate), set y1 $(s10.mul $x1 0.000001), set x2 $(ctr0.rate), set y2 $(s10.mul $x2 0.001), , print $y1 " Mbps\t" $y2 " Kpps\t" , loop);