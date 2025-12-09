# The Protean Spectre Defense
This repository contains the source code for a prototype of Protean,
as presented in the HPCA'26 paper _Protean: A Programmalbe Spectre Defense_.

Artifact evaluators: please see [this section](#Artifact-Evaluation).

## Artifact Evaluation
Use the following commands to run the artifact evaluation (where `/host/path/to/cpu2006.iso` points to your copy of the SPEC CPU2006 benchmarks ISO image
and `$`/`#` denotes a shell command executed on the host / in the Docker container):
```
$ docker pull nmosier/protean:latest
$ docker run --name protean-container -it nmosier/protean:latest /bin/bash
$ docker cp /host/path/to/cpu2006.iso protean-container:/protean/cpu2006.iso
# ./extract-spec-cpu2006-iso.sh
# ./table-v.py --bench={lbm,hacl.poly1305,bearssl,ossl.bnexp,nginx.c1r1}
# ./table-ii.py --instrumentation=rand
```
Note that the `docker cp` command must be executed in a different host shell, since the previous `docker run` command starts the Docker container.

## Building with Docker
To build Protean with Docker, run the following commands:
```
$ git clone https://github.com/StanfordPLArchSec/protean.git
$ cd protean
$ git submodule update --init --recursive --depth=1
$ ./docker/build.sh
$ ./docker/run.sh
# ./build.sh
```
Note that building everything will take a _long_ time.

## Extending Protean's Evaluation Infrastructure
We implemented an extensive Snakemake-based infrastructure for 
evaluating the performance and security of Protean.
Luckily, this infrastructure can be easily extended to 
[add new benchmarks](https://github.com/StanfordPLArchSec/protean-bench/blob/main/example/README.md)
and evaluate the [performance](https://github.com/StanfordPLArchSec/protean-bench/blob/main/HW-SW.md) and [security](https://github.com/StanfordPLArchSec/protean-amulet/blob/protean/HW-SW.md) of new hardware-software codesigns.
