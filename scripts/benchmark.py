import os
import re
from typing import List, Optional

class Benchmark:
    class Executable:
        def __init__(self, name: str, path: str, wd: str):
            self.name = name
            self.path = path
            self.wd = wd
    
    class Input:
        def __init__(self, name: str, args: str, stdin: str, mem_size: str, stack_size: str):
            self.name = name
            self.args = args
            self.mem_size = mem_size
            self.stack_size = stack_size
            self.stdin = stdin

    def __init__(self, name: str):
        self.name = name
        self.exes = []
        self.inputs = []

    def add_executable(self, name: str, path: str, wd: str):
        self.exes.append(Benchmark.Executable(
            name = name,
            path = path,
            wd = wd,
        ))
        return self

    def add_input(self, args: str = "", stdin: str = "/dev/null", mem_size: str = "512MiB", stack_size: str = "8MiB"):
        self.inputs.append(Benchmark.Input(
            name = str(len(self.inputs)),
            args = args,
            stdin = stdin,
            mem_size = mem_size,
            stack_size = stack_size,
        ))
        return self

# class MultiBinaryBenchmark:
#     def __init__(self, leader: Benchmark):
#         self.benchmarks = [leader]
# 
#     def leader() -> Benchmark:
#         return self.benchmarks[0]
#         
#     def add_benchmark(self, benchmark: Benchmark):
#         self.benchmarks.append(benchmark)

class CPU2017IntBenchmark(Benchmark):
    def __init__(self, root: str, name: str):
        super().__init__(exe = os.path.join(root, "External", "SPEC", "CINT2017speed", name, name),
                         name = name,
                         wd = os.path.join(root, "External", "SPEC", "CINT2017speed", name, "run_ref"))

def make_spec_bench(roots, full_name: str, kind: str) -> Benchmark:
    assert kind in ["FP", "INT"]
    match = re.match(r"\d{3}\.(\w+)_s", full_name)
    assert match
    name = match.group(1)
    bench = Benchmark(name = name)
    for rootname, root in roots.items():
        dir = os.path.join(root, "External", "SPEC", f"C{kind}2017speed", full_name)
        bench.add_executable(
            name = rootname,
            path = os.path.join(dir, full_name),
            wd = os.path.join(dir, "run_ref"),
        )
    return bench

def get_cpu2017_int(roots) -> list:
    def make_bench(full_name: str):
        return make_spec_bench(roots, full_name, "INT")

    perlbench = make_bench("600.perlbench_s")
    perlbench.add_input("-I./lib checkspam.pl 2500 5 25 11 150 1 1 1 1")
    perlbench.add_input("-I./lib diffmail.pl 4 800 10 17 19 300")
    perlbench.add_input("-I./lib splitmail.pl 6400 12 26 16 100 0")

    gcc = make_bench("602.gcc_s")
    gcc.add_input("gcc-pp.c -O5 -fipa-pta -o gcc-pp.opts-O5_-fipa-pta.s", mem_size = "16GiB")
    gcc.add_input("gcc-pp.c -O5 -finline-limit=1000 -fselective-scheduling -fselective-scheduling2 -o gcc-pp.opts-O5_-finline-limit_1000_-fselective-scheduling_-fselective-scheduling2.s", mem_size = "4GiB")
    gcc.add_input("gcc-pp.c -O5 -finline-limit=24000 -fgcse -fgcse-las -fgcse-lm -fgcse-sm -o gcc-pp.opts-O5_-finline-limit_24000_-fgcse_-fgcse-las_-fgcse-lm_-fgcse-sm.s", mem_size = "4GiB")

    mcf = make_bench("605.mcf_s").add_input("inp.in", mem_size = "16GiB")

    omnetpp = make_bench("620.omnetpp_s").add_input("-c General -r 0")

    xalancbmk = make_bench("623.xalancbmk_s").add_input("-v t5.xml xalanc.xsl")

    x264 = make_bench("625.x264_s")
    x264.add_input("--pass 1 --stats x264_stats.log --bitrate 1000 --frames 1000 -o BuckBunny_New.264 BuckBunny.yuv 1280x720")
    # FIXME: Need to add dependency support in pydoit. The next command depends on an implicit output of the previous command.
    # This should actually be pretty easy to handle...
    # x264.add_input("--pass 2 --stats x264_stats.log --bitrate 1000 --dumpyuv 200 --frames 1000 -o BuckBunny_New.264 BuckBunny.yuv 1280x720")
    # x264.add_input("--seek 500 --dumpyuv 200 --frames 1250 -o BuckBunny_New.264 BuckBunny.yuv 1280x720")
    
    deepsjeng = make_bench("631.deepsjeng_s").add_input("ref.txt", mem_size = "8GiB")

    leela = make_bench("641.leela_s").add_input("ref.sgf")

    exchange2 = make_bench("648.exchange2_s").add_input("6")

    xz = make_bench("657.xz_s")
    xz.add_input("cpu2006docs.tar.xz 6643 055ce243071129412e9dd0b3b69a21654033a9b723d874b2015c774fac1553d9713be561ca86f74e4f16f22e664fc17a79f30caa5ad2c04fbc447549c2810fae 1036078272 1111795472 4", mem_size = "32GiB")
    xz.add_input("cld.tar.xz 1400 19cf30ae51eddcbefda78dd06014b4b96281456e078ca7c13e1c0c9e6aaea8dff3efb4ad6b0456697718cede6bd5454852652806a657bb56e07d61128434b474 536995164 539938872 8", mem_size = "8GiB")

    benches = [
        perlbench,
        gcc,
        mcf,
        omnetpp,
        xalancbmk,
        x264,
        deepsjeng,
        leela,
        exchange2,
        xz,
    ]
    assert len(benches) == 10
    return benches



def get_cpu2017_fp(roots) -> list:
    def make_bench(full_name: str):
        return make_spec_bench(roots, full_name, "FP")

    bwaves = make_bench("603.bwaves_s")
    bwaves.add_input(stdin = "bwaves_1.in", mem_size = "16GiB", stack_size = "16GiB") \
          .add_input(stdin = "bwaves_2.in", mem_size = "16GiB", stack_size = "16GiB")

    cactuBSSN = make_bench("607.cactuBSSN_s").add_input("spec_ref.par", mem_size = "8GiB")

    lbm = make_bench("619.lbm_s").add_input("2000 reference.dat 0 0 200_200_260_ldc.of", mem_size = "4GiB")

    wrf = make_bench("621.wrf_s").add_input(stack_size = "128MiB")

    cam4 = make_bench("627.cam4_s").add_input(mem_size = "1GiB", stack_size = "128MiB")

    # FIXME: Re-enable pop2. Currently not working.
    # pop2 = make_bench("628.pop2_s").add_input(mem_size = "2GiB")

    imagick = make_bench("638.imagick_s").add_input(
        "-limit disk 0 refspeed_input.tga -resize 817% -rotate -2.76 -shave 540x375 -alpha remove -auto-level " \
        "-contrast-stretch 1x1% -colorspace Lab -channel R -equalize +channel -colorspace sRGB -define histogram:unique-colors=false " \
        "-adaptive-blur 0x5 -despeckle -auto-gamma -adaptive-sharpen 55 -enhance -brightness-contrast 10x10 -resize 30% refspeed_output.tga",
        mem_size = "8GiB",
    )

    nab = make_bench("644.nab_s").add_input("3j1n 20140317 220", mem_size = "1GiB")

    fotonik3d = make_bench("649.fotonik3d_s").add_input(mem_size = "16GiB")

    roms = make_bench("654.roms_s").add_input(stdin = "ocean_benchmark3.in.x", mem_size = "16GiB", stack_size = "64MiB")

    benches =  [
        bwaves,
        cactuBSSN,
        lbm,
        wrf,
        cam4,
        # pop2,
        imagick,
        nab,
        fotonik3d,
        roms,
    ]
    # REVERTME
    assert len(benches) == 9
    return benches
