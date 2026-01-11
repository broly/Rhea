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


FetchContent_MakeAvailable(fastgltf)

