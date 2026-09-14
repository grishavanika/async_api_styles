cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH057_coro_await_all main.cc)
target_compile_features(CH057_coro_await_all PUBLIC cxx_std_23)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CH057_coro_await_all)