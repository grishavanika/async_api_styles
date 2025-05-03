cmake_minimum_required(VERSION 3.24 FATAL_ERROR)
project(async_api_styles LANGUAGES CXX)

add_executable(02_libcurl_callbacks_multi main.cc)

target_compile_features(02_libcurl_callbacks_multi
  PUBLIC cxx_std_23)

set_property(TARGET 02_libcurl_callbacks_multi
  PROPERTY COMPILE_WARNING_AS_ERROR ON)

target_compile_options(02_libcurl_callbacks_multi PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -Wno-c++98-compat>
  )

find_package(CURL REQUIRED)

target_link_libraries(02_libcurl_callbacks_multi
  PRIVATE CURL::libcurl)
