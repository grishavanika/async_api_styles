cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH055_coro_task main.cc)
target_compile_features(CH055_coro_task PUBLIC cxx_std_23)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CH055_coro_task)