add_executable(renderingEngineGlfwExample main.cpp)
target_link_libraries(renderingEngineGlfwExample PRIVATE
    RenderingEngine::RenderingEngine
    glfw
)
target_compile_definitions(renderingEngineGlfwExample PRIVATE
    RENDERING_ENGINE_EXAMPLE_CUBE_MODEL="${CMAKE_CURRENT_LIST_DIR}/cube.obj"
    RENDERING_ENGINE_EXAMPLE_PLANE_MODEL="${CMAKE_CURRENT_LIST_DIR}/plane.obj"
)
