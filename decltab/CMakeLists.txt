cmake_minimum_required(VERSION 3.25)
project(decltab)

set(CMAKE_CXX_STANDARD 20)

add_library(libdecltab decltab.hh decltab.cc)

add_executable(decltab main.cc)
target_link_libraries(decltab PRIVATE libdecltab)

add_executable(decltab2 main2.cc)
target_link_libraries(decltab2 PRIVATE libdecltab)
