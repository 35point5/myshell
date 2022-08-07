cmake_minimum_required(VERSION 2.8)
project(myshell)

add_compile_options(-std=c++11)

set(CMAKE_CXX_STANDARD 20)

add_executable(myshell main.cpp)

add_executable(test test.cpp)