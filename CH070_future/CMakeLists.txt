cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH070_future main.cc)
target_compile_features(CH070_future PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(CH070_future PRIVATE CURL::libcurl)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CH070_future)