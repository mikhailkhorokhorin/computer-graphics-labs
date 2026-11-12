lab_library(lab3_core
    SOURCES
        src/application.cpp
        src/graphics_internal.cpp
    SHADERS
        mesh.vert
        mesh.frag
)
target_link_libraries(lab3_core PUBLIC lodepng)
target_compile_definitions(lab3_core PRIVATE TEXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/textures/")

lab_executable(lab3_main
    SOURCES app/main.cpp
    LINK lab3_core
)
