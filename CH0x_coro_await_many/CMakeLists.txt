cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH0x_coro_await_many main.cc)
target_compile_features(CH0x_coro_await_many PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(CH0x_coro_await_many PRIVATE CURL::libcurl)

