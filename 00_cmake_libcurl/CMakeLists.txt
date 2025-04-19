add_executable(00_cmake_libcurl main_00_cmake_libcurl.cc)

set_property(TARGET 00_cmake_libcurl
  PROPERTY COMPILE_WARNING_AS_ERROR ON)
# https://stackoverflow.com/a/50882216
target_compile_options(00_cmake_libcurl PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
  )

find_package(CURL REQUIRED)
target_link_libraries(00_cmake_libcurl PRIVATE CURL::libcurl)
