cmake_minimum_required(VERSION 4.1)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(RenderingEngine LANGUAGES C CXX)
    set(RENDERING_ENGINE_IS_TOP_LEVEL ON)
else()
    set(RENDERING_ENGINE_IS_TOP_LEVEL OFF)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RENDERING_ENGINE_USE_PROVIDED_VULKAN
    "Use existing Vulkan targets instead of finding the Vulkan SDK" OFF)

if(RENDERING_ENGINE_USE_PROVIDED_VULKAN)
    if(NOT TARGET Vulkan::Vulkan)
        message(FATAL_ERROR
            "RENDERING_ENGINE_USE_PROVIDED_VULKAN is ON, but Vulkan::Vulkan was not provided"
        )
    endif()
    if(NOT TARGET Vulkan::glslc)
        message(FATAL_ERROR
            "RENDERING_ENGINE_USE_PROVIDED_VULKAN is ON, but Vulkan::glslc was not provided"
        )
    endif()
else()
    find_package(Vulkan REQUIRED COMPONENTS glslc)
endif()

if(NOT TARGET glm)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/glm/CMakeLists.txt")
        message(FATAL_ERROR
            "GLM is unavailable. Initialize Renderer submodules with "
            "`git submodule update --init --recursive`."
        )
    endif()
    add_subdirectory(external/glm EXCLUDE_FROM_ALL)
endif()

add_subdirectory(renderer)

option(RENDERING_ENGINE_BUILD_EXAMPLES
    "Build RenderingEngine examples" ${RENDERING_ENGINE_IS_TOP_LEVEL})

if(RENDERING_ENGINE_BUILD_EXAMPLES)
    if(NOT TARGET glfw)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/CMakeLists.txt")
            message(FATAL_ERROR
                "GLFW is unavailable. Initialize Renderer submodules with "
                "`git submodule update --init --recursive`."
            )
        endif()
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        add_subdirectory(external/glfw EXCLUDE_FROM_ALL)
    endif()
    add_subdirectory(examples/glfw_consumer)
endif()
