# Yala
Yala is a performance prediction framework for on-NIC NFs, featuring
- multi-resource contention modeling
- traffic-aware modeling

to accurately predict NF performance on SmartNICs under multi-resource contention and varying traffic profiles. 

## 1. Environment
### 1.1. Hardware
- NVIDIA BlueField-2 MBF2H332A-AENOT SmartNIC
#### Hardware configurations
- Regex engine: we turn on the regex engine on BF-2 SmartNIC by
```terminal
systemctl start mlx-regex
systemctl status mlx-regex
```
- Hugepage: we configure the hugepage of BF-2 SmartNCI as follows
```terminal
sudo echo 4096 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
```

### 1.2. Software
- Python 3.8
- Python libraries
    - `scikit-learn`==0.24.2 
    - `numpy`==1.19.5
    - `pandas`==1.1.5
    - `tabulate`==0.9.0
- Traffic generator
    - `DPDK-Pktgen` 23.03.1
- NF frameworks (on SmartNIC)
    - `Click` 2.2 
    - `DPDK` MLNX_DPDK_20.11.6
    - `DOCA` 1.5-LTS

## 2. Example Usage
Here we provide an example workflow of profiling, training and throughput prediction for FlowMonitor. 
We require a host server, a SmartNIC attached to the host and a traffic generator server.

### 2.1. Compilation
Please refer to [Additional tips](#4-additional-tips) for compilation of synthetic benchmarks and NFs.

### 2.2. Offline profiling:

To collect training and testing data for a specific NF, the following steps are required:
- Collecting contention level of synthetic benchmarks
- Co-running target NFs with synthetic benchmarks to obtain throughput under different traffic profiles and contention levels.

#### 2.2.1. Note: before you start
- Scripts may contain hardcoded parameters, e.g. PCIe address is set to `0000:03:00.0` for NFs. Please manually adapt our scripts to your own environment. Some of the fields that should be modified are listed below Please check each script for comments on this.
    - Absolute path
    - Username (DPU, traffic generator)
    - Hostname (DPU, traffic generator)
    - PCIe address
    - Application-specific parameters, e.g. parameters of `DPDK Pktgen`.
- All scripts require root permission. This is because NFs and `DPDK Pktgen` require huge page.
- For scripts that are not mentioned in the documentation, you can refer to the details of them and use them as helpers.   
- In case the profiling can not be done due to environment limitations, e.g. you do not have a BF-2 SmartNIC in hand, we provide example training sets and testing sets of FlowMonitor in `profile/flowmon` so that you can still test model training and throughput prediction (jump to [offline training](#23-offline-training)).

#### 2.2.2. Contention level of synthetic benchmarks
Contention level of `mem-bench` and `regex-bench` can be collected using `script/metric_profile.sh`. In our experiments, we run the following command on the server hosting the SmartNIC.
```bash
bash metric_profile.sh membench
```
This command will invoke `script/metric_profile_mem_snic.sh` on the SmartNIC, which starts `mem-bench` and measures its performance counters with `perf-tools`. Please refer to the script to adjust the three parameters controlling contention level of `mem-bench` (memory access type, memory access speed and size of allocated memory buffer) to your targeted SmartNIC if needed (the current values are used by us).

For `regex-bench`, please run 
```bash
bash metric_profile.sh regbench
```
Its adjustable parameters include match-to-byte-ratio (MtBR) and matching speed.

#### 2.2.3. Co-running NFs with synthetic benchmarks
To start a `Pktgen` on the traffic generator server, run the following command on the host
```bash
bash start_remote_pktgen.sh [ip_addr] [script_name] [script_type]
```
This starts a `Pktgen` instance on the traffic generator server, which sends traffic to the SmartNIC. The IP address of the control interface of traffic generator, name of the traffic profile and file type of the traffic profile should be specified.

To start an NF solo on SmartNIC, run the following command on the host
```terminal
bash sens_profile.sh [nf] solo [folder] 0 0 0 [flowsz] [pktsz] 0
```
This starts the NF on core 0 of SmartNIC. Other parameters (`folder`, `flowsz`, `pktsz`) are used to specify which traffic profile is being used and the output file name.

Then to start a competitor, run the following command on the host
```terminal
bash sens_profile.sh [nf] [competitor] [folder] [type] [rate] [size] [flowsz] [pktsz] [mtbr]
```
This will start a competitor instance and collect the NF performance when contending with this competitor instance.
The type of the competitor is set by `competitor`, and its parameters are jointly set by `type`, `rate`,`size` and `mtbr`.

For example, the following command starts `mem-bench` as the competitor and set its memory access type as 0 (dumb sequential copy), memory access speed as 10000 Kops/s, and memory buffer size as 3 MB. Other parameters are set accordingly.   
```terminal
bash sens_profile.sh flowmon membench example 0 10000 3 16384 1500 0
```

A full profiling can be done by repeatedly invoking `sens_profile.sh` with different parameters (changing the background contention level of target NF). Currently we do not provide the full profiling scheme since it depends on the target NF and SmartNIC. Please derive the profiling scheme based on the adaptive profiling algorithm in our paper based on needs.  

### 2.3. Offline training: 
To train Yala, you need to collect training data that contains the following content in each data entry:
- performance counters
- traffic attributes
- NF throughput

We provide example training sets of FlowMonitor in [`profile/flowmon`](profile/flowmon) for reference. To train Yala using example training set, run following command:
```terminal
cd model
python3 train.py
```
For detailed requirements of training data, please refer to [`model/train.py`](model/train.py) and our paper.

### 2.4. Online prediction:

    Each entry of testing data is similar to training data entries.
    We provide an example testing set of FlowMonitor in `/profile/flowmon` for reference. To use the testing set, run following command:
    ```terminal
    cd model
    python3 predict.py
    ```
    For detailed requirements of testing data, please refer to `/model/predict.py` and our paper.

## 3. Repo Structure
- [`click/`](click/) Source code of Click Modular Router. Note that we add some additional elements to the original version.
- [`model/`](model/) Model training and prediction. 
- [`nfs/`](nfs/) Example network functions.
- [`profile/`](profile/) Example profile of network functions.
- [`rulesets/`](rulesets/) Ruleset for regex accelerator.
- [`script/`](script/) Scripts for profiling contention level and throughput of NFs.
- [`tool/`](tool/) Related tools used by Yala. 
- [`traffic_profile/`](traffic_profile/) Example traffic profiles.

## 4. Additional Tips
### 4.1. Using Synthetic NFs for Benchmarking
- `mem-bench`
```terminal
cd ./nfs/synthetic/membench/
make
taskset -c 0 ./membench -q -t0 -n 0 -r 150000 5
```
The above commnad will start `mem-bench` on core 0. It will do the "dumb-copy" operation `b[i] = a[i]` (`t0` operation) at `150000` Kops/s between two `5` MB arrays.

- `regex-bench`
```terminal
cd ./nfs/synthetic/accbench/
make
taskset -c 0 ./build/accbench -D "-l0 -n 1 -a 03:00.0,class=regex --file-prefix dpdk0" \ 
--input-mode pcap_file -f ../../../traffic_profile/pcap/l7_filter/example.pcap \ 
-d rxp -r ../../../rulesets/l7_filter/build/l7_filter.rof2.binary \ 
-c 1 -s 100 --rate 1 --per-pkt-len
```
The above commnad will start `regex-bench` on core 0. It will send each packet in `example.pcap` to regex accelerator to match against the `l7_filter.rof2.binary` ruleset. Such matching will last for `100` seconds at `1` Gbps.

### 4.2. Collecting Memory-related Performance Counters
Below is an example of collecting runtime performance counters of a `click` process.
Yala uses [`perf-tools`](https://github.com/brendangregg/perf-tools) to collect hardware performance counters. 
Since no counter provides cache occupancy (or equivalent information) on Bluefield-2, Yala leverages a [software-based tool](https://www.brendangregg.com/wss.html) to estimate working set size. 
```terminal
# Hardware performance counter collection
perf stat -p $(pidof -s click) -e cycles,instructions,inst_retired,l2d_cache_rd,l2d_cache_wr,l2d_cache,mem_access_rd,mem_access_wr sleep 3
# Working set size estimation
./tool/wss/wss.pl $(pidof -s click) 3 
```
### 4.3. NFs
#### 4.3.1. Compile Click-DPDK
```terminal
cd click
./configure --enable-dpdk --enable-user-multithread --enable-local --disable-linuxmodule 
make elemlist
make
make install
```
<!-- #### Compile DOCA Samples

#### Compile DPDK Pipeline -->

## 5. Ackonwledgement
We list open-source projects used by us and our modifications to them (if any).
- Click
    - Directory: [`click/`](click/)
    - Related projects
    - [Click modular router](https://github.com/kohler/click)
      - Version: 2.2
    - Modifications
      - (+) Two hardware-based Click elements
        - `RegexMatch` 
        - `Compress`
- Accbench (regex-bench & compress-bench)
    - Directory: [`nfs/synthetic/accbench/`](nfs/synthetic/accbench/)
    - Reference projects
        - [RXPbench](https://docs.nvidia.com/doca/archive/doca-v1.5.0/rxpbench/index.html)
          - Version: 22.10          
- Membench
    - Directory: [`nfs/synthetic/membench/`](nfs/synthetic/membench/)
    - Reference projects
        - [Memory bandwidth benchmark](https://github.com/raas/mbw)
            - Version: 1.0
        - [Stree-ng](https://github.com/ColinIanKing/stress-ng)
- [Working set size estimation](https://www.brendangregg.com/wss.html)
    - Directory: [`tool/wss`](tool/wss)
