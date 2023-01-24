cmake_minimum_required(VERSION 3.16)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "..." FORCE)
endif()

project(VKlelu)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX ${CMAKE_SOURCE_DIR}/install CACHE PATH "..." FORCE)
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(CMAKE_INSTALL_BINDIR ".")
set(CMAKE_INSTALL_LIBDIR ".")

find_package(Vulkan)

add_subdirectory(external)

set(SOURCES main.cc
            vklelu.cc)

set(HEADERS vklelu.hh)

add_executable(VKlelu ${SOURCES} ${HEADERS})

if(WIN32)
    set_target_properties(VKlelu PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
endif()

if(UNIX)
    target_link_options(VKlelu PUBLIC "-Wl,--enable-new-dtags")
    set_target_properties(VKlelu PROPERTIES INSTALL_RPATH "\${ORIGIN}")
endif()

target_link_libraries(VKlelu Vulkan::Vulkan
                             SDL2::SDL2
                             SDL2::SDL2main
                             vk-bootstrap::vk-bootstrap)

install(TARGETS VKlelu SDL2 RUNTIME DESTINATION ".")
