cd..
cmake -S . -B build ^
  -G "Visual Studio 17 2022" ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
pause