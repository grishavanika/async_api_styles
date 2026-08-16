cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH00_cmake main.cc)
target_compile_features(CH00_cmake PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(CH00_cmake PRIVATE CURL::libcurl)
