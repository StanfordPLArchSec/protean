cmake_minimum_required(VERSION 3.20)

project(llsct2-sim)

include(ExternalProject)

set(TEST_DIR "" CACHE PATH "Path to test directory")
if(NOT TEST_DIR)
  message(FATAL_ERROR "Cache variable TEST_DIR unset")
endif()
set(GEM5_BINARY "" CACHE FILEPATH "Path to gem5 binary")
if(NOT GEM5_BINARY)
  message(FATAL_ERROR "Cache variable GEM5_BINARY unset")
endif()
set(GEM5_SE_PY "" CACHE FILEPATH "Path to gem5's se.py script")
if(NOT GEM5_SE_PY)
  message(FATAL_ERROR "Cache variable GEM5_SE_PY unset")
endif()
set(TEST_SUITE_RUN_TYPE "" CACHE STRING "Run type")
if(NOT TEST_SUITE_RUN_TYPE)
  message(FATAL_ERROR "Cache variable TEST_SUITE_RUN_TYPE unset")
endif()
set(TEST_SUITE_BINARY_DIR "" CACHE PATH "LLVM's Test Suite build directory")
if(NOT TEST_SUITE_BINARY_DIR)
  message(FATAL_ERROR "Cache variable TEST_SUITE_BINARY_DIR unset")
endif()
set(SIM_BLACKLIST "" CACHE STRING "tests to ignore")

message("SIM_BLACKLIST: ${SIM_BLACKLIST}")


# Build simpoint.
ExternalProject_Add(simpoint
  GIT_REPOSITORY https://github.com/OscarLGH/SimPoint.3.2.git
  BUILD_IN_SOURCE On
  CONFIGURE_COMMAND ""
  BUILD_COMMAND make Simpoint
  INSTALL_COMMAND ""
)
ExternalProject_Get_Property(simpoint BINARY_DIR)
set(simpoint_BINARY_DIR ${BINARY_DIR})
add_executable(simpoint::simpoint IMPORTED GLOBAL)
set_target_properties(simpoint::simpoint PROPERTIES IMPORTED_LOCATION ${simpoint_BINARY_DIR}/bin/simpoint)
add_dependencies(simpoint::simpoint simpoint)

# Generate scripts to run each benchmark
file(GLOB paths_to_copy ${TEST_DIR}/*)
set(our_test_dir ${CMAKE_CURRENT_BINARY_DIR}/tests)
make_directory(${our_test_dir})
file(COPY ${paths_to_copy} DESTINATION ${our_test_dir})

# Find list of *.test files.
set(mem_size 8589934592)
file(GLOB_RECURSE testpaths ${our_test_dir}/*.test)
set(all_ipcs)
foreach(testpath IN LISTS testpaths)
  set(testids)
  get_filename_component(testdir ${testpath} DIRECTORY)
  get_filename_component(testname ${testpath} NAME_WLE)

  # Configure - Profiling and Generating BBV
  execute_process(COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/generate_runscripts.py
    --test=${testpath}
    --gem5-binary=${GEM5_BINARY}
    --gem5-se-py=${GEM5_SE_PY}
    --input-rundir=${testdir}/run_test
    --output-rundir=${testdir}/simout_test_%i
    --test-suite-binary-dir=${TEST_SUITE_BINARY_DIR}
    --
    --cpu-type=X86NonCachingSimpleCPU --mem-size=${mem_size} --simpoint-profile
    OUTPUT_VARIABLE ids
    COMMAND_ERROR_IS_FATAL ANY
  )

  # Configure - Taking SimPoint Checkpoints in gem5
  execute_process(COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/generate_runscripts.py
    --test=${testpath}
    --gem5-binary=${GEM5_BINARY}
    --gem5-se-py=${GEM5_SE_PY}
    --input-rundir=${testdir}/run_test
    --output-rundir=${testdir}/checkpoint_test_%i
    --test-suite-binary-dir=${TEST_SUITE_BINARY_DIR}
    --no-verify
    --
    --cpu-type=X86NonCachingSimpleCPU --mem-size=${mem_size}
    --take-simpoint-checkpoint=${testdir}/%i.simpoint,${testdir}/%i.weight,10000000,10000
    COMMAND_ERROR_IS_FATAL ANY
  )

  # Configure - Resuming from gem5 SimPoint Checkpoints
  execute_process(COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/generate_runscripts.py
    --test=${testpath}
    --gem5-binary=${GEM5_BINARY}
    --gem5-se-py=${GEM5_SE_PY}
    --input-rundir=${testdir}/run_test
    --output-rundir=${testdir}/results_test_%i
    --test-suite-binary-dir=${TEST_SUITE_BINARY_DIR}
    --no-verify
    --
    --cpu-type=X86O3CPU --mem-size=${mem_size} --l1d_size=64kB --l1i_size=16kB --caches
    --checkpoint-dir=${testdir}/checkpoint_test_%i/m5out
    --restore-simpoint-checkpoint
    --checkpoint-restore=\$checkpoint
    COMMAND_ERROR_IS_FATAL ANY
  )

  set(test_ipcs) # paths to IPC files for this test
  foreach(id IN LISTS ids)
    set(teststr ${testname}_${id})
    list(APPEND testids ${teststr})
    message("${teststr}")
    list(FIND SIM_BLACKLIST ${teststr} blacklist_index)
    if(${blacklist_index} STREQUAL -1)
      set(rundir ${testdir}/simout_test_${id})
      set(m5out ${rundir}/m5out)
      set(simpoint_bb_gz ${m5out}/simpoint.bb.gz)
      add_custom_command(OUTPUT ${simpoint_bb_gz}
	COMMAND bash -x ${rundir}/run.sh
      )
      add_custom_target(${testname}_${id}_simpoint_bb_gz ALL
	DEPENDS ${simpoint_bb_gz}
      )

      # process simpoints
      set(simpoints ${testdir}/${id}.simpoint)
      set(weights ${testdir}/${id}.weight)
      add_custom_command(OUTPUT ${simpoints} ${weights}
	COMMAND simpoint::simpoint -loadFVFile ${simpoint_bb_gz} -maxK 30 -saveSimpoints ${simpoints}
	-saveSimpointWeights ${weights} -inputVectorsGzipped
	DEPENDS ${simpoint_bb_gz}
      )
      add_custom_target(${testname}_${id}_simpoints ALL
	DEPENDS ${simpoints} ${weights}
      )
      
      # taking simpoint checkpoints in gem5
      set(checkpoints_dir ${testdir}/checkpoints_test_${id}/m5out)
      set(checkpoint_stamp ${checkpoints_dir}/stats.txt)
      add_custom_command(OUTPUT ${checkpoint_stamp}
	COMMAND bash -x ${testdir}/checkpoint_test_${id}/run.sh
	DEPENDS ${simpoints} ${weights}
      )
      add_custom_target(${testname}_${id}_checkpoints ALL
	DEPENDS ${checkpoint_stamp}
      )

      # resuming from gem5 checkpoints
      # For now just POC resuming from 1st checkpoint
      set(ipc ${testdir}/ipc_${id})
      add_custom_command(OUTPUT ${ipc}
	COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/restore_checkpoints.py
	--run-sh=${testdir}/results_test_${id}/run.sh
	--checkpoint-dir=${testdir}/checkpoint_test_${id}/m5out
	--ipc=${ipc}
	DEPENDS ${checkpoint_stamp} ${CMAKE_CURRENT_SOURCE_DIR}/restore_checkpoints.py ${testdir}/results_test_${id}/run.sh
	WORKING_DIRECTORY ${testdir}/results_test_${id}
      )
      add_custom_target(${testname}_${id}_ipc ALL DEPENDS ${ipc})
      list(APPEND test_ipcs ${ipc})
    endif()
  endforeach()

  # compute ipc for this test in total
  list(LENGTH test_ipcs testids_len)
  if(${testids_len} GREATER 0)
    set(ipc_geomean ${testdir}/ipc_geomean)
    add_custom_command(OUTPUT ${ipc_geomean}
      COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/geomean.py ${test_ipcs} > ${ipc_geomean}
      DEPENDS ${test_ipcs} ${CMAKE_CURRENT_SOURCE_DIR}/geomean.py
    )
    add_custom_target(${testname}_ipc_geomean DEPENDS ${ipc_geomean})
    list(APPEND all_ipcs ${ipc_geomean})
  endif()
endforeach()

# Compute overall geomean for all tests
set(global_ipc_geomean ${CMAKE_CURRENT_BINARY_DIR}/global_ipc_geomean.txt)
add_custom_command(OUTPUT ${global_ipc_geomean}
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/geomean.py ${all_ipcs} > ${global_ipc_geomean}
  DEPENDS ${all_ipcs} ${CMAKE_CURRENT_SOURCE_DIR}/geomean.py
)
add_custom_target(global_ipc_geomean ALL DEPENDS ${global_ipc_geomean})

