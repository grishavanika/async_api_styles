cmake_minimum_required(VERSION 3.24 FATAL_ERROR)
project(async_api_styles LANGUAGES CXX)

add_executable(01_libcurl_blocking_easy main.cc)

target_compile_features(01_libcurl_blocking_easy
  PUBLIC cxx_std_23)

set_property(TARGET 01_libcurl_blocking_easy
  PROPERTY COMPILE_WARNING_AS_ERROR ON)

target_compile_options(01_libcurl_blocking_easy PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
  )

find_package(CURL REQUIRED)

target_link_libraries(01_libcurl_blocking_easy
  PRIVATE CURL::libcurl)
