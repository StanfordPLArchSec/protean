llvm:
	git clone https://github.com/StanfordPLArchSec/protean-llvm.git -b hpca26/ptex-17 "$@"

test-suite:
	git clone https://github.com/StanfordPLArchSec/protean-llvm-test-suite.git -b protean "$@"

gem5/protean:
	git clone https://github.com/StanfordPLArchSec/protean-gem5.git -b hpca26/protean "$@"

gem5/%: gem5/protean
	git -C "$<" worktree add ../$* hpca26/$*

amulet:
	git clone https://github.com/StanfordPLArchSec/protean-amulet.git -b protean "$@"

amulet/gem5/%: gem5/protean
	git -C "$<" worktree add ../../amulet/gem5/$* hpca26/amulet/$* 

GEM5_BRANCHES = protean protean-se base base-se stt spt spt-se
GEM5_DIRS = $(addprefix gem5/,$(GEM5_BRANCHES))

GEM5_AMULET_BRANCHES = protean base stt spt
GEM5_AMULET_DIRS = $(addprefix amulet/gem5/,$(GEM5_AMULET_BRANCHES))

.PHONY: clone
clone: llvm test-suite $(GEM5_DIRS) amulet $(GEM5_AMULET_DIRS)
