1:/rdpdr.*cliprdr.*rdpsnd/
2:/^\x80[\x01-"`-\x7f\x80-\xa2\xe0-\xff]?..........*\x80/
3:/^\x10\x03...........\x0a\x02.....\x0e/
4:/^..\x02............/
5:/\xffsmb[\x72\x25]/
6:/^220[\x09-\x0d -~]* (e?smtp|simple mail)/
7:/userspace flags=REG_NOSUB REG_EXTENDED/
8:/^\x02\x01\x04.+([\xa0-\xa3]\x02[\x01-\x04].?.?.?.?\x02\x01.?\x02\x01.?\x30|\xa4\x06.+\x40\x04.?.?.?.?\x02\x01.?\x02\x01.?\x43)/
9:/\x05[\x01-\x08]*\x05[\x01-\x08]?.*\x05[\x01-\x03][\x01\x03].*\x05[\x01-\x08]?[\x01\x03]/
10:/^GETMP3\x0d\x0aFilename|^\x01.?.?.?(\x51\x3a\+|\x51\x32\x3a)|^\x10[\x14-\x16]\x10[\x15-\x17].?.?.?.?$/
11:/^(\x05..?|.\x01.[ -~]+\x01F..?.?.?.?.?.?.?)$/
12:/^ssh-[12]\.[0-9]/
13:/^(.?.?\x16\x03.*\x16\x03|.?.?\x01\x03\x01?.*\x0b)/
14:/^[\x01\x02]................?$/
15:/^\x01....\x11\x10........\x01$/
16:/^\( success \( 1 2 \(/
17:/^\xff\xff\xff\xff.....*tfTeam Fortress/
18:/^\xf4\xbe\x03.*teamspeak/
19:/^\xff[\xfb-\xfe].\xff[\xfb-\xfe].\xff[\xfb-\xfe]/
20:/\x03\x9a\x89\x22\x31\x31\x31\.\x30\x30\x20\x42\x65\x74\x61\x20|\xe2\x3c\x69\x1e\x1c\xe9/
21:/^(\x01|\x02)[ -~]*(netascii|octet|mail)/
22:/^t\x03ni.?[\x01-\x06]?t[\x01-\x05]s[\x0a\x0b](glob|who are you$|query data)/
23:/TOR1.*<identity>/
24:/^[\x01-\x13\x16-$]\x01.?.?.?.?.?.?.?.?.?.?[ -~]+/
25:/^\x10here=/
26:/^(.?.?\x16\x03.*\x16\x03|.?.?\x01\x03\x01?.*\x0b).*(thawte|equifax secure|rsa data security, inc|verisign, inc|gte cybertrust root|entrust\.net limited)/
27:/^..?v\$\xcf/
28:/^rfb 00[1-9]\.00[0-9]\x0a$/
29:/^[ !-~]+\x0d\x0a$/
30:/^\x06\xec\x01/
31:/userspace flags=REG_NOSUB/
32:/^\x58\x80........\xf3|^\x06\x58\x4e/
33:/^(ymsg|ypns|yhoo).?.?.?.?.?.?.?[lwt].*\xc0\x80/
34:/^\x1b\xd7\x3b\x48[\x01\x02]\x01?\x01/
35:/^(\x45\x5f\xd0\xd5|\x45\x5f.*0.60(6|8)W)/
36:/^<stream:stream to="gmail\.com"/
37:/User-Agent: DA [678]\.[0-9]/
38:/user-agent: nsplayer/
39:/user-agent: quicktime \(qtver=[0-9].[0-9].[0-9];os=[\x09-\x0d -~]+\)\x0d\x0a/
40:/^\x02\x01\x04.+[\xa0-\xa3]\x02[\x01-\x04].?.?.?.?\x02\x01.?\x02\x01.?\x30/
41:/^\x02\x01\x04.+\xa4\x06.+\x40\x04.?.?.?.?\x02\x01.?\x02\x01.?\x43/
42:/\x4d\x5a(\x90\x03|\x50\x02)\x04/
43:/[FC]WS[\x01-\x09]|FLV\x01\x05\x09/
44:/GIF8(7|9)a/
45:/<html.*><head>/
46:/\xff\xd8/
47:/\x49\x44\x33\x03/
48:/oggs.?.?.?.?.?.?.?.?.?.?.?.?.?.?.?.?.?.?.?.?.?.?.?.?\x01vorbis/
49:/%PDF-1\.[0123456]/
50:/\x89PNG\x0d\x0a\x1a\x0a/
51:/%!ps/
52:/rar\x21\x1a\x07/
53:/\xed\xab\xee\xdb.?.?.?.?[1-7]/
54:/\{\\rtf[12]/
55:/ustar/
56:/pk\x03\x04\x14/
57:/^\x01\x01\x05\x0a/
58:/^(\*[\x01\x02].*\x03\x0b|\*\x01.?.?.?.?\x01)|flapon|toc_signon.*0x/
59:/^ajprot\x0d\x0a/
60:/^\x03[]Z].?.?\x05$/
61:/YCLC_E|CYEL/
62:/^\x01\x11\x10\|\xf8\x02\x10\x40\x06/
63:/^(\x11\x20\x01...?\x11|\xfe\xfd.?.?.?.?.?.?(\x14\x01\x06|\xff\xff\xff))|[]\x01].?battlefield2/
64:/^(\x11\x20\x01\x90\x50\x64\x10|\xfe\xfd.?.?.?\x18|[\x01\\].?battlefield2)/
65:/^\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff..?\x01[\x03\x04]/
66:/^[a-z][a-z0-9]+@[1-9][0-9]+$/
67:/^CTPv1\.[123] Kamusta.*\x0d\x0a$/
68:/\x02[0-4][0-9]:[0-9]+.*\x03$/
69:/^\x01\xf4\x01\xf4/
70:/\x32\x26\x85\x92\x58/
71:/^\xff\xff\xff\xff.*cstrikeCounter-Strike/
72:/^BEGIN (AUTH|VERIFICATION|GSSAPI) REQUEST\x0a/
73:/^\xff\xff\xff\xff.*dodDay of Defeat/
74:/^(longaccoun|qsver2auth|\x35[57]\x30|\+\x10\*)/
75:/^[\x01\x02][\x01- ]\x06.*c\x82sc/
76:/^(\$mynick |\$lock |\$key )/
77:/^\xff\xffchallenge/
78:/^[a-z][a-z0-9\-_]+|login: [\x09-\x0d -~]* name: [\x09-\x0d -~]* Directory: /
79:/^\x01[\x08\x09][\x03\x04]/
80:/^220[\x09-\x0d -~]*ftp/
81:/^gkrellm [23].[0-9].[0-9]\x0a$/
82:/^[\x09-\x0d]*[1-9,+tgi][\x09-\x0d -~]*\x09[\x09-\x0d -~]*\x09[a-z0-9.]*\.[a-z][a-z].?.?\x09[1-9]/
83:/^[\x04\x05]\x0c.i\x01/
84:/^\x03..?\x08...?.?.?.?.?.?.?.?.?.?.?.?.?.?.?\x05/
85:/^\xff\xff\xff\xff.*hl2mpDeathmatch/
86:/^....................TRTPHOTL\x01\x02/
87:/^[1-9][0-9]?[0-9]?[0-9]?[0-9]?[\x09-\x0d]*,[\x09-\x0d]*[1-9][0-9]?[0-9]?[0-9]?[0-9]?(\x0d\x0a|[\x0d\x0a])?$/
88:/^(\* ok|a[0-9]+ noop)/
89:/^(nick[\x09-\x0d -~]*user[\x09-\x0d -~]*:|user[\x09-\x0d -~]*:[\x02-\x0d -~]*nick[\x09-\x0d -~]*\x0d\x0a)/
90:/<stream:stream[\x09-\x0d ][ -~]*[\x09-\x0d ]xmlns=['"]jabber/
91:/^(\x64.....\x70....\x50\x37|\x65.+)/
92:/membername.*session.*player/
93:/^..\x05\x58\x0a\x1d\x03/
94:/^\xff\xff\xff\xffgetstatus\x0a/
95:/^(ver [ -~]*msnftp\x0d\x0aver msnftp\x0d\x0ausr|method msnmsgr:)/
96:/ver [0-9]+ msnp[1-9][0-9]? [\x09-\x0d -~]*cvr0\x0d\x0a$|usr 1 [!-~]+ [0-9. ]+\x0d\x0a$|ans 1 [!-~]+ [0-9. ]+\x0d\x0a$/
97:/^(Public|AES)Key: [0-9a-f]*\x0aEnd(Public|AES)Key\x0a$/
98:/^(.[\x02\x06][!-~]+ [!-~]+ [0-9][0-9]?[0-9]?[0-9]?[0-9]? "[\x09-\x0d -~]+" ([0-9]|10)|1(send|get)[!-~]+ "[\x09-\x0d -~]+")/
99:/\x01\x10\x01|\)\x10\x01\x01|0\x10\x01/
100:/^(dmdt.*\x01.*(""|\x11\x11|uu)|tncp.*33)/
101:/^(20[01][\x09-\x0d -~]*AUTHINFO USER|20[01][\x09-\x0d -~]*news)/
102:/^([\x13\x1b\x23\xd3\xdb\xe3]|[\x14\x1c$].......?.?.?.?.?.?.?.?.?[\xc6-\xff])/
103:/x-openftalias: [-)(0-9a-z ~.]/
104:/^(nq|st)$/
105:/^\x80\x94\x0a\x01....\x1f\x9e/
106:/^(\+ok |-err )/
107:/\x01...\xd3.+\x0c.$/
108:/^.?.?\x02.+\x03$/
109:/^\xff\xff\xff\xffget(info|challenge)/
110:/^\x80\x0c\x01quake\x03/
111:/^\x01\x01(\x08\x08|\x1b\x1b)$/
