# 3D Fractal Explorer

This project is a web-based **3D Fractal Explorer** built with **Three.js** and **WebGL**. It uses ray-marching to render complex 3D fractals in real-time.

## Project Context
Contrary to initial assumptions, this is NOT a port of the Torque Game Engine. While the repository contains legacy C++ code (`old code/fxFractals.cpp`), it serves as a mathematical reference for fractal algorithms.

### Pickover Fractal
The `fxFractals.cpp` file contains an implementation of the **Pickover Fractal** (specifically a 3D chaotic attractor) used to render 3D objects by calculating point positions.

**Core Formula:**
```cpp
HypX = sin(InputA * posY) - posZ * cos(InputB * posX);
HypY = posZ * sin(InputC * posX) - cos(InputD * posY);
posZ = sin(posX);
posX = HypX;
posY = HypY;
```

## Implementation Goals
- [x] Document project context and Pickover formula.
- [x] Implement the **Pickover Fractal** in the web-based explorer.
- [x] Add interactive controls for Pickover parameters (`InputA`, `InputB`, `InputC`, `InputD`, `FractalScale`).
- [x] Support both ray-marching (SDF) and point-based (Attractor) rendering.

## Current Supported Fractals
- Mandelbulb
- Julia Set
- Menger Sponge
- Sierpinski Tetrahedron
- **Pickover**
