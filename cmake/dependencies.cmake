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
