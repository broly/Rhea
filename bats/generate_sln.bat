cd..
rd /s /q build
cmake -S . -B build ^
  -G "Visual Studio 17 2022" ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DCMAKE_C_COMPILER=clang-cl ^
  -DCMAKE_CXX_COMPILER=clang-cl
pause