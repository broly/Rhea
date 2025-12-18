cmake_minimum_required(VERSION 3.21)
project(rhea LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)


file(GLOB_RECURSE RHEA_SOURCES
    CONFIGURE_DEPENDS
    src/*.cpp
    src/*.h
    shaders/*.vert
    shaders/*.frag
    shaders/*.comp
    shaders/*.vs
    shaders/*.fs
)

add_executable(rhea ${RHEA_SOURCES})

target_link_libraries(rhea
	PRIVATE
		Vulkan::Vulkan
		glfw
		JsonCpp::JsonCpp
)

target_compile_definitions(rhea
    PRIVATE GLFW_INCLUDE_VULKAN
)

target_include_directories(rhea
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
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

add_dependencies(rhea shaders)
