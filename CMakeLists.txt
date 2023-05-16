cmake_minimum_required(VERSION 3.10)

project(llsct2)

include(ExternalProject)

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
set_target_properties(gem5::gem5 PROPERTIES IMPORTED_LOCATION ${gem5_BINARY_DIR}/build/X86/gem5.${GEM5_BUILD_TYPE})
add_dependencies(gem5::gem5 gem5)

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
)
ExternalProject_Get_Property(llvm BINARY_DIR)
set(llvm_BINARY_DIR ${BINARY_DIR})

foreach(bin IN ITEMS clang clang++)
  add_executable(llvm::${bin} IMPORTED GLOBAL)
  set_target_properties(llvm::${bin} PROPERTIES IMPORTED_LOCATION ${llvm_BINARY_DIR}/bin/${bin})
  add_dependencies(llvm::${bin} llvm)
endforeach()

add_custom_target(gem5_test ALL gem5::gem5 --help)
add_custom_target(clang_test ALL llvm::clang ${CMAKE_CURRENT_SOURCE_DIR}/test.c)
