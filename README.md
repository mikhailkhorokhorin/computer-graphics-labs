# Computer Graphics Labs

C++20 labs on real-time 3D graphics with Vulkan, GLFW and Dear ImGui, built on top of
[vulkan-starter-app](https://github.com/vladeemerr/vulkan-starter-app).
Every lab evolves the same application; the final state of each lab is marked with a git tag.

| No. | Topic | Tag |
| --- | --- | --- |
| 1 | [3D Graphics Basics: Truncated Tetrahedron](docs/lab1/) | `lab1` |

## Build and run

Requirements: a C++20 compiler, CMake 3.20+ and the Vulkan SDK with `glslc` in `PATH`.

```bash
cmake --preset mingw-debug
cmake --build build-debug --parallel
ctest --test-dir build-debug --output-on-failure
./build-debug/vulkan-starter-app
```

Use `debug` instead of `mingw-debug` on GNU/Linux and `msvc-debug` with Visual Studio.
Run the application from the repository root, it loads compiled shaders from `shaders/`.

## Layout

- `source/main.cpp`, `source/graphics_internal.*` - window, Vulkan and ImGui setup from the starter.
- `source/graphics.*` - buffer and shader module helpers.
- `source/geometry.*`, `source/transform.*` - meshes and matrices, no Vulkan dependency.
- `source/application.cpp` - scene, descriptors, pipeline, interface and rendering.
- `shaders/` - GLSL sources, compiled to SPIR-V by the build.
- `tests/` - GoogleTest unit tests for the geometry and the matrices.
- `docs/labN/` - task, variant and reports of each lab.

## Starter updates

```bash
git remote add upstream https://github.com/vladeemerr/vulkan-starter-app.git
git fetch upstream
git merge upstream/master
```
