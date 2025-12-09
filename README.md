# The Protean Spectre Defense
This repository contains the source code for a prototype of Protean,
as presented in the HPCA'26 paper _Protean: A Programmalbe Spectre Defense_.

Artifact evaluators: please see [this section](#Artifact-Evaluation).

## Artifact Evaluation
Use the following commands to run the artifact evaluation (where `/host/path/to/cpu2006.iso` points to your copy of the SPEC CPU2006 benchmarks ISO image
and `$`/`#` denotes a shell command executed on the host / in the Docker container):
```sh
$ curl -L https://zenodo.org/records/17857896?preview=1&token=eyJhbGciOiJIUzUxMiJ9.eyJpZCI6ImQzYTlhMjE3LWFjNzEtNGJiNC1iZWNkLTg1Y2Y1ZmE4NmE3MyIsImRhdGEiOnt9LCJyYW5kb20iOiJjMzBjZGFiZjg3NGJlMzUxZTFmNmEwMzI4MDIxNTRhZiJ9._yZvid8Wm9o7c3d5jar_f0t90myhGMcDTu0U7-MWacq3uWci7Wo6QKp2fOTpIFkD3qCNyTjV0F2peKokd74rTg | docker load
$ docker run --name protean-container -it protean:latest
$ docker cp /host/path/to/cpu2006.iso protean-container:/protean/cpu2006.iso
$ docker exec -it protean-container /bin/bash
# ./extract-spec-cpu2006-iso.sh
# ./table-v.py --bench={lbm,hacl.poly1305,bearssl,ossl.bnexp,nginx.c1r1}
# ./table-ii.py --instrumentation=rand
```

## Building with Docker
To build Protean with Docker, run the following commands:
```sh
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
