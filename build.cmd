call cmake -S . -B __build ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
call cmake --build __build --config Debug

call cmake -S . -B __build_clang -T ClangCL ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
call cmake --build __build_clang --config Debug
