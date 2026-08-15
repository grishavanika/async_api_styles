cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(02_libcurl_callbacks_multi main.cc)
target_compile_features(02_libcurl_callbacks_multi PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(02_libcurl_callbacks_multi PRIVATE CURL::libcurl)
