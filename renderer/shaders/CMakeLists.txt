if(NOT TARGET Vulkan::glslc)
    message(FATAL_ERROR "The Vulkan::glslc executable target was not provided")
endif()

if(NOT DEFINED RENDERING_ENGINE_SHADER_OUTPUT_DIRECTORY)
    set(RENDERING_ENGINE_SHADER_OUTPUT_DIRECTORY
        "${CMAKE_CURRENT_BINARY_DIR}/out"
        CACHE PATH
        "Directory where RenderingEngine writes its compiled SPIR-V shaders"
    )
endif()
get_filename_component(RENDERING_ENGINE_RESOLVED_SHADER_OUTPUT_DIRECTORY
    "${RENDERING_ENGINE_SHADER_OUTPUT_DIRECTORY}"
    ABSOLUTE
    BASE_DIR "${CMAKE_BINARY_DIR}"
)
file(GLOB RENDERING_ENGINE_SHADER_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.frag"
)

set(RENDERING_ENGINE_REQUIRED_SHADER_SOURCES
    basic.vert
    basic.frag
    shadow.vert
)
foreach(REQUIRED_SHADER IN LISTS RENDERING_ENGINE_REQUIRED_SHADER_SOURCES)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${REQUIRED_SHADER}")
        message(FATAL_ERROR
            "Required RenderingEngine shader is missing: ${REQUIRED_SHADER}"
        )
    endif()
endforeach()

set(RENDERING_ENGINE_SPIRV_FILES)
foreach(SOURCE_FILE IN LISTS RENDERING_ENGINE_SHADER_SOURCES)
    get_filename_component(SHADER_NAME "${SOURCE_FILE}" NAME)
    set(OUTPUT_FILE
        "${RENDERING_ENGINE_RESOLVED_SHADER_OUTPUT_DIRECTORY}/${SHADER_NAME}.spirv"
    )

    add_custom_command(
        OUTPUT "${OUTPUT_FILE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${RENDERING_ENGINE_RESOLVED_SHADER_OUTPUT_DIRECTORY}"
        COMMAND "$<TARGET_FILE:Vulkan::glslc>"
                "${SOURCE_FILE}"
                -o "${OUTPUT_FILE}"
        DEPENDS "${SOURCE_FILE}" Vulkan::glslc
        COMMENT "Compiling GLSL shader ${SHADER_NAME}"
        VERBATIM
    )

    list(APPEND RENDERING_ENGINE_SPIRV_FILES "${OUTPUT_FILE}")
endforeach()

add_custom_target(RenderingEngineShaders
    DEPENDS ${RENDERING_ENGINE_SPIRV_FILES}
    SOURCES ${RENDERING_ENGINE_SHADER_SOURCES}
)
add_dependencies(RenderingEngine RenderingEngineShaders)

target_compile_definitions(RenderingEngine PRIVATE
    RENDERING_ENGINE_SHADER_DIRECTORY="${RENDERING_ENGINE_RESOLVED_SHADER_OUTPUT_DIRECTORY}"
)
