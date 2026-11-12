lab_library(lab1_core
    SOURCES
        src/application.cpp
        src/graphics_internal.cpp
    SHADERS
        mesh.vert
        mesh.frag
)

lab_executable(lab1_main
    SOURCES app/main.cpp
    LINK lab1_core
)
