cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH068_fiber_await_many main.cc)
target_compile_features(CH068_fiber_await_many PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(CH068_fiber_await_many PRIVATE CURL::libcurl)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CH068_fiber_await_many)