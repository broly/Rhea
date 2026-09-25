cmake_minimum_required(VERSION 3.30)
project(rhea LANGUAGES CXX)

# C++26 static reflection (P2996) requires the bloomberg/clang-p2996 compiler,
# see CMakePresets.json and tools/build_clang_p2996.bat
if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "Rhea requires clang-p2996 (C++26 reflection). Use a preset from CMakePresets.json.")
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Must be identical for every target: module BMIs are only importable with matching flags.
add_compile_options(-freflection-latest)

# x86-64-v3. Jolt Physics is built for these instruction sets and its headers use them in inline
# functions, so every target gets the same flags (also required for importable module BMIs).
set(RHEA_SIMD_FLAGS -mavx2 -mbmi -mpopcnt -mlzcnt -mf16c -mfma -mfpmath=sse)
add_compile_options(${RHEA_SIMD_FLAGS})

include(cmake/std_module.cmake)
include(cmake/dependencies.cmake)



find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(tinyexr CONFIG REQUIRED)
find_package(unofficial-spirv-reflect CONFIG REQUIRED)


# add_executable(rhea ${RHEA_SOURCES})
add_executable(rhea ${RHEA_SOURCES})

# file(GLOB_RECURSE RHEA_SOURCES
#     CONFIGURE_DEPENDS
#     src/*.cpp
#     src/*.ixx
#     src/*.h
#     shaders/*.vert
#     shaders/*.frag
#     shaders/*.comp
#     shaders/*.vs
#     shaders/*.fs
# )
set(RHEA_MODULE_DIRS
    src/glm_wrap
    src/tinyexr_wrap
    src/input
    src/expr
    src/spirv_reflect_wrap
    src/stb_image_wrap
    src/fastgltf_helper
    src/common
    src/profiling
    src/math
    src/logging
    src/object
    src/assets
    src/platform
    src/render_scene
    src/physics
    src/debug_draw
    src/framework
    src/rhcomponents
    src/render
    src/world_scripts
    src/engine
    src/render_backends/vk
    src/render_backends/vk/aggregate
    src/render_backends/vk/pipelines
    src/ui
    src/game
)

function(add_module_dir TARGET DIR)
    file(GLOB MODULE_IXX CONFIGURE_DEPENDS ${DIR}/*.ixx)
    file(GLOB MODULE_CPP CONFIGURE_DEPENDS ${DIR}/*.cpp)
    file(GLOB MODULE_H   CONFIGURE_DEPENDS ${DIR}/*.h)
    

    list(FILTER MODULE_IXX INCLUDE REGEX ".*/module\\.ixx$")
    file(GLOB OTHER_IXX CONFIGURE_DEPENDS ${DIR}/*.ixx)
    list(FILTER OTHER_IXX EXCLUDE REGEX ".*/module\\.ixx$")

    target_sources(${TARGET}
        PRIVATE
            ${MODULE_CPP}
            ${MODULE_H}
        PRIVATE FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src
        FILES
            ${MODULE_IXX}
            ${OTHER_IXX}
    )
endfunction()


foreach(dir ${RHEA_MODULE_DIRS})
    add_module_dir(rhea ${dir})
endforeach()

target_sources(rhea 
    PRIVATE
        src/main.cpp)


target_link_libraries(rhea
	PRIVATE
		rhea_std
		Vulkan::Vulkan
		glfw
		JsonCpp::JsonCpp
		unofficial::spirv-reflect
		fastgltf::module
		unofficial::tinyexr::tinyexr
		imgui
		Jolt
)

target_compile_definitions(rhea
    PRIVATE 
        GLFW_INCLUDE_VULKAN
        STB_IMAGE_IMPLEMENTATION
)

target_include_directories(rhea
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
	PRIVATE
		${Stb_INCLUDE_DIR}
)


find_program(GLSLC glslc REQUIRED)

set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_BINARY_DIR}/shaders)

file(GLOB_RECURSE SHADERS
    CONFIGURE_DEPENDS
    ${SHADER_SOURCE_DIR}/*.vert
    ${SHADER_SOURCE_DIR}/*.frag
    ${SHADER_SOURCE_DIR}/*.comp
    ${SHADER_SOURCE_DIR}/*.glsl
    ${SHADER_SOURCE_DIR}/*.slang
    ${SHADER_SOURCE_DIR}/*.r*
)

source_group("Shaders" FILES ${SHADERS})

target_sources(rhea PRIVATE ${SHADERS})

set_source_files_properties(${SHADERS}
    PROPERTIES
    HEADER_FILE_ONLY TRUE
)


# Shaders are compiled at runtime (see render/pipeline_family.cpp); they are
# listed as sources only so they show up in the IDE.

set(ENABLE_ASAN 1)

# if (MSVC AND ENABLE_ASAN)
#    get_filename_component(LLVM_BIN
#        "$ENV{VCToolsInstallDir}/Llvm/x64/lib/clang/19/lib/windows"
#        ABSOLUTE)
#
#    add_custom_command(TARGET rhea POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy_if_different
#        "${LLVM_BIN}/clang_rt.asan_dynamic-x86_64.dll"
#        $<TARGET_FILE_DIR:rhea>)
# endif()

# if (MSVC AND ENABLE_ASAN)
#     target_compile_options(rhea PRIVATE /fsanitize=address)
#     target_link_options(rhea PRIVATE /fsanitize=address)
# endif()

file(GLOB_RECURSE JSON_FILES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/render/resources/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/render/schemas/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/render/schemas/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/levels/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/system/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/actors/*.json
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/nn/*.json
)

source_group("Assets\\JSON" FILES ${JSON_FILES})

target_sources(rhea PRIVATE ${JSON_FILES})

set_source_files_properties(${JSON_FILES}
    PROPERTIES HEADER_FILE_ONLY TRUE
)

# Copy dependency DLLs (glfw, jsoncpp, simdjson, miniz, ...) next to the executable.
# Replaces vcpkg's applocal step, which needs dumpbin/llvm-objdump in PATH.
add_custom_command(TARGET rhea POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:rhea> $<TARGET_FILE_DIR:rhea>
    COMMAND_EXPAND_LISTS
)

# Embed natvis into the PDB so Rider/VS debuggers pick it up
target_link_options(rhea PRIVATE "LINKER:/NATVIS:${CMAKE_SOURCE_DIR}/tools/debugger/Name.natvis")
set_property(TARGET rhea APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/tools/debugger/Name.natvis)




# Windows specific: set the CUDA toolkit path manually
if (WIN32)
    set(CUDAToolkit_ROOT "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.2")
endif()


# Find CUDA toolkit
find_package(CUDAToolkit REQUIRED)

# Include NVTX headers
target_include_directories(rhea PRIVATE "${CUDAToolkit_INCLUDE_DIRS}")



target_include_directories(rhea PRIVATE ${CUDA_TOOLKIT_ROOT_DIR}/include)
target_compile_definitions(rhea PRIVATE WIN32)

# printf-style helpers (Logger::Log, print_error) take the format string as a parameter by design
target_compile_options(rhea PRIVATE -Wno-format-security)
