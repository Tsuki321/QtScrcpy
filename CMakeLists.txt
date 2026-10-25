cmake_minimum_required(VERSION 3.19 FATAL_ERROR)
project(all)

add_subdirectory(QtScrcpy)

option(QTSCRCPY_BUILD_TESTS "Build regression tests and microbenchmarks" OFF)
if(QTSCRCPY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
