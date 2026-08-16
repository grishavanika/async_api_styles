cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH02_libcurl_multi main.cc)
target_compile_features(CH02_libcurl_multi PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(CH02_libcurl_multi PRIVATE CURL::libcurl)
