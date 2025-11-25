PTEX_ROOT = $(HOME)/mieros
LLVM_ROOT = $(PTEX_ROOT)/llvm
LLVM_PTEX = $(LLVM_ROOT)/ptex-17/build
LLVM_BASE = $(LLVM_ROOT)/base-17/build

libsodium/build:
	mkdir libsodium/build
	cd libsodium/build && ../src/configure --prefix=$PWD/.. CC=$(LLVM_PTEX) LD=
