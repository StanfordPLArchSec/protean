# The Protean Spectre Defense (Artifact) 
This repository serves as an artifact for our HPCA'26 paper
_Protean: A Programmable Spectre Defense_.


# New Build instructions

```sh
# Build all gem5 instances (Protean, unsafe baseline, secure baselines, Amulet models). 
# Takes many hours.
./build-gem5.sh
```

# SPEC CPU2017 and CPU2006 Benchmarks
Because these benchmarks are properietary...
We provide scripts for automatically extracting the SPEC CPU2017 and CPU2006 benchmarks
from their ISOs (you must provide the ISO).

```sh
./extract-spec-cpu2017-iso.sh /path/to/cpu2017.iso
```
We have tested with `cpu2017-1.1.0.iso` (i.e., SPEC CPU2017 v1.1.0), specifically.
We have not tested other versions.

To extract




# OLD OLD OLD: IGNORE

# FIXME: Update.
# LLSCT2 

## Prerequisites
```sh
brew install cmake p7zip binutils gawk llvm
```

## Cloning
```sh
git clone https://github.com/StanfordPLArchSec/llsct2
git submodule init
git submodule update
LLSCT=$PWD     # set the root dir for LLSCT
```
This will probably take a few minutes.

## Building

First, make sure that the default generator for CMake is set to Ninja:
```sh
export CMAKE_GENERATOR=Ninja
```
### Building LLVM
```sh

cd llvm
cmake -S llvm -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DLLVM_ENABLE_PROJECTS='clang;lld' \
      -DLLVM_TARGETS_TO_BUILD=X86 \
      -DLLVM_INCLUDE_TESTS=Off \
      -DLLVM_BINUTILS_INCDIR=$(brew --prefix binutils)/include \
      -DLLVM_USE_LINKER=lld
cmake --build build
cd ..
```

### Building gem5
```sh
cd gem5
scons --ignore-style -j $(nproc) build/X86/gem5.fast
cd ..
```

### Building glibc
```sh
cd glibc && mkdir build && cd build
glibc_flags="-O3"
../configure --prefix=$LLSCT/glibc/install CC=$LLSCT/llvm/build/bin/clang CXX=$LLSCT/llvm/build/bin/clang++ CFLAGS="$glibc_flags" CXXFLAGS="$glibc_flags"
make -s -j$(nproc)
make -s -j$(nproc) install
cd ../..
```

### Configuring SPEC CPU2017 benchmarks
1. First, get the `cpu2017-1.1.0.iso` image. Let `$SPEC_ISO` denote the path to this ISO image.
2. 
```sh
mkdir cpu2017 && cd cpu2017
7z e $SPEC_ISO install_archives/cpu2017.tar.xz
tar -xf cpu2017.tar.xz
yes | ./install.sh
SPEC=$PWD
cd ..
```

### Building test-suite
```sh
mkdir test-suite/build && cd test-suite/build
test_suite_cflags=""
test_suite_ldflags="-static -L $LLSCT/glibc/install/lib -Wl,--rpath=$LLSCT/glibc/install/lib -Wl,--dynamic-linker=$LLSCT/glibc/install/lib/ld-linux-x86-64.so.2 /usr/lib/x86_64-linux-gnu/libc_nonshared.a"
cmake ..  -DCMAKE_C_COMPILER=$LLVM/bin/clang \
	  -DCMAKE_CXX_COMPILER=$LLVM/bin/clang++ \
	  -DCMAKE_C_FLAGS="$test_suite_cflags" \
	  -DCMAKE_CXX_FLAGS="$test_suite_cflags" \
	  -DCMAKE_EXE_LINKER_FLAGS="$test_suite_ldflags" \
	  -DCMAKE_SHARED_LINKER_FLAGS="$test_suite_ldflags" \
	  -DTEST_SUITE_SPEC2017_ROOT=$LLSCT/cpu2017 \
	  -DTEST_SUITE_SUBDIRS=External \
	  -DTEST_SUITE_COLLECT_STATS=Off \
	  -DTEST_SUITE_COLLECT_CODE_SIZE=Off \
	  -DTEST_SUITE_RUN_TYPE=test
ninja
TEST_SUITE=$PWD
cd ../..
```

### Building valgrind
We will use valgrind for profiling the SPEC CPU2017 benchmarks to produce basic block vector (BBV) files, which we will pass to SimPoint for analysis.
```sh
cd valgrind
./autogen.sh
mkdir build
cd build
../configure --prefix=$PWD/../install
make -s -j$(nproc)
make -s -j$(nproc) install
cd ../../
```

### Building SimPoint
```sh
cd simpoint
make -s -j$(nproc)
```

