function(ts3_plugin NAME)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})

    add_library(${NAME} SHARED ${ARG_SOURCES})
    target_link_libraries(${NAME} PRIVATE ts3sdk)
    set_target_properties(${NAME} PROPERTIES PREFIX "")

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_arch win64)
    else()
        set(_arch win32)
    endif()

    set(_stage ${CMAKE_SOURCE_DIR}/dist/stage/${NAME})

    add_custom_command(TARGET ${NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_stage}/plugins
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${NAME}> ${_stage}/plugins/${NAME}_${_arch}.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/package.ini ${_stage}/package.ini
    )

    add_custom_target(package-${NAME}
        COMMAND ${CMAKE_COMMAND} -E chdir ${_stage}
            ${CMAKE_COMMAND} -E tar cf ${CMAKE_SOURCE_DIR}/dist/${NAME}.ts3_plugin --format=zip .
        DEPENDS ${NAME}
        COMMENT "Packing dist/${NAME}.ts3_plugin"
    )
endfunction()
