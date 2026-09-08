cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH0x_coro_await_all main.cc)
target_compile_features(CH0x_coro_await_all PUBLIC cxx_std_23)
