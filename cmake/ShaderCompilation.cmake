# Embeds GLSL shader source files as C++ string constants.
# Usage: embed_shaders(TARGET <tgt> SHADERS <file1.vert> <file2.frag> ...)
# Generates <shader_name>_vert.h / <shader_name>_frag.h in the build directory.
function(embed_shaders)
    cmake_parse_arguments(ARG "" "TARGET" "SHADERS" ${ARGN})
    foreach(SHADER_FILE ${ARG_SHADERS})
        get_filename_component(SHADER_NAME "${SHADER_FILE}" NAME_WE)
        get_filename_component(SHADER_EXT  "${SHADER_FILE}" EXT)
        string(REPLACE "." "" SHADER_EXT "${SHADER_EXT}")
        set(OUT_HEADER "${CMAKE_BINARY_DIR}/generated/shaders/${SHADER_NAME}_${SHADER_EXT}.h")
        add_custom_command(
            OUTPUT "${OUT_HEADER}"
            COMMAND ${CMAKE_COMMAND}
                -DINPUT="${SHADER_FILE}"
                -DOUTPUT="${OUT_HEADER}"
                -DVAR_NAME="${SHADER_NAME}_${SHADER_EXT}"
                -P "${CMAKE_SOURCE_DIR}/cmake/EmbedShaderFile.cmake"
            DEPENDS "${SHADER_FILE}"
            COMMENT "Embedding shader ${SHADER_FILE}"
        )
        target_sources(${ARG_TARGET} PRIVATE "${OUT_HEADER}")
    endforeach()
endfunction()
