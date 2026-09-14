cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH053_coro_curl0 main.cc)
target_compile_features(CH053_coro_curl0 PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(CH053_coro_curl0 PRIVATE CURL::libcurl)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CH053_coro_curl0)