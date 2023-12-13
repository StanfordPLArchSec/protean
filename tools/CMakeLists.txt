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

add_pin_tool(CFI CFI.cpp)

add_pin_tool(Count Count.cpp)

add_executable(cfitool cfitool.c)

add_executable(validate-trace validate-trace.cc)
target_link_libraries(validate-trace PRIVATE capstone elf)

add_executable(validate-trace2 validate-trace2.cc)


add_pin_tool(bbtrace bbtrace.cpp)


add_pin_tool(PrintInsts PrintInsts.cpp)

add_pin_tool(bbtrace2 bbtrace2.cpp)
