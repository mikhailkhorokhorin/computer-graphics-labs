function(lab_current_name out_var)
    get_filename_component(name "${CMAKE_CURRENT_SOURCE_DIR}" NAME)
    set(${out_var} "${name}" PARENT_SCOPE)
endfunction()

function(lab_register target)
    lab_current_name(lab)
    if(NOT TARGET ${lab})
        add_custom_target(${lab})
    endif()
    add_dependencies(${lab} ${target})
endfunction()

function(lab_library name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;SHADERS")
    add_library(${name} STATIC ${ARG_SOURCES})
    target_include_directories(${name} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(${name} PUBLIC lab_dependencies)

    set(shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    set(shader_binaries)
    foreach(shader IN LISTS ARG_SHADERS)
        set(source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}")
        set(binary "${shader_dir}/${shader}.spv")
        add_custom_command(
            OUTPUT "${binary}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${shader_dir}"
            COMMAND "${GLSLC}" "${source}" -o "${binary}"
            DEPENDS "${source}"
            COMMENT "Compiling ${shader}"
        )
        list(APPEND shader_binaries "${binary}")
    endforeach()
    add_custom_target(${name}_shaders DEPENDS ${shader_binaries})
    add_dependencies(${name} ${name}_shaders)
    target_compile_definitions(${name} PRIVATE SHADER_DIR="${shader_dir}/")

    lab_register(${name})
endfunction()

function(lab_executable name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;LINK")
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_LINK})
    if(WIN32)
        if(MSVC)
            target_link_options(${name} PRIVATE /subsystem:windows /entry:mainCRTStartup)
        else()
            target_link_options(${name} PRIVATE -mwindows)
        endif()
    endif()
    lab_register(${name})
endfunction()
