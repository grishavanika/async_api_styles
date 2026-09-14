cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH064_fiber_callback main.cc)
target_compile_features(CH064_fiber_callback PUBLIC cxx_std_23)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CH064_fiber_callback)