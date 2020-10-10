cmake_minimum_required(VERSION 3.5)

project(alisp CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions( "/W3 /D_CRT_SECURE_NO_WARNINGS /wd4005 /wd4996 /nologo" )

find_package(fmt CONFIG REQUIRED)


add_library(libalisp STATIC alisp.cpp)

add_executable(alisp main.cpp)
target_link_libraries(alisp PRIVATE libalisp fmt::fmt fmt::fmt-header-only)

find_package(Catch2 REQUIRED)
add_executable(test-alisp test_alisp.cpp)
target_link_libraries(test-alisp Catch2::Catch2 libalisp)

include(CTest)
include(Catch)
catch_discover_tests(test-alisp)
