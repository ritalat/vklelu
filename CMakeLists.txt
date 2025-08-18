cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

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

set(SYSTEM_SDL OFF CACHE BOOL "...")

if(SYSTEM_SDL)
    find_package(SDL3 REQUIRED)
endif()

add_subdirectory(external)

find_package(Vulkan REQUIRED COMPONENTS glslc)

set(SHADERS shader.frag
            shader.vert)

file(MAKE_DIRECTORY shaders)

foreach(SHADER ${SHADERS})
    set(SHADER_FILE ${CMAKE_SOURCE_DIR}/src/glsl/${SHADER})
    set(GLSLC_OUT ${CMAKE_SOURCE_DIR}/shaders/${SHADER}.spv)
    add_custom_command(
        OUTPUT ${GLSLC_OUT}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${SHADER_FILE} -o ${GLSLC_OUT}
        DEPENDS ${SHADER_FILE})
    list(APPEND SPIRV_BINARIES ${GLSLC_OUT})
endforeach()

add_custom_target(Shaders DEPENDS ${SPIRV_BINARIES})

if(MINGW)
    # Find MinGW runtime libraries (only tested with Arch and MSYS2)
    set(MINGW_LIB_DIRS /usr/x86_64-w64-mingw32/bin C:/msys64/mingw64/bin)
    set(CMAKE_FIND_LIBRARY_SUFFIXES .dll)
    find_library(MINGW_STDCPP_LIB NAMES libstdc++-6 HINTS ${MINGW_LIB_DIRS})
    if(MINGW_STDCPP_LIB)
        list(APPEND MINGW_RUNTIME_LIBS "${MINGW_STDCPP_LIB}")
    endif()
    find_library(MINGW_GCCSEH_LIB NAMES libgcc_s_seh-1 HINTS ${MINGW_LIB_DIRS})
    if(MINGW_GCCSEH_LIB)
        list(APPEND MINGW_RUNTIME_LIBS "${MINGW_GCCSEH_LIB}")
    endif()
    find_library(MINGW_WINPTHREAD_LIB NAMES libwinpthread-1 HINTS ${MINGW_LIB_DIRS})
    if(MINGW_WINPTHREAD_LIB)
        list(APPEND MINGW_RUNTIME_LIBS "${MINGW_WINPTHREAD_LIB}")
    endif()
endif()

add_library(single_header STATIC src/lib_impl.cc)
set_target_properties(single_header PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(single_header Vulkan::Vulkan stb tinyobjloader VulkanMemoryAllocator)

set(SOURCES src/context.cc
            src/himmeli.cc
            src/main.cc
            src/memory.cc
            src/utils.cc
            src/vklelu.cc
            src/vklelu_init.cc)

set(HEADERS src/context.hh
            src/himmeli.hh
            src/memory.hh
            src/utils.hh
            src/vklelu.hh)

add_executable(vklelu ${SOURCES} ${HEADERS})

if(WIN32)
    # This only works with generated VS solutions
    # When using VS builtin cmake support you must edit CMakeSettings.json instead
    set(VSENV "VKLELU_ASSETDIR=${CMAKE_SOURCE_DIR}/assets\nVKLELU_SHADERDIR=${CMAKE_SOURCE_DIR}/shaders")
    # Only set the correct subsystem for release builds
    # This way we can easily see console output in debug builds
    set_target_properties(vklelu PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>
                                            VS_DEBUGGER_ENVIRONMENT "${VSENV}")
endif()

if(UNIX AND NOT SYSTEM_SDL)
    target_link_options(vklelu PUBLIC "-Wl,--enable-new-dtags")
    set_target_properties(vklelu PROPERTIES INSTALL_RPATH "\${ORIGIN}")
endif()

if(MSVC)
    target_compile_options(vklelu PRIVATE /W4)
    target_compile_definitions(vklelu PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vklelu PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    # GCC gives this warning with designated initializers on C++20 but not C
    # Vulkan structs generally have sane defaults when left zero initialized
    target_compile_options(vklelu PRIVATE -Wno-missing-field-initializers)
endif()

add_dependencies(vklelu Shaders)
target_link_libraries(vklelu Vulkan::Vulkan
                             SDL3::SDL3
                             glm::glm
                             vk-bootstrap::vk-bootstrap
                             single_header)

install(TARGETS vklelu RUNTIME DESTINATION ".")

if(NOT SYSTEM_SDL)
    install(TARGETS SDL3-shared RUNTIME DESTINATION ".")
endif()

install(DIRECTORY assets shaders DESTINATION ".")

if(MINGW)
    if(MINGW_RUNTIME_LIBS)
        install(FILES ${MINGW_RUNTIME_LIBS} DESTINATION ".")
    else()
        message(WARNING "Remember to copy MinGW runtime libraries!")
    endif()
endif()
