lab_library(lab4_core
    SOURCES
        src/application.cpp
        src/graphics_internal.cpp
    SHADERS
        mesh.vert
        mesh.frag
        shadow.vert
)
target_link_libraries(lab4_core PUBLIC lodepng)
target_compile_definitions(lab4_core PRIVATE TEXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/textures/")

lab_executable(lab4_main
    SOURCES app/main.cpp
    LINK lab4_core
)
