cmake_minimum_required(VERSION 3.20)

project(llsct2-sim)

set(TEST_SUITE_ROOT "" CACHE PATH "Path to test suite directory containing *.test files")
if(NOT TEST_SUITE_ROOT)
  message(FATAL_ERROR "TEST_SUITE_ROOT unset")
endif()
set(TEST_SUITE_SUBDIR "External/SPEC/CINT2017speed" CACHE PATH "Path to subdirectory containing relevant tests.")
set(LLVM_LIT "" CACHE FILEPATH "Path to llvm-lit executable (built by LLVM)")
if(NOT LLVM_LIT)
  message(FATAL_ERROR "LLVM_LIT unset")
endif()
set(GEM5_COMMAND "" CACHE INTERNAL "gem5 command prefix")
if(NOT GEM5_COMMAND)
  message(FATAL_ERROR "GEM5_COMMAND unset")
endif()
set(TEST_SUITE_RUN_TYPE "test" CACHE STRING "See LLVM's Test Suite's TEST_SUITE_RUN_TYPE")

# Copy over site configs?
file(GLOB paths_to_copy ${TEST_SUITE_ROOT}/*)
if(NOT paths_to_copy)
  message(FATAL_ERROR "paths_to_copy empty??? (TEST_SUITE_ROOT=${TEST_SUITE_ROOT})")
endif()
file(COPY ${paths_to_copy} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
 #file(COPY ${TEST_SUITE_ROOT}/lit.cfg ${TEST_SUITE_ROOT}/lit.site.cfg DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Separate test files
file(GLOB_RECURSE testpaths ${CMAKE_CURRENT_BINARY_DIR}/${TEST_SUITE_SUBDIR} *.test)
if(NOT testpaths)
  message(FATAL_ERROR "testpaths empty??? In path ${CMAKE_CURRENT_BINARY_DIR}/${TEST_SUITE_SUBDIR}/*.test ")
endif()
foreach(testpath IN LISTS testpaths)
  # FIXME: Find python3.
  execute_process(COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/separate_tests.py ${testpath}
    OUTPUT_VARIABLE new_testpaths
    COMMAND_ERROR_IS_FATAL ANY
  )
endforeach()

# Run lit without 'run under'.
file(GLOB_RECURSE testpaths ${CMAKE_CURRENT_BINARY_DIR}/${TEST_SUITE_SUBDIR} *.test)
if(NOT testpaths)
  message(FATAL_ERROR "testpaths empty???")
endif()
foreach(testpath IN LISTS testpaths)
  get_filename_component(testname ${testpath} NAME_WLE)
  add_custom_target(${testname} ALL
    COMMAND ${LLVM_LIT} ${testpath}
    DEPENDS ${testpath}
  )
endforeach()

# Run lit with 'run under' simpoints.
set(mem_size 8589934592)
execute_process(
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/generate_lit_cfg.py --
    ${GEM5_COMMAND} --cpu-type=X86NonCachingSimpleCPU --mem-size=${mem_size} --simpoint-profile --
  INPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/lit.site.cfg
  OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/simpoints.lit.site.cfg
  COMMAND_ERROR_IS_FATAL ANY
)
file(COPY_FILE ${CMAKE_CURRENT_BINARY_DIR}/lit.cfg ${CMAKE_CURRENT_BINARY_DIR}/simpoints.lit.cfg)
if(NOT testpaths)
  message(FATAL_ERROR "testpaths is empty???")
endif()
foreach(testpath IN LISTS testpaths)
  # message("1: testpath = ${testpath}")
  get_filename_component(testname ${testpath} NAME_WLE)
  # message("2: testname = ${testname}")
  get_filename_component(testdir ${testpath} DIRECTORY)
  # message("3: testdir = ${testdir}")
  set(m5out ${testdir}/run_${TEST_SUITE_RUN_TYPE}/m5out-${testname})
  # message("4: m5out = ${m5out}")
  add_custom_command(OUTPUT ${m5out}/simpoint.bb.gz
    COMMAND ${LLVM_LIT} --config-prefix=simpoints.lit ${testpath}
  )
  add_custom_target(${testname}_simpoint_bb_gz ALL DEPENDS ${m5out}/simpoint.bb.gz)
endforeach()
