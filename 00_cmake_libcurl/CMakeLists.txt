cmake_minimum_required(VERSION 3.24 FATAL_ERROR)
project(async_api_styles LANGUAGES CXX)

add_executable(00_cmake_libcurl main.cc)

target_compile_features(00_cmake_libcurl
  PUBLIC cxx_std_23)

set_property(TARGET 00_cmake_libcurl
  PROPERTY COMPILE_WARNING_AS_ERROR ON)

# https://stackoverflow.com/a/50882216
target_compile_options(00_cmake_libcurl PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
  )

find_package(CURL REQUIRED)

target_link_libraries(00_cmake_libcurl
  PRIVATE CURL::libcurl)
