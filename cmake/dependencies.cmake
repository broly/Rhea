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

