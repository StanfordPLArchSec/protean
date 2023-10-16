cmake_minimum_required(VERSION 3.10)

project(llsct_hwx_tools)

set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# include(PinTool)
include(PinTool2)
add_pin_tool(FrameForwarding FrameForwarding.cpp)

add_pin_tool(Declassify Declassify.cpp)

add_executable(process-declassify process-declassify.cc)

add_pin_tool(InstructionCount InstructionCount.cpp)

add_pin_tool(Checkpointer Checkpointer.cpp MiscRegs.h)

add_pin_tool(Test Test.cpp)

add_pin_tool(Test2 Test2.cpp Vsyscall.h Trace.h)

add_executable(tracediff tracediff.cc)

add_pin_tool(Hello Hello.cpp)

add_executable(pattern-analyzer pattern-analyzer.cc)

add_pin_tool(AccessSizes AccessSizes.cpp)

add_pin_tool(RFSizes RFSizes.cpp)

add_executable(popcount popcount.c)
