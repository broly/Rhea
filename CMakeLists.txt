cmake_minimum_required(VERSION 3.21)
project(rhea LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "cxx_modules")
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 1)

option(FASTGLTF_COMPILE_AS_CPP20 "Compile fastgltf as C++20" ON)
option(FASTGLTF_ENABLE_CPP_MODULES "Enable C++ modules target" ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(Stb REQUIRED)


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
    src/stb_image_wrap
    src/fastgltf_wrapper
    src/common
    src/math
    src/logging
    src/object
    src/assets
    src/platform
    src/framework
    src/render
    src/world_scripts
    src/engine
    src/render_backends/vk
    src/render_backends/vk/aggregate
    src/game
)

function(add_module_dir TARGET DIR)
    file(GLOB MODULE_IXX CONFIGURE_DEPENDS ${DIR}/*.ixx)
    file(GLOB MODULE_CPP CONFIGURE_DEPENDS ${DIR}/*.cpp)
    file(GLOB MODULE_H   CONFIGURE_DEPENDS ${DIR}/*.h)

    # гарантируем, что module.ixx идёт первым
    list(FILTER MODULE_IXX INCLUDE REGEX ".*/module\\.ixx$")
    file(GLOB OTHER_IXX CONFIGURE_DEPENDS ${DIR}/*.ixx)
    list(FILTER OTHER_IXX EXCLUDE REGEX ".*/module\\.ixx$")

    target_sources(${TARGET}
        PRIVATE
            ${MODULE_IXX}
            ${OTHER_IXX}
            ${MODULE_CPP}
            ${MODULE_H}
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
		Vulkan::Vulkan
		glfw
		JsonCpp::JsonCpp
		fastgltf::fastgltf
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
)

set(SPIRV_SHADERS "")


foreach(shader ${SHADERS})
    get_filename_component(shader_name ${shader} NAME)
    set(output ${SHADER_BINARY_DIR}/${shader_name}.spv)

    add_custom_command(
        OUTPUT ${output}
        COMMAND ${GLSLC} ${shader} -o ${output}
        DEPENDS ${shader}
        COMMENT "Compiling shader ${shader_name}"
        VERBATIM
    )

    list(APPEND SPIRV_SHADERS ${output})
endforeach()

add_custom_target(shaders ALL
    DEPENDS ${SPIRV_SHADERS}
)

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

add_dependencies(rhea shaders)

# Создать пользовательскую цель
add_custom_target(compile_shaders ALL
    DEPENDS ${SPIRV_SHADERS}
    COMMENT "Compiling all shaders"
)

# Сделать так, чтобы цель rhea зависела от файлов шейдеров
add_custom_command(
    TARGET rhea
    PRE_BUILD
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target compile_shaders
    COMMENT "Recompiling shaders if needed"
)

# Убедиться, что shaders пересобираются при изменении исходных файлов
set_target_properties(compile_shaders PROPERTIES EXCLUDE_FROM_ALL FALSE)