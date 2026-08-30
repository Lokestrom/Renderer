cmake_minimum_required(VERSION 4.1)

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

file(GLOB renderer_sources
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)

add_library(RenderingEngine ${renderer_sources})
add_library(RenderingEngine::RenderingEngine ALIAS RenderingEngine)

target_compile_features(RenderingEngine PUBLIC cxx_std_23)
target_link_libraries(RenderingEngine PUBLIC
    glm
    Vulkan::Vulkan
)

target_include_directories(RenderingEngine
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../external"
)

option(RENDERING_ENGINE_ENABLE_VALIDATION
    "Enable Vulkan validation outside Debug configurations" OFF)
option(RENDERING_ENGINE_UNLIMITED_FRAME_RATE
    "Prefer mailbox/immediate presentation when supported" ON)

target_compile_definitions(RenderingEngine PUBLIC VULKAN_HPP_NO_CONSTRUCTORS)
target_compile_definitions(RenderingEngine PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<BOOL:${RENDERING_ENGINE_ENABLE_VALIDATION}>>:RENDERING_ENGINE_USE_VULKAN_DEBUG_REPORT>
    $<$<BOOL:${RENDERING_ENGINE_UNLIMITED_FRAME_RATE}>:RENDERING_ENGINE_USE_UNLIMITED_FRAME_RATE>
)

add_subdirectory(shaders)
