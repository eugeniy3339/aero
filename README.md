# AeroLBM

> **Disclaimer:** This is a personal test project exploring the possibilities of AI.

AeroLBM is a Windows desktop virtual wind tunnel for exploring airflow around 3D models. It combines a lattice-Boltzmann flow solver with an interactive OpenGL viewport and a Dear ImGui control panel.

## Download

Download the latest Windows build from the [GitHub Releases page](https://github.com/eugeniy3339/aero/releases/latest). Releases will be published for major project updates; check each release's notes for changes and its assets for downloadable builds.

## Features

- Import FBX and OBJ models through [ufbx](https://github.com/ufbx/ufbx), and binary or ASCII STL models through the built-in reader.
- Choose a model with **Pick file**, enter a path manually, or drag a model file onto the application window. Built-in sphere and NACA 2412 wing models are also available.
- Voxelise the model into a 3D simulation domain and adjust model scale, position, and orientation.
- Simulate flow with a D3Q19 lattice-Boltzmann method, BGK collision, and Smagorinsky large-eddy turbulence model.
- Inspect velocity, pressure coefficient, and vorticity slices, streamlines, and surface pressure-coefficient colouring.
- View estimated drag, lift, and side-force coefficients, physical forces, Reynolds number, and simulation performance.

The solver's flow direction is **+X** and lift is **+Y**. Rotate imported models so their nose points toward +X. The displayed results are numerical estimates and depend on grid resolution, model voxelisation, flow settings, and how long the simulation has developed.

## Build

This project targets Windows and uses Visual Studio C++ project files. The third-party source and headers it needs are vendored under `include/`.

1. Install Visual Studio with the **Desktop development with C++** workload, the MSVC v143 toolset, and a Windows 10 SDK.
2. Open `aero.slnx` in Visual Studio.
3. Select the `x64` platform and either `Debug` or `Release` configuration.
4. Build and run the `aero` project.

The application uses Win32, WGL, and OpenGL 3.3. No separate dependency download step is required.

## Using the application

The window opens maximized with three panels: **Controls** on the left, the 3D **Viewport** in the middle and **Results** on the right. Panels can be dragged and re-docked; **View → Reset panel layout** restores the default arrangement. A status bar at the bottom shows the grid size, step count, throughput and whether the solver is running.

### Load and position a model

1. Click **Open model...** (or press **Ctrl+O**) and select an `.fbx`, `.obj`, or `.stl` file. The model loads immediately. You can also type a path and press Enter, or drag a model file onto the window.
2. Alternatively, choose a built-in sphere or NACA 2412 wing from the **Built-in** list or **File → Built-in model**.
3. Under **Placement**, adjust **Size**, **Length**, **Position X**, and the pitch, yaw, and roll controls. Enable **Z-up model** when appropriate for the source model. Hover a label for a short explanation.
4. To change the simulation grid, edit **Grid** in **Domain** and click **Rebuild domain**.

### Run the flow

Set the wind speed, air density, and kinematic viscosity in **Flow**, then click **Run** (or press **Space**). **Lattice velocity** is applied when the flow is reset; the panel offers an **Apply** button when it has changed. **Reset flow** (**R**) restarts the solver and clears the force-history plots. **Budget** controls how much solver time is spent per rendered frame.

The **Convergence** bar in Results shows how many times the free stream has crossed the domain. Wait for about three flow-throughs before interpreting the force coefficients.

### Explore the viewport

- Left-drag to orbit the camera, right- or middle-drag to pan, and use the mouse wheel to zoom. Double-click or press **F** to reset the camera.
- The toolbar above the viewport toggles the domain box, slice, streamlines, and surface pressure colouring, and selects the slice field. A colour legend and an axis gizmo are drawn over the view; the inlet face of the domain box is green.
- Slice axis, position, colour range and opacity, and the streamline settings are under **Visualisation** in Controls.
- **Help → Controls** (**F1**) lists all mouse and keyboard shortcuts.

## Project layout

- `aero/main.cpp` — Win32 window, OpenGL context, and main loop.
- `aero/lbm.cpp` — lattice-Boltzmann solver.
- `aero/model.cpp` — model import and built-in shapes.
- `aero/scene.cpp` — voxelisation, simulation driving, and 3D rendering.
- `aero/ui.cpp` — Dear ImGui interface.
- `aero/gfx.cpp`, `aero/gl_loader.cpp` — OpenGL helpers and function loading.
- `aero/aero.vcxproj` and `aero.slnx` — Visual Studio project and solution.
- `include/` — vendored Dear ImGui, GLM, ufbx, and OpenGL headers.

## License

See [LICENSE](LICENSE) for the project license. Vendored dependencies may have separate licenses in their respective subdirectories.
