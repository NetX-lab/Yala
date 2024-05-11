# Tomur
Tomur is a performance prediction framework for on-NIC NFs, featuring
- multi-resource contention modeling
- traffic-aware modeling

to accurately predict NF performance under multi-resource contention and varying traffic profiles. 

<!-- refer to our paper at xxx -->

## Environment
### Hardware
- NVIDIA BlueField-2 MBF2H332A-AENOT SmartNIC

### Software
- Python 3.8
- Python libraries
    - `scikit-learn`==0.24.2 
    - `numpy`==1.19.5
    - `pandas`==1.1.5
    - `tabulate`==0.9.0
- Traffic generator
    - `DPDK-Pktgen` 23.03.1
- NF frameworks
    - `Click` 2.1 
    - `DPDK` MLNX_DPDK_20.11.6
    - `DOCA` 1.5-LTS

## Usage
- Offline profiling/training: 

    To train Tomur, you need to collect training data that contains the following content in each data entry:
    - performance counters
    - traffic attributes
    - NF throughput

    We provide example training sets of FlowMonitor in `/profile/flowmon` for reference. To train Tomur using example training set, run following command:
    ```terminal
    cd model
    python3 train.py
    ```
    For detailed requirements of training data, please refer to `/model/train.py` and our paper.
- Online prediction:

    Each entry of testing data is similar to training data entries.
    We provide an example testing set of FlowMonitor in `/profile/flowmon` for reference. To use the testing set, run following command:
    ```terminal
    cd model
    python3 predict.py
    ```
    For detailed requirements of testing data, please refer to `/model/predict.py` and our paper.

## Repo Structure
- `click/` Source code of Click Modular Router. Note that we add some additional elements to the original version.
- `model/` Model training and prediction. 
- `nfs/` Example network functions.
- `profile/` Example profile of network functions.
- `rulesets/` Ruleset for regex accelerator.
- `tool/` Related tools used by Tomur. 
- `traffic_profile/` Example traffic profiles.

## Additional Tips
### Use Synthetic Competitor
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
taskset -c 0 ./accbench/build/accbench -D "-l0 -n 1 -a 03:00.0,class=regex --file-prefix dpdk0" \ 
--input-mode pcap_file -f ../../../traffic_profile/pcap/l7_filter/example.pcap \ 
-d rxp -r ../rulesets/l7_filter/build/l7_filter.rof2.binary \ 
-c 1 -s 100 --rate 1 --per-pkt-len
```
The above commnad will start `regex-bench` on core 0. It will send each packet in `example.pcap` to regex accelerator to match against the `l7_filter.rof2.binary` ruleset. Such matching will last for `100` seconds at `1` Gbps.

### Collecting Memory-related Performance Counters
Below is an example of collecting runtime performance counters of a `click` process.
Tomur uses [`perf-tools`](https://github.com/brendangregg/perf-tools) to collect hardware performance counters. 
Since no counter provides cache occupancy (or equivalent information) on Bluefield-2, Tomur leverages a [software-based tool](https://www.brendangregg.com/wss.html) to estimate working set size. 
```terminal
# Hardware performance counter collection
perf stat -p $(pidof -s click) -e cycles,instructions,inst_retired,l2d_cache_rd,l2d_cache_wr,l2d_cache,mem_access_rd,mem_access_wr sleep 3
# Working set size estimation
../tool/wss/wss.pl $(pidof -s click) 3 
```
### NFs
#### Compile Click-DPDK
```terminal
cd click
./configure --enable-dpdk --enable-user-multithread --enable-local --disable-linuxmodule 
make elemlist
make
make install
```
<!-- #### Compile DOCA Samples

#### Compile DPDK Pipeline -->