lab_library(lab1_core
    SOURCES
        src/application.cpp
        src/buffer.cpp
        src/descriptor.cpp
        src/graphics_internal.cpp
        src/mesh.cpp
        src/pipeline.cpp
        src/shader.cpp
    SHADERS
        mesh.vert
        mesh.frag
)

lab_executable(lab1_main
    SOURCES app/main.cpp
    LINK lab1_core
)
