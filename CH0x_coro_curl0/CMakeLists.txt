cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(0x_cpp_coro_await_curl_crash main.cc)
target_compile_features(0x_cpp_coro_await_curl_crash PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(0x_cpp_coro_await_curl_crash PRIVATE CURL::libcurl)
