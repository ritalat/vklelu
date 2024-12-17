cmake_minimum_required(VERSION 3.16)

add_subdirectory(glm)

set(SDL_STATIC OFF CACHE BOOL "...")
set(SDL_TEST OFF CACHE BOOL "...")
add_subdirectory(SDL)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)

add_library(tinyobjloader INTERFACE)
target_include_directories(tinyobjloader SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/tinyobjloader)

add_subdirectory(vk-bootstrap EXCLUDE_FROM_ALL)

add_library(VulkanMemoryAllocator INTERFACE)
target_include_directories(VulkanMemoryAllocator SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/VulkanMemoryAllocator/include)
