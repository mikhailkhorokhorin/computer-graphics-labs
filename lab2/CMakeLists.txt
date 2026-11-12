lab_library(lab2_core
    SOURCES
        src/application.cpp
        src/graphics_internal.cpp
    SHADERS
        mesh.vert
        mesh.frag
)

lab_executable(lab2_main
    SOURCES app/main.cpp
    LINK lab2_core
)
