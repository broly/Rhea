@echo off
rem Builds bloomberg/clang-p2996 (clang with C++26 reflection, P2996) for Windows.
rem Result: D:\toolchains\clang-p2996-install, used by cmake/clang-p2996-toolchain.cmake.
rem Takes ~1 hour, ~2.6 GB installed. Requires Visual Studio 2022 (C++ workload), git, cmake.

set SRC=D:\toolchains\clang-p2996
set BUILD=D:\toolchains\build-p2996
set INSTALL=D:\toolchains\clang-p2996-install

if not exist %SRC% (
    git clone --depth 1 --branch p2996 https://github.com/bloomberg/clang-p2996.git %SRC% || exit /b 1
)

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%

cmake -S %SRC%\llvm -B %BUILD% -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DLLVM_ENABLE_PROJECTS="clang;lld" ^
  -DLLVM_TARGETS_TO_BUILD=X86 ^
  -DLLVM_ENABLE_ASSERTIONS=OFF ^
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DCLANG_INCLUDE_TESTS=OFF ^
  -DLLVM_OPTIMIZED_TABLEGEN=ON ^
  -DCMAKE_INSTALL_PREFIX=%INSTALL% || exit /b 1
cmake --build %BUILD% --target install || exit /b 1

rem After updating the compiler, regenerate the MSVC STL port of <meta>:
rem   python tools\gen_meta_msstl.py %SRC%
echo BUILD_OK
