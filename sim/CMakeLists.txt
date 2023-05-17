cmake_minimum_required(VERSION 3.20)

project(llsct2-sim)

set(TEST_SUITE_ROOT "" CACHE PATH "Path to test suite directory containing *.test files")
if(NOT TEST_SUITE_ROOT)
  message(FATAL_ERROR "TEST_SUITE_ROOT unset")
endif()
set(TEST_SUITE_SUBDIR "External/SPEC/CINT2017speed" CACHE STRING "Path to subdirectory containing relevant tests.")
set(LLVM_LIT "" CACHE FILEPATH "Path to llvm-lit executable (built by LLVM)")
if(NOT LLVM_LIT)
  message(FATAL_ERROR "LLVM_LIT unset")
endif()
set(GEM5_COMMAND "" CACHE STRING "gem5 command prefix")
if(NOT GEM5_COMMAND)
  message(FATAL_ERROR "GEM5_COMMAND unset")
endif()
set(TEST_SUITE_RUN_TYPE "test" CACHE STRING "See LLVM's Test Suite's TEST_SUITE_RUN_TYPE")

# Copy over site configs?
file(GLOB paths_to_copy ${TEST_SUITE_ROOT}/*)
set(our_test_suite ${CMAKE_CURRENT_BINARY_DIR}/test-suite)
make_directory(${our_test_suite})
file(COPY ${paths_to_copy} DESTINATION ${our_test_suite})

# Separate test files
# TODO: Fix glob crap.
file(GLOB_RECURSE testpaths ${our_test_suite}/${TEST_SUITE_SUBDIR}/*.test)
foreach(testpath IN LISTS testpaths)
  # FIXME: Find python3.
  execute_process(COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/separate_tests.py ${testpath}
    OUTPUT_VARIABLE new_testpaths
    COMMAND_ERROR_IS_FATAL ANY
  )
endforeach()

# Run lit without 'run under'.
file(GLOB_RECURSE testpaths ${our_test_suite}/${TEST_SUITE_SUBDIR}/*.test)
add_custom_target(sim_host_tests ALL
  COMMAND ${LLVM_LIT} -vva ${testpaths}
  DEPENDS ${testpaths}
)
# foreach(testpath IN LISTS testpaths)
#   get_filename_component(testname ${testpath} NAME_WLE)
#   add_custom_target(${testname} ALL
#     COMMAND ${LLVM_LIT} -vva ${testpath}
#     DEPENDS ${testpath}
#   )
# endforeach()

# Run lit with 'run under' simpoints.
set(mem_size 8589934592)
if(1)
execute_process(
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/generate_lit_cfg.py --name simpoints
  INPUT_FILE ${our_test_suite}/lit.cfg
  OUTPUT_FILE ${our_test_suite}/simpoints.lit.cfg
  COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/generate_lit_cfg.py --name simpoints --
    ${GEM5_COMMAND} m5out-simpoints- %s -- -- --cpu-type=X86NonCachingSimpleCPU --mem-size=${mem_size} --simpoint-profile --
  INPUT_FILE ${our_test_suite}/lit.site.cfg
  OUTPUT_FILE ${our_test_suite}/simpoints.lit.site.cfg
  COMMAND_ERROR_IS_FATAL ANY
)
endif()

foreach(testpath IN LISTS testpaths)
  message("${testpath}")
  # message("1: testpath = ${testpath}")
  get_filename_component(testname ${testpath} NAME_WLE)
  # message("2: testname = ${testname}")
  get_filename_component(testdir ${testpath} DIRECTORY)
  # message("3: testdir = ${testdir}")
  set(m5out ${testdir}/run_${TEST_SUITE_RUN_TYPE}/m5out-${testname})
  # message("4: m5out = ${m5out}")
  add_custom_command(OUTPUT ${m5out}/simpoint.bb.gz
    COMMAND ${LLVM_LIT} --config-prefix=simpoints.lit -vva ${testpath}
  )
  add_custom_target(${testname}_simpoint_bb_gz ALL DEPENDS ${m5out}/simpoint.bb.gz)
endforeach()
