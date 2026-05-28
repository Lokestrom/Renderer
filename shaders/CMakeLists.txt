find_program(GLSLC glslc)

file(GLOB SHADERS "*.vert" "*.frag")

foreach(SHADER ${SHADERS})
    get_filename_component(NAME ${SHADER} NAME)
    set(SPIRV "${CMAKE_BINARY_DIR}/shaders/${NAME}.spv")

    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${GLSLC} ${SHADER} -o ${SPIRV}
        DEPENDS ${SHADER}
    )

    list(APPEND SPV_FILES ${SPIRV})
endforeach()

message("Shaders compiled")
