cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH092_senders_simple main.cc)
target_compile_features(CH092_senders_simple PUBLIC cxx_std_23)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CH092_senders_simple)
