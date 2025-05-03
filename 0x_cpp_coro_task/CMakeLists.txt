cmake_minimum_required(VERSION 3.24 FATAL_ERROR)
project(async_api_styles LANGUAGES CXX)

add_executable(0x_cpp_coro_task main.cc)

target_compile_features(0x_cpp_coro_task
  PUBLIC cxx_std_23)

set_property(TARGET 0x_cpp_coro_task
  PROPERTY COMPILE_WARNING_AS_ERROR ON)

target_compile_options(0x_cpp_coro_task PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Wno-c++98-compat>
  )
