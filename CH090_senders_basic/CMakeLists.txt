cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(ASYNC_API LANGUAGES CXX)
include(FetchContent)

# stdexec
FetchContent_Declare(
  stdexec
  GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
  GIT_TAG        ae896337cbcfc242df609585a47f9822a0b48545 # as of 2026/09/13
  GIT_SHALLOW    FALSE # TRUE causes issues, see <https://gitlab.kitware.com/cmake/cmake/-/issues/23379>
  )
set(STDEXEC_BUILD_EXAMPLES OFF) # disable projects we don't care
FetchContent_MakeAvailable(stdexec)

add_executable(CH090_senders_basic main.cc)
target_compile_features(CH090_senders_basic PUBLIC cxx_std_23)
target_link_libraries(CH090_senders_basic PRIVATE STDEXEC::stdexec)
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CH090_senders_basic)
