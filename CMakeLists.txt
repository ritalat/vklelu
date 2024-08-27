cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)
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
            src/struct_helpers.hh
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

if(UNIX)
    target_link_options(vklelu PUBLIC "-Wl,--enable-new-dtags")
    set_target_properties(vklelu PROPERTIES INSTALL_RPATH "\${ORIGIN}")
endif()

add_dependencies(vklelu Shaders)
target_link_libraries(vklelu Vulkan::Vulkan
                             SDL2::SDL2
                             SDL2::SDL2main
                             glm::glm
                             stb
                             tinyobjloader
                             vk-bootstrap::vk-bootstrap
                             VulkanMemoryAllocator)

install(TARGETS vklelu SDL2 RUNTIME DESTINATION ".")

install(DIRECTORY assets shaders DESTINATION ".")
