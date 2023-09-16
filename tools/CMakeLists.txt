cmake_minimum_required(VERSION 3.10)

project(llsct_hwx_tools)

set(CMAKE_CXX_STANDARD 20)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

include(PinTool)
add_pin_tool(FrameForwarding FrameForwarding.cpp)

add_pin_tool(Declassify Declassify.cpp Declassify.h)

add_executable(process-declassify process-declassify.cc)

add_pin_tool(InstructionCount InstructionCount.cpp)

add_pin_tool(Checkpointer Checkpointer.cpp MiscRegs.h)

add_pin_tool(Test Test.cpp)
