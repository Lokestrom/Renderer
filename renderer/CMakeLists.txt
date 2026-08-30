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
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/.."
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../external"
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
