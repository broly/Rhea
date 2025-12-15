cmake_minimum_required(VERSION 3.21)
project(rhea LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

add_executable(rhea
    src/main.cpp
)

target_link_libraries(rhea
    Vulkan::Vulkan
    glfw
)

target_compile_definitions(rhea
    PRIVATE GLFW_INCLUDE_VULKAN
)

target_include_directories(rhea
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)