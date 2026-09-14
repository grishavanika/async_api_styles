cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH058_coro_await_curl main.cc)
target_compile_features(CH058_coro_await_curl PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(CH058_coro_await_curl PRIVATE CURL::libcurl)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CH058_coro_await_curl)