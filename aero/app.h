// ============================================================================
//  Application state shared by the scene (scene.cpp), UI (ui.cpp) and the
//  Win32 shell (main.cpp). There is a single global instance, A.
// ============================================================================
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "gfx.h"
#include "lbm.h"
#include "model.h"

#include <glm/glm.hpp>

#include <random>
#include <string>
#include <vector>

// Streamline tracer particles advected through the velocity field (grid units).
struct Particles {
    int P = 4000, T = 16, maxAge = 500;
    float speed = 1.0f, seedY = 0.5f, seedZ = 0.5f, alpha = 0.8f;
    bool on = true;
    std::vector<glm::vec3> tr;      // P trails of T points, newest first
    std::vector<int> age; std::vector<float> spd;
    std::mt19937 rng{12345};

    void init(const Sim& s);
    void step(const Sim& s);
private:
    float rnd() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng); }
    void respawn(int p, bool anywhere, const Sim& s);
};

struct OrbitCamera {
    float yaw = 1.2f, pitch = 0.3f, dist = 200.f;
    glm::vec3 target{0.f};
    glm::vec3 dir() const;          // unit vector from target towards the eye
    glm::mat4 view() const;
    void frame(const Sim& s);       // default view of the whole domain
};

// Physical <-> lattice scaling derived from the current settings.
struct FlowScaling { float Re, ReSim, nuLB, tau0, mach; };
// Force coefficients and the matching physical forces.
struct Forces { double Cd, Cl, Cs, Aref, drag, lift, side; };

enum SliceField    { Field_Speed, Field_Pressure, Field_Vorticity };
enum SurfaceColour { Surface_Solid, Surface_Pressure };
enum RefArea       { Ref_Frontal, Ref_Planform };

struct App {
    HWND hwnd = nullptr; HDC hdc = nullptr; HGLRC glrc = nullptr;
    Sim sim; Model model; bool haveModel = false;
    GLMesh mesh; std::vector<glm::vec3> gridPos, gridNrm, vcol;   // model in grid space
    Particles part;
    std::string status = "Open a model (Ctrl+O) or drop a file onto the window.";
    bool statusError = false;

    // model
    char path[1024] = "";
    int builtin = Builtin_Sphere;
    float modelSizeM = 1.0f, lenCells = 40.f, posX = 0.30f, yaw = 0, pitch = 0, roll = 0;
    bool zUp = false;
    int gridDims[3] = {128, 64, 64};
    // flow
    float wind = 30.f, rhoAir = 1.225f, nuAir = 1.5e-5f, uLB = 0.08f;   // uLB is applied on reset
    int refMode = Ref_Frontal;
    // run
    bool running = false; float budgetMs = 25.f; double mlups = 0;
    // visualisation
    bool showSlice = true, showBox = true;
    int sliceAxis = 2, sliceIdx = 32, sliceField = Field_Speed;
    float speedMax = 1.6f, cpRange = 1.5f, vortMax = 6.f, sliceAlpha = 0.85f;
    int colorMode = Surface_Solid; int cpFrame = 0;
    // voxel stats
    long long solidCount = 0; int areaFront = 1, areaPlan = 1;
    OrbitCamera cam;
    std::vector<float> hCd, hCl;    // coefficient history, one sample per frame while running
    // GL
    MeshProgram progMesh; LineProgram progLine; TexProgram progTex;
    RenderTarget view; DynQuad sliceQuad; DynLines trails, boxLines;
    GLuint sliceTex = 0;
    std::vector<uint8_t> sliceImg;
};
extern App A;

// ---- scene.cpp ----
void initScene();                           // GL resources + initial domain and model (needs a GL context)
void rebuildDomain();                       // reallocate the grid from A.gridDims
void applyModel();                          // re-place, re-voxelise and reset the flow
void loadModelPath(const std::string& path);
void useBuiltin(int which);
void resetFlow();                           // restart the solver with A.uLB
void advanceSimulation();                   // solver work for one frame + particles
void renderViewport(int w, int h);          // draws into A.view
int sliceCount();                           // number of slices along A.sliceAxis
FlowScaling flowScaling();
Forces forces();

// ---- ui.cpp ----
void setupStyle(float dpi);
void drawUI();
