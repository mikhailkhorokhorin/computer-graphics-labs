# Lab 1. 3D Graphics Basics: Truncated Tetrahedron

## Task

Render a simple 3D object in real time with Vulkan, GLFW and ImGui and project it onto the screen with a perspective matrix.
Variant 4: a truncated regular tetrahedron, whose faces are 4 regular triangles and 4 regular hexagons.
The base task without extra tasks is done: the object rotates, and the rotation speed is changed in the interface.

## Build and run

Requirements: a C++20 compiler, CMake 3.20+ and the Vulkan SDK with `glslc` in `PATH`.

```bash
make run LAB=lab1
```

On Windows run `mingw32-make run LAB=lab1`; the preset is chosen by `PRESET=` (`mingw-debug` on Windows, `debug` on GNU/Linux).

## Example

The `Lab 1` window shows the frame rate and the `Rotation speed` slider in rad/s.

![Truncated tetrahedron](docs/screenshots/01.png)
![Hexagonal face](docs/screenshots/02.png)

Reports: [Russian](docs/report-ru.pdf), [English](docs/report-en.pdf).

## Notes

- The tetrahedron corners are $(1, 1, 1)$, $(1, -1, -1)$, $(-1, 1, -1)$, $(-1, -1, 1)$; every edge $A_iA_j$ is cut at one third, $P_{ij} = \frac{2A_i + A_j}{3}$, which gives 12 vertices, 18 edges and 8 faces.
- Every face has its own vertices and color; hexagons are split into 4 triangles, 20 triangles in total.
- The vertex shader gets one matrix $MVP = P \cdot V \cdot M$ from a uniform buffer.
- $P$ is `glm::perspectiveRH_ZO` with the $y$ axis flipped, because in Vulkan $y$ points down and depth is in $[0, 1]$.
