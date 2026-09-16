cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)

add_executable(CH094_senders_sequence main.cc)
target_compile_features(CH094_senders_sequence PUBLIC cxx_std_23)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CH094_senders_sequence)
