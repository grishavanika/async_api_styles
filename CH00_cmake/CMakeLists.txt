cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(00_cmake_libcurl LANGUAGES CXX)

add_executable(00_cmake_libcurl main.cc)
target_compile_features(00_cmake_libcurl PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(00_cmake_libcurl PRIVATE CURL::libcurl)
