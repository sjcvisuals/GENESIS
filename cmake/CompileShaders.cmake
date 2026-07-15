function(genesis_compile_shaders TARGET_NAME)
    set(SHADER_SOURCES ${ARGN})

    if(NOT GENESIS_GLSLC_EXECUTABLE)
        message(FATAL_ERROR "GENESIS_GLSLC_EXECUTABLE is not set. Include VulkanSetup.cmake first.")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/shaders/spv")

    set(SPV_OUTPUTS "")
    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_ABS ${SHADER} ABSOLUTE BASE_DIR ${CMAKE_SOURCE_DIR})
        get_filename_component(SHADER_NAME ${SHADER_ABS} NAME)
        set(SPV_OUTPUT "${CMAKE_BINARY_DIR}/shaders/spv/${SHADER_NAME}.spv")

        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SHADER_ABS}")

        execute_process(
            COMMAND ${GENESIS_GLSLC_EXECUTABLE} ${SHADER_ABS} -o ${SPV_OUTPUT}
            RESULT_VARIABLE _shader_result
            ERROR_VARIABLE _shader_error
        )
        if(_shader_result OR NOT EXISTS "${SPV_OUTPUT}")
            message(FATAL_ERROR "Failed to compile shader ${SHADER_NAME}: ${_shader_error}")
        endif()

        list(APPEND SPV_OUTPUTS ${SPV_OUTPUT})
        message(STATUS "GENESIS: compiled shader ${SHADER_NAME} -> ${SPV_OUTPUT}")
    endforeach()

    add_custom_target(${TARGET_NAME} DEPENDS ${SPV_OUTPUTS})
    set(GENESIS_SHADER_SPV ${SPV_OUTPUTS} PARENT_SCOPE)
endfunction()
