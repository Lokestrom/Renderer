cmake_minimum_required(VERSION 4.1)
project(RenderingEngineExternalConsumer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(repository_root "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

find_package(Vulkan REQUIRED COMPONENTS glslc)
set(RENDERING_ENGINE_USE_PROVIDED_VULKAN ON)
set(RENDERING_ENGINE_SHADER_OUTPUT_DIRECTORY
    "${CMAKE_CURRENT_BINARY_DIR}/shaders"
)
add_subdirectory("${repository_root}/external/glm" glm)
add_subdirectory("${repository_root}/external/glfw" glfw)

add_subdirectory("${repository_root}/src/rendering" rendering-engine)

add_executable(renderingEngineGlfwExample main.cpp)
target_link_libraries(renderingEngineGlfwExample PRIVATE
    RenderingEngine::RenderingEngine
    glfw
)
target_compile_definitions(renderingEngineGlfwExample PRIVATE
    RENDERING_ENGINE_EXAMPLE_CUBE_MODEL="${CMAKE_CURRENT_LIST_DIR}/cube.obj"
    RENDERING_ENGINE_EXAMPLE_PLANE_MODEL="${CMAKE_CURRENT_LIST_DIR}/plane.obj"
)
