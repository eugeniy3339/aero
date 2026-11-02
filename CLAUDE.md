# CLAUDE.md

AeroLBM: a Windows-only virtual wind tunnel. It runs a D3Q19 lattice-Boltzmann solver (BGK collision with Smagorinsky LES) on the CPU with OpenMP, renders with OpenGL 3.3 core through WGL, and builds its UI with Dear ImGui (docking branch).

## Build and run

There is no CMake. Build with the Visual Studio solution (`aero.slnx`, `x64`, `Debug`/`Release`). From a shell:

```bash
"/c/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/MSBuild.exe" aero.slnx -p:Configuration=Release -p:Platform=x64 -m -v:minimal -nologo
```

- Output goes to `x64/<Config>/aero.exe`. The saved panel layout, `aero_layout.ini`, is written next to the exe.
- Release links as a GUI app (`/SUBSYSTEM:WINDOWS` with `mainCRTStartup`, so the entry point stays `int main()`). Debug keeps a console, because shader compile and link errors go to `stderr`.
- Sources compile with `/utf-8`. The warnings `C4849: OpenMP 'collapse' clause ignored` are expected: MSVC's `/openmp` implements OpenMP 2.0, so only the outer loop runs in parallel.
- There are no automated tests. To verify a change, build both configurations, launch the app (it starts maximized), and press Space to run the solver. ImGui asserts only fire in Debug, so run Debug after UI changes.

## Layout

| File | Role |
|---|---|
| `aero/lbm.h/.cpp` | `Sim`: D3Q19 lattice tables, collide and stream step, boundaries, trilinear `sample()`. Self-contained, with no GL or UI code. |
| `aero/model.h/.cpp` | `Model`: FBX and OBJ import through ufbx, the built-in STL reader, procedural built-in shapes, and the UTF-8 to UTF-16 helpers `widen`/`narrow`. |
| `aero/gfx.h/.cpp` | GL helpers: shader programs with cached uniforms, `GLMesh`, `DynLines`, `DynQuad`, the MSAA `RenderTarget`, and the colour maps. |
| `aero/app.h` | The `App` struct (the single global `A`), `Particles`, `OrbitCamera`, and declarations for scene.cpp and ui.cpp. |
| `aero/scene.cpp` | Places and voxelises the model, rebuilds the domain, drives the solver each frame (`advanceSimulation`), and renders the viewport. |
| `aero/ui.cpp` | Everything in ImGui: theme and fonts, menu bar, status bar, dock layout, the Controls, Results and Viewport panels, overlays, and keyboard shortcuts. |
| `aero/main.cpp` | Win32 window, WGL context, message loop, drag and drop, DPI changes. |
| `aero/gl_loader.*` | Minimal GL loader. Add each new GL entry point to `AERO_GL_FUNCS`, otherwise it is a null pointer at runtime. |
| `include/` | Vendored third-party code: Dear ImGui 1.93 WIP (docking), GLM, ufbx, Khronos headers. Don't edit it. |

## Conventions and gotchas

- Coordinates are grid (lattice) units throughout scene and render code: 1 unit = 1 cell. Flow goes in +X, lift is +Y, the inlet is at x = 0, the outlet at x = nx-1.
- Physical units appear only in `flowScaling()` and `forces()` in scene.cpp. Use `A.sim.uLB`, the velocity the solver is actually running with, for anything physical. `A.uLB` is the UI value, and it is applied only on `resetFlow()`.
- Changing anything that affects the voxelisation (the model, its transform, the grid) goes through `applyModel()` or `rebuildDomain()`. Both reset the flow and the Cd/Cl history.
- Model-transform sliders commit on release (`IsItemDeactivatedAfterEdit`) so that dragging doesn't re-voxelise every frame.
- UI layout uses the `beginProps` and `prop(label, help)` helpers in ui.cpp, which give two-column label/widget tables. Give widgets `##id` labels and put the visible text in `prop()`. Reuse these helpers for new controls.
- Sizes in the UI are DPI-scaled through `ImGuiStyle::ScaleAllSizes` and `FontScaleDpi`. For custom draw-list overlays, multiply pixel constants by `uiScale()`.
- Non-ASCII text in string literals uses UTF-8 hex escapes (for example `"\xC2\xB0"` for °). Watch out for a hex escape running into a following hex digit; split the literal (`"\xCF\x84" "0"`).
- The code style is dense, `static` free functions per file, with few comments that explain *why*. Match the surrounding code.
