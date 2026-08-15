cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(01_libcurl_blocking_easy main.cc)
target_compile_features(01_libcurl_blocking_easy PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(01_libcurl_blocking_easy PRIVATE CURL::libcurl)
