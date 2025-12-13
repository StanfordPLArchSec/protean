from util.suite import (
    Suite,
    WebserverSuite,
)
from util.bench import Bench
from contextlib import chdir
from util.util import run_if_requested

suites = [
    Suite(
        name = "arch-wasm",
        benches = [
            Bench("bzip2", "wasm.401.bzip2"),
            Bench("mcf", "wasm.429.mcf"),
            Bench("milc", "wasm.433.milc"),
            Bench("namd", "wasm.444.namd"),
            Bench("libquantum", "wasm.462.libquantum"),
            Bench("lbm", "wasm.470.lbm"),
        ],
        baseline = "stt.atret",
        protcc = "base",
        group = "base",
    ),
    Suite(
        name = "cts-crypto",
        benches = [
            Bench("hacl.chacha20", "ctsbench.hacl.chacha20"),
            Bench("hacl.curve25519", "ctsbench.hacl.curve25519"),
            Bench("hacl.poly1305", "ctsbench.hacl.poly1305"),
            Bench("sodium.salsa20", "ctsbench.libsodium.salsa20"),
            Bench("sodium.sha256", "ctsbench.libsodium.sha256"),
            Bench("ossl.chacha20", "ctsbench.openssl.chacha20"),
            Bench("ossl.curve25519", "ctsbench.openssl.curve25519"),
            Bench("ossl.sha256", "ctsbench.openssl.sha256"),
        ],
        baseline = "spt.atret",
        protcc = "cts",
        group = "ctsbench",
    ),
    Suite(
        name = "ct-crypto",
        benches = [
            Bench("bearssl", "bearssl"),
            Bench("ctaes", "ctaes"),
            Bench("djbsort", "djbsort"),
        ],
        baseline = "spt.atret",
        protcc = "ct",
        group = "ctbench",
    ),
    Suite(
        name = "unr-crypto",
        benches = [
            Bench("ossl.bnexp", "nctbench.openssl.bnexp"),
            Bench("ossl.dh", "nctbench.openssl.dh"),
            Bench("ossl.ecadd", "nctbench.openssl.ecadd"),
        ],
        baseline = "sptsb.atret",
        protcc = "nct",
        group = "nctbench",
    ),
    WebserverSuite(
        name = "webserver",
        benches = [
            Bench(f"nginx.c{c}r{r}", f"c{c}r{r}")
            for c, r in [(1, 1), (2, 2), (1, 4), (4, 1), (4, 4)]
        ],
        baseline = "sptsb.atret",
        protcc = "nct.ossl-annot",
    )
]

def get_results_for_benchmarks(bench_list, args):
    # Construct a list of targets.
    targets = []
    for bench in bench_list:
        targets.extend(bench.targets())

    with chdir("bench"):
        # Run results.
        run_if_requested(args, [*args.snakemake_command, *targets])

        # Read in results.
        for bench in bench_list:
            unsafe = bench.perf_unsafe()
            baseline = bench.perf_baseline()
            protdelay = bench.perf_protdelay()
            prottrack = bench.perf_prottrack()
            assert unsafe != 0
            assert baseline != 0
            assert protdelay != 0
            assert prottrack != 0
            bench.results = [
                baseline / unsafe,
                protdelay / unsafe,
                prottrack / unsafe,
            ]

