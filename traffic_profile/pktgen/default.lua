package.path = package.path ..";?.lua;test/?.lua;app/?.lua;"

require "Pktgen"
pktgen.screen("on");


pktgen.set_range("0", "on");

-- Port 0 b8:ce:f6:83:b8:fc
-- pktgen.range.src_mac("0", "start", "b8:ce:f6:83:b8:fc");
pktgen.range.dst_mac("0", "start", "08:c0:eb:8e:d6:86");
pktgen.range.dst_mac("0", "inc", "00:00:00:00:00:00");

pktgen.range.src_ip("0", "start", "100.100.100.100");
pktgen.range.src_ip("0", "min", "100.100.100.100");
pktgen.range.src_ip("0", "max", "100.100.100.100");
pktgen.range.src_ip("0", "inc", "0.0.0.0");

pktgen.range.dst_ip("0", "start", "0.0.0.0");
pktgen.range.dst_ip("0", "min", "0.0.0.0");
pktgen.range.dst_ip("0", "max", "0.0.0.0");
pktgen.range.dst_ip("0", "inc", "0.0.0.1");


pktgen.range.src_port("0", "start", 1234);
pktgen.range.src_port("0", "min", 1234);
pktgen.range.src_port("0", "max", 1234);
pktgen.range.src_port("0", "inc", 0);

pktgen.range.dst_port("0", "start", 1234);
pktgen.range.dst_port("0", "min", 1234);
pktgen.range.dst_port("0", "max", 1234);
pktgen.range.dst_port("0", "inc", 0);

pktgen.range.pkt_size("0", "start", 82);
pktgen.range.pkt_size("0", "min", 82);
pktgen.range.pkt_size("0", "max", 82);
pktgen.range.pkt_size("0", "inc", 0);

pktgen.range.ip_proto("0", "udp");

pktgen.page("range");

pktgen.set("0", "rate", 1.5);

pktgen.start("0");



