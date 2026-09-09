cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(App_Blocking main.cc)
target_compile_features(App_Blocking PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(App_Blocking PRIVATE CURL::libcurl)
