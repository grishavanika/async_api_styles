cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(App_Tasks main.cc)
target_compile_features(App_Tasks PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(App_Tasks PRIVATE CURL::libcurl)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT App_Tasks)