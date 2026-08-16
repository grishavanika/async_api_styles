cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH01_libcurl_easy main.cc)
target_compile_features(CH01_libcurl_easy PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(CH01_libcurl_easy PRIVATE CURL::libcurl)
