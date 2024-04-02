cmake_minimum_required(VERSION 3.20)

project(llsct2)

include(ExternalProject)

set(TEST_SUITE_SUBDIR External/SPEC/CINT2017speed CACHE STRING "Subdirectory of SPEC CPU tests to run in LLVM's Test Suite repository")
set(TEST_SUITE_RUN_TYPE test CACHE STRING "SPEC CPU2017 benchmark run size")
set(SIM_BLACKLIST "600.perlbench_s_1;602.gcc_s_0;625.x264_s_0;605.mcf_s_0;631.deepsjeng_s_0" CACHE STRING "simulation blacklist")

# FIXME: Find SCONS.

# GEM5
set(GEM5_BUILD_TYPE fast CACHE BOOL "gem5 build type (options: debug, opt, fast)")
ExternalProject_Add(gem5
  PREFIX gem5
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/gem5
  # GIT_REPOSITORY git@github.com:StanfordPLArchSec/llsct2-gem5.git
  # GIT_TAG develop # TODO: Change this once we get results.
  # GIT_PROGRESS On
  CONFIGURE_COMMAND ""
  BUILD_COMMAND scons --ignore-style build/X86/gem5.${GEM5_BUILD_TYPE} -j $ENV{CMAKE_BUILD_PARALLEL_LEVEL}
  BUILD_IN_SOURCE On
  INSTALL_COMMAND ""
)
ExternalProject_Get_Property(gem5 BINARY_DIR)
set(gem5_BINARY_DIR ${BINARY_DIR})

add_executable(gem5::gem5 IMPORTED GLOBAL)
set(GEM5_BINARY ${gem5_BINARY_DIR}/build/X86/gem5.${GEM5_BUILD_TYPE})
set_target_properties(gem5::gem5 PROPERTIES IMPORTED_LOCATION ${GEM5_BINARY})
add_dependencies(gem5::gem5 gem5)
# FIXME: Use SOURCES instead of DEPENDS + set_target_properties?
set(gem5_se_py ${CMAKE_CURRENT_SOURCE_DIR}/gem5/configs/deprecated/example/se.py)
add_custom_target(gem5_se_py DEPENDS ${gem5_se_py})

# LLVM
ExternalProject_Add(llvm
  PREFIX llvm
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/llvm
  SOURCE_SUBDIR llvm
  CMAKE_ARGS
  -DCMAKE_BUILD_TYPE=Release
  -DLLVM_ENABLE_PROJECTS=clang
  -DLLVM_TARGETS_TO_BUILD=X86
  -DLLVM_INCLUDE_TESTS=Off
  BINARY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/llvm/build
  INSTALL_COMMAND ""
  # BUILD_ALWAYS On
)
ExternalProject_Get_Property(llvm BINARY_DIR)
set(llvm_BINARY_DIR ${BINARY_DIR})

foreach(bin IN ITEMS clang clang++ llvm-lit)
  add_executable(llvm::${bin} IMPORTED GLOBAL)
  set_target_properties(llvm::${bin} PROPERTIES IMPORTED_LOCATION ${llvm_BINARY_DIR}/bin/${bin})
  add_dependencies(llvm::${bin} llvm)
endforeach()


# GLIBC
ExternalProject_Add(glibc
  PREFIX glibc
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/glibc
  CONFIGURE_COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/glibc/configure
      --prefix=${CMAKE_CURRENT_BINARY_DIR}/glibc
      CC=$<TARGET_FILE:llvm::clang>
      CXX=$<TARGET_FILE:llvm::clang++>
  BUILD_COMMAND make -s -j $ENV{CMAKE_BUILD_PARALLEL_LEVEL}
  INSTALL_COMMAND make -s -j $ENV{CMAKE_BUILD_PARALLEL_LEVEL} install
  DEPENDS llvm::clang llvm::clang++ # TODO: will need to add passes here too
  # BUILD_ALWAYS On
)
ExternalProject_Get_Property(glibc INSTALL_DIR)
set(glibc_INSTALL_DIR ${INSTALL_DIR})

add_library(glibc::glibc IMPORTED GLOBAL SHARED)
set_target_properties(glibc::glibc PROPERTIES IMPORTED_LOCATION ${glibc_INSTALL_DIR}/lib/libc.so.6)
add_dependencies(glibc::glibc glibc)

# TEST SUITE
# FIXME: Somehow parameterize which SPEC benchmarks are run.
ExternalProject_Get_Property(glibc INSTALL_DIR)
set(glibc_INSTALL_DIR ${INSTALL_DIR})
set(glibc_LIBRARY_DIR ${glibc_INSTALL_DIR}/lib)
set(glibc_INCLUDE_DIR ${glibc_INSTALL_DIR}/include)
set(glibc_LINKER ${glibc_LIBRARY_DIR}/ld-linux-x86-64.so.2)
set(test_suite_flags "-static -L ${glibc_LIBRARY_DIR} -isystem ${glibc_INCLUDE_DIR} -Wl,--rpath=${glibc_LIBRARY_DIR} -Wl,--dynamic-linker=${glibc_LINKER}")
ExternalProject_Add(test-suite
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/test-suite
  BINARY_DIR ${CMAKE_SOURCE_DIR}/test-suite/build
  CMAKE_ARGS
    -DCMAKE_C_COMPILER=$<TARGET_FILE:llvm::clang>
    -DCMAKE_CXX_COMPILER=$<TARGET_FILE:llvm::clang++>
    -DCMAKE_C_FLAGS=${test_suite_flags}
    -DCMAKE_CXX_FLAGS=${test_suite_flags}
    -DTEST_SUITE_SPEC2017_ROOT=${CMAKE_SOURCE_DIR}/cpu2017
    -DTEST_SUITE_SUBDIRS=External
    -DTEST_SUITE_COLLECT_STATS=Off
    -DTEST_SUITE_COLLECT_CODE_SIZE=Off
    -DTEST_SUITE_RUN_TYPE=${TEST_SUITE_RUN_TYPE}
  INSTALL_COMMAND ""
  DEPENDS llvm::clang llvm::clang++ glibc::glibc
  BUILD_ALWAYS On
)

# Test test suite to make sure everything works
ExternalProject_Get_Property(test-suite BINARY_DIR)
set(test-suite_BINARY_DIR ${BINARY_DIR})
add_custom_target(test_suite_test ALL
  COMMAND llvm::llvm-lit ${test-suite_BINARY_DIR}/${TEST_SUITE_SUBDIR}
  DEPENDS test-suite # NOTE: auto-dep on llvm::llvm-lit I think
) 


# SIM
list(JOIN SIM_BLACKLIST "|" SIM_BLACKLIST)
ExternalProject_Add(sim
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/sim
  BINARY_DIR ${CMAKE_SOURCE_DIR}/sim/build
  LIST_SEPARATOR "|"
  CMAKE_ARGS
    -DTEST_DIR=${test-suite_BINARY_DIR}/${TEST_SUITE_SUBDIR}
    -DGEM5_BINARY=$<TARGET_FILE:gem5::gem5>
    -DGEM5_SE_PY=${gem5_se_py}
    -DTEST_SUITE_BINARY_DIR=${test-suite_BINARY_DIR}
    -DTEST_SUITE_RUN_TYPE=${TEST_SUITE_RUN_TYPE}
    -DSIM_BLACKLIST=${SIM_BLACKLIST}
  INSTALL_COMMAND ""
  DEPENDS test-suite gem5::gem5 gem5_se_py
  BUILD_ALWAYS On
)
add_custom_target(sim_test ALL DEPENDS sim)

  
# # SIMPOINTS
# set(mem_size 8589934592)
# add_test_suite(test-suite-simpoints TEST
#   # RUN_UNDER ${gem5_se_wrapper} $<TARGET_FILE:gem5::gem5> ${gem5_se_py} --cpu-type=X86NonCachingSimpleCPU --mem-size=${mem_size} --simpoint-profile --
#   # DEPENDS gem5_se_wrapper gem5::gem5 gem5_se_py
# )
# ExternalProject_Get_Property(test-suite-simpoints BINARY_DIR)
# foreach(cpu2017_test IN LISTS cpu2017_tests)
#   # TODO
# endforeach()
# add_custom_target(test-suite-simpoints-test ALL DEPENDS test-suite-simpoints)



add_custom_target(glibc_test ALL
  llvm::clang -static -L ${glibc_INSTALL_DIR}/lib -isystem ${glibc_INSTALL_DIR}/include -Wl,--rpath=${glibc_INSTALL_DIR}/lib
  -Wl,--dynamic-linker=${GLIBC_INSTALL_DIR}/lib/ld-linux-x86-64-so.2
  ${CMAKE_CURRENT_SOURCE_DIR}/test.c
)
add_custom_target(gem5_test ALL gem5::gem5 --help >/dev/null)
add_custom_target(clang_test ALL llvm::clang ${CMAKE_CURRENT_SOURCE_DIR}/test.c)

# 



include(cmake/CPM)
include(cmake/IntelPin)
add_subdirectory(tools)
