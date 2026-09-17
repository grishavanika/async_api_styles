cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH095_senders_curlv2 main.cc)
target_compile_features(CH095_senders_curlv2 PUBLIC cxx_std_23)
find_package(CURL REQUIRED)
target_link_libraries(CH095_senders_curlv2 PRIVATE CURL::libcurl)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CH095_senders_curlv2)
