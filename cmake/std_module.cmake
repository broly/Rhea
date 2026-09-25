# Builds `import std;` for clang + MSVC STL.
#
# CMake's CXX_MODULE_STD only supports clang with libc++, so the module is built
# here from the STL's own modules/std.ixx, unmodified. The C++26 <meta> header
# (ported from bloomberg/clang-p2996, see tools/gen_meta_msstl.py) is exported from
# it by the <cwctype> wrapper of the STL overlay (tools/gen_stl_overlay.py), so
# `import std;` also provides std::meta, as C++26 specifies.
#
# The original std.ixx (not a patched copy) matters for IDEs: Rider builds `std`
# from the MSVC toolset itself, and a second file declaring `module std` makes
# `import std;` ambiguous there.

set(RHEA_REFLECTION_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/thirdparty/reflection/include)

if (NOT RHEA_MSVC_STL_DIR)
    # Same MSVC installation clang picks up: newest toolset of the newest VS.
    cmake_host_system_information(RESULT _vs_dir QUERY VS_17_DIR)
    file(GLOB _msvc_toolsets LIST_DIRECTORIES true "${_vs_dir}/VC/Tools/MSVC/*")
    list(SORT _msvc_toolsets COMPARE NATURAL ORDER DESCENDING)
    list(GET _msvc_toolsets 0 _msvc_toolset)
    set(RHEA_MSVC_STL_DIR "${_msvc_toolset}" CACHE PATH "MSVC toolset dir that provides modules/std.ixx")
endif()

# Patched copies of a few STL headers so containers/algorithms accept
# consteval-only types (std::meta::info). See tools/gen_stl_overlay.py.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(RHEA_STL_OVERLAY_DIR "${CMAKE_BINARY_DIR}/stl_overlay")
execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tools/gen_stl_overlay.py"
            "${RHEA_MSVC_STL_DIR}/include" "${RHEA_STL_OVERLAY_DIR}"
    RESULT_VARIABLE _overlay_result)
if (NOT _overlay_result EQUAL 0)
    message(FATAL_ERROR "tools/gen_stl_overlay.py failed: the MSVC STL changed, update PATCHES in the script.")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/tools/gen_stl_overlay.py"
    "${RHEA_MSVC_STL_DIR}/include/xutility"
    "${RHEA_MSVC_STL_DIR}/include/vector")
# Every target (including fetched dependencies) must see the same STL as the std module.
include_directories(BEFORE SYSTEM "${RHEA_STL_OVERLAY_DIR}")

set(_std_ixx "${RHEA_MSVC_STL_DIR}/modules/std.ixx")
if (NOT EXISTS "${_std_ixx}")
    message(FATAL_ERROR "std.ixx not found at ${_std_ixx}. Install the 'C++ Modules for v143 build tools' VS component or set RHEA_MSVC_STL_DIR.")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_std_ixx}")

add_library(rhea_std STATIC)
target_sources(rhea_std
    PUBLIC FILE_SET CXX_MODULES
    BASE_DIRS "${RHEA_MSVC_STL_DIR}/modules"
    FILES "${_std_ixx}" "${RHEA_MSVC_STL_DIR}/modules/std.compat.ixx"
)
target_include_directories(rhea_std PRIVATE ${RHEA_REFLECTION_INCLUDE_DIR})
target_compile_options(rhea_std PRIVATE
    -Wno-reserved-module-identifier
    -Wno-include-angled-in-module-purview
)
