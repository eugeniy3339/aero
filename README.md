# AeroLBM

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

### Load and position a model

1. In **Model**, click **Pick file** and select an `.fbx`, `.obj`, or `.stl` file. You can also drag a model file onto the application window.
2. Click **Load** to import the selected file. Alternatively, choose a built-in sphere or NACA 2412 wing and click **Use built-in**.
3. Adjust **Size**, **Length**, **Position X**, and the pitch, yaw, and roll controls. Enable **Model is Z-up** when appropriate for the source model.
4. If you change the simulation grid, click **Rebuild domain** in **Domain**.

### Run the flow

Set the wind speed, air density, and kinematic viscosity in **Flow**, then click **Run** in **Simulation**. **Lattice velocity** is applied when the flow is reset. Use **Reset flow** to restart the solver and clear the force-history plots. **Budget (ms/frame)** controls how much solver work can be done in each rendered frame.

Allow the flow to develop for several domain flow-throughs before interpreting the force coefficients. The Results panel reports the number of solver steps and throughput as well as the force and coefficient estimates.

### Explore the viewport

- Left-drag to orbit the camera.
- Right- or middle-drag to pan.
- Use the mouse wheel to zoom.
- In **Visualisation**, toggle the domain box, slice, and streamlines. Choose the slice axis, position, and field; adjust the display ranges and particle trails as needed.
- Surface colouring can show the pressure coefficient. **Reset camera** restores the default view.

## Project layout

- `aero/main.cpp` — application, model import, solver, visualisation, and UI.
- `aero/gl_loader.cpp` and `aero/gl_loader.h` — OpenGL function loading.
- `aero/aero.vcxproj` and `aero.slnx` — Visual Studio project and solution.
- `include/` — vendored Dear ImGui, GLM, ufbx, and OpenGL headers.

## License

See [LICENSE](LICENSE) for the project license. Vendored dependencies may have separate licenses in their respective subdirectories.
