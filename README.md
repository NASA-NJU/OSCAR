# OSCAR

This repository is the open-source implementation of the OSCAR paper for NSDI'26 with scripts for reproducing figures presented in the paper.

This code repository is based on the open-source [PrioPlus](https://doi.org/10.1145/3689031.3717463) repository (EuroSys'25), which itself is based on the ns-3 codebase internally maintained by NASA-NJU, built on ns-3.40 and developed collaboratively by several NASA-NJU members.

## Code

We have modularized our modifications to ensure compatibility with the original ns3. Our main modules are the `src/dcb` (Data center bridging) module and the `src/json-util` module.

- The `src/dcb` module implements the RoCEv2 protocol stack. The OSCAR congestion control algorithm is implemented in [src/dcb/model/rocev2-oscar.cc](src/dcb/model/rocev2-oscar.cc) and [src/dcb/model/rocev2-oscar.h](src/dcb/model/rocev2-oscar.h).
- The `src/json-util` module includes utility code for easily experimental configuration and result output via JSON.

Furthermore, we have made minor modifications to the original ns3 code, primarily to optimize routing functionality and to change the forwarding table from linear search to hash lookup (based on our experience, this can reduce experiment time by over 40% in topologies with 320 switches).

Demos for part of workloads and congestion control algorithms supported by our code base can be found in `config/demos` with `config/demos/run-all.sh` being the one-click script to run them all.

## Reproduce OSCAR

### Configure the project

Configure ns3 using the following command:

```bash
./ns3 configure -d release --enable-examples --enable-tests --disable-gtk --cmake-prefix-path '/path/to/your/boost/' --enable-mtp
```

Note that the `src/json-util` module relies on the `boost::json` library and requires installing `boost` version 1.75 or higher. If there is an existing version of the boost library in the system that is currently in use and it's inconvenient to configure the PATH, you can directly specify the path to the boost library using `--cmake-prefix-path`.

When debuging your code, you can configure with `-d debug`. The `-d optimized` configuration is not recommended as there are some known platform-specfic bugs. According to our test, `-d optimized` is not much faster than `-d release` on our Intel server.

### Run experiments

The configs are put in `config/oscar-nsdi26/experiments` which can be run by running `config/oscar-nsdi26/experiments/run_all.sh` in one click.

Note that finishing all the experiments needs around 4 hours on our server with Intel 48-core CPU @ 2.2GHz and 256 GB memory. It may take much more time on a desktop PC.

### Plot figures

All the figure codes used in our experiments are placed in `config/oscar-nsdi26/figure-script`. You can reproduce the figure with them. We have attached the reproduced figures in the ipython notebook for your convenience.

## Future Evolution of this Repository

We are still developing new features for our codebase internally, focusing on:

1. Supporting a general flow control framework
2. Supporting advanced AI training and inference workloads (considering integration with SimAI)
3. Addressing known minor issues in the existing framework to make it more robust

In the feature, the main open-source content will be released with our following papers publishing. If you would like to contribute to the codebase, please submit a PR directly, or contact us for more in-depth discussion and collaboration.

## Acknowledgement

Main maintainers and core contributors to this repository:

- Peiwen Yu
- <ins>Zhaochen Zhang</ins>
- Feiyang Xue

(listed in chronological order, underlined names are people currently maintaining the repository)

Other contributors:

- Rui Li
- Songyuan Bai
- Chang Liu
- Chao Yang

## License

This software is licensed under the terms of the GNU General Public License v2.0 only (GPL-2.0-only). See the [LICENSE](./LICENSE) file for more details.

## Citation

If you use this codebase in your work or find our work valuable, please cite this codebase or the paper that open-source this codebase. 

The BibTeX for OSCAR is:

```
@inproceedings {316752,
author = {Zhaochen Zhang and Feiyang Xue and Rui Ning and Keqiang He and Gianni Antichi and Jiaqi Gao and Zhimeng Yin and Kexin Liu and Rui Li and Zhengqi Cui and Zhehao Lin and Peirui Cao and Guihai Chen and Chen Tian},
title = {{OSCAR}: {O(1)-Step} Convergence and Readily-deployable Congestion Control},
booktitle = {23rd USENIX Symposium on Networked Systems Design and Implementation (NSDI 26)},
year = {2026},
isbn = {978-1-939133-54-0},
address = {Renton, WA},
pages = {543--570},
url = {https://www.usenix.org/conference/nsdi26/presentation/zhang-zhaochen},
publisher = {USENIX Association},
month = may
}
```

The BibTeX for PrioPlus is:

```
@inproceedings{10.1145/3689031.3717463,
author = {Zhang, Zhaochen and Xue, Feiyang and He, Keqiang and Yin, Zhimeng and Antichi, Gianni and Gao, Jiaqi and Wang, Yizhi and Ning, Rui and Nan, Haixin and Zhang, Xu and Cao, Peirui and Wang, Xiaoliang and Dou, Wanchun and Chen, Guihai and Tian, Chen},
title = {Enabling Virtual Priority in Data Center Congestion Control},
year = {2025},
isbn = {9798400711961},
publisher = {Association for Computing Machinery},
address = {New York, NY, USA},
url = {https://doi.org/10.1145/3689031.3717463},
doi = {10.1145/3689031.3717463},
booktitle = {Proceedings of the Twentieth European Conference on Computer Systems},
pages = {396–412},
numpages = {17},
keywords = {Congestion control algorithm, Data center network, In-network priority},
location = {Rotterdam, Netherlands},
series = {EuroSys '25}
}
```
