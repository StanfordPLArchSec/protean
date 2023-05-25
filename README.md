# LLSCT2 

## Prerequisites
```sh
brew install cmake p7zip
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
mkdir build
cd build
cmake ../llvm -DCMAKE_BUILD_TYPE=Release \
              -DLLVM_ENABLE_PROJECTS=clang \
		      -DLLVM_TARGETS_TO_BUILD=X86 \
			  -DLLVM_INCLUDE_TESTS=Off
ninja
LLVM=$PWD
cd ../..
```

### Building gem5
```sh
cd gem5
scons --ignore-style -j $(nproc) build/X86/gem5.fast
export GEM5=$PWD
cd ..
```

### Building glibc
```sh
cd glibc
mkdir build
cd build
glibc_flags="-O3"
GLIBC=$PWD/../install
../configure --prefix=$GLIBC CC=$LLVM/bin/clang CXX=$LLVM/bin/clang++ CFLAGS="$flags" CXXFLAGS="$flags"
make -s -j$(nproc)
make -s -j$(nproc) install
cd ../..
```

### Configuring SPEC CPU2017 benchmarks
1. First, get the `cpu2017-1.1.0.iso` image. Let `$SPEC_ISO` denote the path to this ISO image.
2. 
```sh
mkdir cpu2017
cd cpu2017
7z e $SPEC_ISO install_archives/cpu2017.tar.xz
tar -xf cpu2017.tar.xz
yes | ./install.sh
SPEC=$PWD
cd ..
```

### Building test-suite
```sh
mkdir test-suite/build
cd test-suite/build
test_suite_flags="-static -L $GLIBC/lib -isystem $GLIBC/include -Wl,--rpath=$GLIBC/lib -Wl,--dynamic-linker=$GLIBC/lib/ld-linux-x86-64.so.2"
cmake .. \
      -DCMAKE_C_COMPILER=$LLVM/bin/clang \
	  -DCMAKE_CXX_COMPILER=$LLVM/bin/clang++ \
	  -DCMAKE_C_FLAGS="$test_suite_flags" \
	  -DCMAKE_CXX_FLAGS="$test_suite_flags" \
	  -DTEST_SUITE_SPEC2017_ROOT=$SPEC \
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
mkdir valgrind/build
cd valgrind/build
../configure --prefix=$PWD/../install
make -s -j$(nproc)
make -s -j$(nproc) install
VALGRIND=$PWD/../install/bin/valgrind
cd ../../
```

### Building SimPoint
```sh
cd simpoint
make -s -j$(nproc)
SIMPOINT=$PWD/bin/simpoint
```

