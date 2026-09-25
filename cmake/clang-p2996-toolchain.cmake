# Toolchain for building Rhea with bloomberg/clang-p2996 (C++26 reflection)
# against the MSVC STL/ABI. Chainloaded by vcpkg, see CMakePresets.json.
# The compiler is built with tools/build_clang_p2996.bat.

set(RHEA_CLANG_ROOT "D:/toolchains/clang-p2996-install" CACHE PATH "clang-p2996 install prefix")

# Forced: IDEs may pass their own default compiler (e.g. cl.exe), which can't
# compile reflection code.
set(CMAKE_CXX_COMPILER "${RHEA_CLANG_ROOT}/bin/clang++.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER "${RHEA_CLANG_ROOT}/bin/clang.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_RC_COMPILER "${RHEA_CLANG_ROOT}/bin/llvm-rc.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_LINKER_TYPE LLD)

# clang only autodetects MSVC/Windows SDK library dirs when the LIB environment
# variable is absent. Pass them explicitly so the build does not depend on the
# environment the IDE is started from.
cmake_host_system_information(RESULT _vs_dir QUERY VS_17_DIR)
file(GLOB _msvc_toolsets LIST_DIRECTORIES true "${_vs_dir}/VC/Tools/MSVC/*")
list(SORT _msvc_toolsets COMPARE NATURAL ORDER DESCENDING)
list(GET _msvc_toolsets 0 _msvc_toolset)

cmake_host_system_information(RESULT _kits_root QUERY WINDOWS_REGISTRY
    "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots" VALUE KitsRoot10)
file(GLOB _sdk_versions LIST_DIRECTORIES true "${_kits_root}/Lib/10.*")
list(SORT _sdk_versions COMPARE NATURAL ORDER DESCENDING)
list(GET _sdk_versions 0 _sdk_lib)

set(_libpaths "")
foreach(_dir "${_msvc_toolset}/lib/x64" "${_sdk_lib}/ucrt/x64" "${_sdk_lib}/um/x64")
    file(TO_CMAKE_PATH "${_dir}" _dir)
    string(APPEND _libpaths " \"-Xlinker\" \"/libpath:${_dir}\"")
endforeach()
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_libpaths}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_libpaths}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_libpaths}")
