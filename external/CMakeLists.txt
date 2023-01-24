cmake_minimum_required(VERSION 3.16)

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

set(SDL_STATIC OFF)
set(SDL_TEST OFF)
add_subdirectory(SDL)

add_subdirectory(vk-bootstrap EXCLUDE_FROM_ALL)
