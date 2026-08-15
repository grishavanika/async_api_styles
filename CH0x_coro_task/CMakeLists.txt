cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(0x_cpp_coro_task main.cc)
target_compile_features(0x_cpp_coro_task PUBLIC cxx_std_23)
