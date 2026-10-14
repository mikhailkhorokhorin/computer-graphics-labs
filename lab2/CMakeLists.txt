lab_library(lab2_core
    SOURCES
        src/application.cpp
        src/buffer.cpp
        src/camera.cpp
        src/descriptor.cpp
        src/graphics_internal.cpp
        src/lighting.cpp
        src/mesh.cpp
        src/pipeline.cpp
        src/shader.cpp
    SHADERS
        mesh.vert
        mesh.frag
)

lab_executable(lab2_main
    SOURCES app/main.cpp
    LINK lab2_core
)
