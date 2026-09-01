cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH0x_fiber_basic main.cc)
target_compile_features(CH0x_fiber_basic PUBLIC cxx_std_23)
