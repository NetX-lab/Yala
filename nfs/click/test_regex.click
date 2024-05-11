in_device	:: FromDPDKDevice($port);
ctr0 :: Counter();
Regex:: RegexMatch(BATCH 32, LATMODE 0, DEBUG 1);

in_device 
    -> Regex
    -> ctr0
	-> Discard;

s10 :: Script(wait 1, set x1 $(ctr0.bit_rate), set y1 $(s10.mul $x1 0.000001), set x2 $(ctr0.rate), set y2 $(s10.mul $x2 0.001), , print $y1 " Mbps\t" $y2 " Kpps\t" , loop);
