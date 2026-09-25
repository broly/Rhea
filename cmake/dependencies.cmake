cmake_minimum_required (VERSION 3.29)

include(FetchContent)

FetchContent_Declare(
  fastgltf
  GIT_REPOSITORY https://github.com/spnda/fastgltf.git
  GIT_TAG        v0.9.0
  EXCLUDE_FROM_ALL
  CMAKE_CACHE_ARGS
)
set(FASTGLTF_COMPILE_AS_CPP20 ON CACHE BOOL "" FORCE)
set(FASTGLTF_ENABLE_CPP_MODULES ON CACHE BOOL "" FORCE)
# clang + MSVC STL can't include std headers in a module purview; use `import std` (see std_module.cmake)
set(FASTGLTF_USE_STD_MODULE ON CACHE BOOL "" FORCE)


FetchContent_MakeAvailable(fastgltf)

target_link_libraries(fastgltf_module PRIVATE rhea_std)



# Dear ImGui (docking branch) + GLFW / Vulkan backends, used by the debug UI (src/ui).
# Plain C++ library: built without modules, consumed from global module fragments.
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.92.9-docking
  GIT_SHALLOW    TRUE
  EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(imgui)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${imgui_SOURCE_DIR}/misc/cpp
)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan glfw)
# imgui.h ships with IMGUI_DISABLE_OBSOLETE_FUNCTIONS off; keep the API surface lean
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)



# Jolt Physics: backend of the physics module (src/physics). Plain C++ library, included only in
# global module fragments of the physics implementation units, never exported.
FetchContent_Declare(
  JoltPhysics
  GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
  GIT_TAG        v5.6.0
  GIT_SHALLOW    TRUE
  SOURCE_SUBDIR  Build
  EXCLUDE_FROM_ALL
)
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(OBJECT_LAYER_BITS 32 CACHE STRING "" FORCE)          # 16 category bits | 16 mask bits (physics:types)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)             # same as the rest of the engine
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)          # keep the engine's Debug / RelWithDebInfo flags
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE ON CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)                # GPU compute (hair) is not used
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(USE_ASSERTS ON CACHE BOOL "" FORCE)
else()
  set(USE_ASSERTS OFF CACHE BOOL "" FORCE)
endif()
# Instruction sets must match RHEA_SIMD_FLAGS (root CMakeLists.txt)
set(USE_SSE4_1 ON CACHE BOOL "" FORCE)
set(USE_SSE4_2 ON CACHE BOOL "" FORCE)
set(USE_AVX ON CACHE BOOL "" FORCE)
set(USE_AVX2 ON CACHE BOOL "" FORCE)
set(USE_AVX512 OFF CACHE BOOL "" FORCE)
set(USE_LZCNT ON CACHE BOOL "" FORCE)
set(USE_TZCNT ON CACHE BOOL "" FORCE)
set(USE_F16C ON CACHE BOOL "" FORCE)
set(USE_FMADD ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(JoltPhysics)

# Jolt publishes its -m flags to consumers. The engine already compiles everything with
# RHEA_SIMD_FLAGS; target-specific flags would make module BMIs (std, fastgltf) unimportable.
set_property(TARGET Jolt PROPERTY INTERFACE_COMPILE_OPTIONS "")
# Jolt asks for C++17; the MSVC STL overlay (std_module.cmake) needs the engine's standard.
set_property(TARGET Jolt PROPERTY CXX_STANDARD 26)
