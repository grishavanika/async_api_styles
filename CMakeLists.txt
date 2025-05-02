# 3.24 is required because of C++23 (3.20)
# and CMAKE_COMPILE_WARNING_AS_ERROR (3.24) support.
cmake_minimum_required(VERSION 3.24 FATAL_ERROR)
project(async_api_styles LANGUAGES CXX)

add_subdirectory(00_cmake_libcurl)
add_subdirectory(01_libcurl_blocking_easy)
