// ============================================================================
//  Dear ImGui user interface: theme, menu bar, dock layout, panels, viewport
//  overlays and the status bar.
// ============================================================================
#include "app.h"

#include <commdlg.h>           // open file dialog
#pragma comment(lib, "Comdlg32.lib")

#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder, BeginViewportSideBar

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <iterator>

using glm::vec3; using glm::mat3;

static constexpr float BASE_FONT_SIZE = 16.f;
static ImFont* g_monoFont = nullptr;          // numeric read-outs; nullptr = use the UI font
static bool g_resetLayout = false, g_showHelp = false, g_showAbout = false;

static const ImVec4 COL_ACCENT  = ImVec4(0.30f, 0.55f, 1.00f, 1.f);
static const ImVec4 COL_GOOD    = ImVec4(0.36f, 0.80f, 0.50f, 1.f);
static const ImVec4 COL_WARN    = ImVec4(0.95f, 0.70f, 0.25f, 1.f);
static const ImVec4 COL_ERROR   = ImVec4(1.00f, 0.38f, 0.38f, 1.f);

// ============================================================================
//  Style
// ============================================================================
static ImVec4 hex(unsigned rgb, float a = 1.f) {
    return ImVec4(((rgb >> 16) & 0xff) / 255.f, ((rgb >> 8) & 0xff) / 255.f, (rgb & 0xff) / 255.f, a);
}

static ImFont* addSystemFont(const char* file) {
    char dir[MAX_PATH];
    if (!GetWindowsDirectoryA(dir, MAX_PATH)) return nullptr;
    std::string path = std::string(dir) + "\\Fonts\\" + file;
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return nullptr;
    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), BASE_FONT_SIZE);
}

void setupStyle(float dpi) {
    static bool fontsLoaded = false;
    if (!fontsLoaded) {
        fontsLoaded = true;
        if (!addSystemFont("segoeui.ttf")) ImGui::GetIO().Fonts->AddFontDefault();
        g_monoFont = addSystemFont("consola.ttf");
    }

    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();                 // start from unscaled defaults so DPI changes don't compound
    ImGui::StyleColorsDark(&st);
    st.FontSizeBase = BASE_FONT_SIZE;

    st.WindowPadding = ImVec2(10, 10); st.FramePadding = ImVec2(8, 4); st.CellPadding = ImVec2(6, 3);
    st.ItemSpacing = ImVec2(8, 6); st.ItemInnerSpacing = ImVec2(6, 4); st.IndentSpacing = 14;
    st.ScrollbarSize = 12; st.GrabMinSize = 10;
    st.WindowBorderSize = 0; st.ChildBorderSize = 1; st.PopupBorderSize = 1; st.FrameBorderSize = 0; st.TabBorderSize = 0;
    st.WindowRounding = 6; st.ChildRounding = 4; st.FrameRounding = 4; st.PopupRounding = 4;
    st.ScrollbarRounding = 6; st.GrabRounding = 3; st.TabRounding = 4;
    st.WindowMenuButtonPosition = ImGuiDir_None;
    st.DockingSeparatorSize = 2;
    st.SeparatorTextBorderSize = 1; st.SeparatorTextPadding = ImVec2(0, 4);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = hex(0xE3E6EB);
    c[ImGuiCol_TextDisabled]         = hex(0x8A919E);
    c[ImGuiCol_WindowBg]             = hex(0x1B1E24);
    c[ImGuiCol_ChildBg]              = hex(0x1B1E24, 0.f);
    c[ImGuiCol_PopupBg]              = hex(0x20242B, 0.98f);
    c[ImGuiCol_Border]               = hex(0x2E333C);
    c[ImGuiCol_BorderShadow]         = hex(0x000000, 0.f);
    c[ImGuiCol_FrameBg]              = hex(0x272B33);
    c[ImGuiCol_FrameBgHovered]       = hex(0x2F3540);
    c[ImGuiCol_FrameBgActive]        = hex(0x363E4C);
    c[ImGuiCol_TitleBg]              = hex(0x15171C);
    c[ImGuiCol_TitleBgActive]        = hex(0x15171C);
    c[ImGuiCol_TitleBgCollapsed]     = hex(0x15171C);
    c[ImGuiCol_MenuBarBg]            = hex(0x15171C);
    c[ImGuiCol_ScrollbarBg]          = hex(0x1B1E24, 0.f);
    c[ImGuiCol_ScrollbarGrab]        = hex(0x353B46);
    c[ImGuiCol_ScrollbarGrabHovered] = hex(0x434A57);
    c[ImGuiCol_ScrollbarGrabActive]  = hex(0x505968);
    c[ImGuiCol_CheckMark]            = COL_ACCENT;
    c[ImGuiCol_SliderGrab]           = hex(0x4D8DFF, 0.85f);
    c[ImGuiCol_SliderGrabActive]     = COL_ACCENT;
    c[ImGuiCol_Button]               = hex(0x2C323C);
    c[ImGuiCol_ButtonHovered]        = hex(0x38414F);
    c[ImGuiCol_ButtonActive]         = hex(0x434F62);
    c[ImGuiCol_Header]               = hex(0x262B34);
    c[ImGuiCol_HeaderHovered]        = hex(0x2F3644);
    c[ImGuiCol_HeaderActive]         = hex(0x384254);
    c[ImGuiCol_Separator]            = hex(0x2E333C);
    c[ImGuiCol_SeparatorHovered]     = hex(0x4D8DFF, 0.6f);
    c[ImGuiCol_SeparatorActive]      = COL_ACCENT;
    c[ImGuiCol_ResizeGrip]           = hex(0x000000, 0.f);
    c[ImGuiCol_ResizeGripHovered]    = hex(0x4D8DFF, 0.6f);
    c[ImGuiCol_ResizeGripActive]     = COL_ACCENT;
    c[ImGuiCol_Tab]                  = hex(0x15171C);
    c[ImGuiCol_TabHovered]           = hex(0x2F3644);
    c[ImGuiCol_TabSelected]          = hex(0x1B1E24);
    c[ImGuiCol_TabSelectedOverline]  = COL_ACCENT;
    c[ImGuiCol_TabDimmed]            = hex(0x15171C);
    c[ImGuiCol_TabDimmedSelected]    = hex(0x1B1E24);
    c[ImGuiCol_TabDimmedSelectedOverline] = hex(0x4D8DFF, 0.f);
    c[ImGuiCol_DockingPreview]       = hex(0x4D8DFF, 0.5f);
    c[ImGuiCol_DockingEmptyBg]       = hex(0x131519);
    c[ImGuiCol_PlotLines]            = COL_ACCENT;
    c[ImGuiCol_PlotLinesHovered]     = hex(0x8FB8FF);
    c[ImGuiCol_TableHeaderBg]        = hex(0x20242B);
    c[ImGuiCol_TableBorderStrong]    = hex(0x2E333C);
    c[ImGuiCol_TableBorderLight]     = hex(0x262A31);
    c[ImGuiCol_TableRowBgAlt]        = hex(0xFFFFFF, 0.02f);
    c[ImGuiCol_TextSelectedBg]       = hex(0x4D8DFF, 0.35f);
    c[ImGuiCol_NavCursor]            = COL_ACCENT;
    c[ImGuiCol_ModalWindowDimBg]     = hex(0x000000, 0.5f);

    st.ScaleAllSizes(dpi);
    st.FontScaleDpi = dpi;
}

// ============================================================================
//  Widget helpers
// ============================================================================
static float uiScale() { return ImGui::GetStyle().FontScaleDpi; }

// Two-column "label | widget" tables so every control lines up.
static bool beginProps(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_None)) return false;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9.f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

// Starts a row: draws the label (with an optional hover tooltip) and readies a full-width widget.
static void prop(const char* label, const char* help = nullptr) {
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", help);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

// Read-only "label ...... value" row for result tables.
static void valueRow(const char* label, const char* fmt, ...) IM_FMTARGS(2);
static void valueRow(const char* label, const char* fmt, ...) {
    char buf[128];
    va_list args; va_start(args, fmt); std::vsnprintf(buf, sizeof(buf), fmt, args); va_end(args);
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
    ImGui::PushFont(g_monoFont, 0.f);
    ImGui::TextUnformatted(buf);
    ImGui::PopFont();
}

static bool coloredButton(const char* label, const ImVec4& col, const ImVec2& size = ImVec2(0, 0)) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x * 0.75f, col.y * 0.75f, col.z * 0.75f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x * 0.88f, col.y * 0.88f, col.z * 0.88f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

// Small toggle that looks pressed while `v` is true.
static bool toggleButton(const char* label, bool* v) {
    if (*v) {
        ImGui::PushStyleColor(ImGuiCol_Button, hex(0x2D5290));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hex(0x3762AA));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, hex(0x4171C2));
    }
    bool pressed = ImGui::Button(label);
    if (*v) ImGui::PopStyleColor(3);
    if (pressed) *v = !*v;
    return pressed;
}

// ============================================================================
//  Actions
// ============================================================================
static void openModelDialog() {
    wchar_t file[32768] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = A.hwnd;
    ofn.lpstrFilter = L"3D models (*.fbx;*.obj;*.stl)\0*.fbx;*.obj;*.stl\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = (DWORD)_countof(file);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = L"Open a 3D model";
    if (!GetOpenFileNameW(&ofn)) return;
    std::string selected = narrow(file);
    std::snprintf(A.path, sizeof(A.path), "%s", selected.c_str());
    loadModelPath(selected);
}

static void toggleRunning() {
    if (A.sim.diverged) return;
    A.running = !A.running;
}

static bool modelIsBuiltin() { return A.model.name.rfind("Built-in", 0) == 0; }

static void handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) openModelDialog();
    if (io.WantTextInput || io.KeyCtrl || io.KeyAlt) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) toggleRunning();
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) resetFlow();
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) || ImGui::IsKeyPressed(ImGuiKey_Home, false)) A.cam.frame(A.sim);
    if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) g_showHelp = true;
}

// ============================================================================
//  Menu bar, status bar, dock layout
// ============================================================================
static void drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open model...", "Ctrl+O")) openModelDialog();
        if (ImGui::BeginMenu("Built-in model")) {
            for (int i = 0; i < Builtin_Count; ++i)
                if (ImGui::MenuItem(BUILTIN_NAMES[i])) useBuiltin(i);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) PostMessageW(A.hwnd, WM_CLOSE, 0, 0);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Simulation")) {
        if (ImGui::MenuItem(A.running ? "Pause" : "Run", "Space", false, !A.sim.diverged)) toggleRunning();
        if (ImGui::MenuItem("Reset flow", "R")) resetFlow();
        if (ImGui::MenuItem("Rebuild domain")) rebuildDomain();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Domain box", nullptr, &A.showBox);
        ImGui::MenuItem("Slice", nullptr, &A.showSlice);
        ImGui::MenuItem("Streamlines", nullptr, &A.part.on);
        bool cp = A.colorMode == Surface_Pressure;
        if (ImGui::MenuItem("Surface pressure", nullptr, &cp)) A.colorMode = cp ? Surface_Pressure : Surface_Solid;
        ImGui::Separator();
        if (ImGui::MenuItem("Reset camera", "F")) A.cam.frame(A.sim);
        if (ImGui::MenuItem("Reset panel layout")) g_resetLayout = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Controls", "F1")) g_showHelp = true;
        if (ImGui::MenuItem("About AeroLBM")) g_showAbout = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

static void drawStatusBar() {
    const ImGuiWindowFlags fl = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##statusbar", ImGui::GetMainViewport(), ImGuiDir_Down, ImGui::GetFrameHeight(), fl)) {
        if (ImGui::BeginMenuBar()) {
            if (A.statusError) ImGui::TextColored(COL_ERROR, "%s", A.status.c_str());
            else ImGui::TextUnformatted(A.status.c_str());

            const Sim& s = A.sim;
            char right[192];
            std::snprintf(right, sizeof(right), "%d x %d x %d   |   %lld steps   |   %.1f MLUPS   |   %.0f FPS",
                          s.nx, s.ny, s.nz, s.steps, A.running ? A.mlups : 0.0, ImGui::GetIO().Framerate);
            const char* state = s.diverged ? "DIVERGED" : A.running ? "RUNNING" : "PAUSED";
            const ImVec4 stateCol = s.diverged ? COL_ERROR : A.running ? COL_GOOD : COL_WARN;
            const float sp = ImGui::GetStyle().ItemSpacing.x;
            const float w = ImGui::CalcTextSize(right).x + ImGui::CalcTextSize(state).x + 3 * sp;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX() + sp, ImGui::GetWindowWidth() - w));
            ImGui::TextDisabled("%s", right);
            ImGui::SameLine();
            ImGui::TextColored(stateCol, "%s", state);
            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
}

static void drawDockspace() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImGuiID dock = ImGui::GetID("MainDock");
    if (g_resetLayout || !ImGui::DockBuilderGetNode(dock)) {   // first run (no saved layout) or reset
        g_resetLayout = false;
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dock, vp->WorkPos);
        ImGui::DockBuilderSetNodeSize(dock, vp->WorkSize);
        ImGuiID rest, center;
        ImGuiID left = ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.22f, nullptr, &rest);
        ImGuiID right = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.25f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Controls", left);
        ImGui::DockBuilderDockWindow("Results", right);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderFinish(dock);
    }
    ImGui::DockSpaceOverViewport(dock, vp, ImGuiDockNodeFlags_PassthruCentralNode);
}

// ============================================================================
//  Controls panel
// ============================================================================
static void drawModelSection(bool& dirty) {
    const Sim& s = A.sim;
    auto commit = [&] { if (ImGui::IsItemDeactivatedAfterEdit()) dirty = true; };

    if (ImGui::Button("Open model...", ImVec2(-FLT_MIN, 0))) openModelDialog();
    if (beginProps("##model")) {
        prop("File", "Path to an .fbx, .obj or .stl file. Press Enter to load.\nYou can also drop a file onto the window.");
        const float loadW = ImGui::CalcTextSize("Load").x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetNextItemWidth(-(loadW + ImGui::GetStyle().ItemSpacing.x));
        bool enter = ImGui::InputTextWithHint("##path", "path to model file", A.path, sizeof(A.path), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(A.path[0] == 0);
        if (ImGui::Button("Load") || enter) loadModelPath(A.path);
        ImGui::EndDisabled();

        prop("Built-in", "Procedural test shapes.");
        if (ImGui::BeginCombo("##builtin", modelIsBuiltin() ? BUILTIN_NAMES[A.builtin] : "Choose...")) {
            for (int i = 0; i < Builtin_Count; ++i)
                if (ImGui::Selectable(BUILTIN_NAMES[i], modelIsBuiltin() && A.builtin == i)) useBuiltin(i);
            ImGui::EndCombo();
        }
        if (A.haveModel) {
            prop("Current");
            ImGui::TextDisabled("%s, %zu tris", A.model.name.c_str(), A.model.idx.size() / 3);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Placement");
    if (beginProps("##placement")) {
        prop("Size (m)", "Physical length of the model's longest side.\nUsed for the Reynolds number and physical forces.");
        ImGui::SliderFloat("##size", &A.modelSizeM, 0.01f, 50.f, "%.2f m", ImGuiSliderFlags_Logarithmic);
        prop("Length (cells)", "Grid cells spanned by the model's longest side.\nMore cells = finer resolution but slower.");
        ImGui::SliderFloat("##len", &A.lenCells, 10.f, 0.8f * s.nx, "%.0f"); commit();
        prop("Position X", "Model centre as a fraction of the domain length.");
        ImGui::SliderFloat("##posx", &A.posX, 0.15f, 0.6f, "%.2f"); commit();
        prop("Pitch / AoA", "Rotation about Z (angle of attack).");
        ImGui::SliderFloat("##pitch", &A.pitch, -90.f, 90.f, "%.1f\xC2\xB0"); commit();
        prop("Yaw");
        ImGui::SliderFloat("##yaw", &A.yaw, -180.f, 180.f, "%.1f\xC2\xB0"); commit();
        prop("Roll");
        ImGui::SliderFloat("##roll", &A.roll, -180.f, 180.f, "%.1f\xC2\xB0"); commit();
        prop("Z-up model", "Rotate a Z-up source model so it is Y-up.");
        if (ImGui::Checkbox("##zup", &A.zUp)) dirty = true;
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Flow goes +X, lift is +Y. Point the nose at +X.");
}

static void drawFlowSection() {
    Sim& s = A.sim;
    if (!beginProps("##flow")) return;
    prop("Wind speed", "Free-stream speed (physical).");
    ImGui::SliderFloat("##wind", &A.wind, 1.f, 300.f, "%.1f m/s");
    prop("Air density");
    ImGui::InputFloat("##rho", &A.rhoAir, 0, 0, "%.4f kg/m\xC2\xB3");
    prop("Kinematic visc.", "Kinematic viscosity of the fluid (air: 1.5e-5 m\xC2\xB2/s).");
    ImGui::InputFloat("##nu", &A.nuAir, 0, 0, "%.3e m\xC2\xB2/s");
    prop("Lattice velocity", "Free-stream speed in lattice units. Higher runs faster\nbut is less accurate and less stable (Mach = u\xE2\x88\x9A" "3).\nApplied when the flow is reset.");
    ImGui::SliderFloat("##ulb", &A.uLB, 0.02f, 0.15f, "%.3f");
    prop("Smagorinsky C", "LES sub-grid model constant. Raise if the solver diverges.");
    ImGui::SliderFloat("##smag", &s.smagC, 0.f, 0.3f, "%.3f");
    ImGui::EndTable();
    if (A.uLB != s.uLB) {
        ImGui::TextColored(COL_WARN, "Lattice velocity changed.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Apply (reset flow)")) resetFlow();
    }
}

static void drawDomainSection() {
    const Sim& s = A.sim;
    if (beginProps("##domain")) {
        prop("Grid (x y z)", "Domain size in cells (32-384 per axis).");
        ImGui::InputInt3("##grid", A.gridDims);
        ImGui::EndTable();
    }
    const double cells = (double)A.gridDims[0] * A.gridDims[1] * A.gridDims[2];
    ImGui::TextDisabled("%.2f M cells, ~%.0f MB", cells / 1e6, cells * (19 * 2 + 4 + 1) * 4 / 1e6);
    const bool changed = A.gridDims[0] != s.nx || A.gridDims[1] != s.ny || A.gridDims[2] != s.nz;
    if (changed ? coloredButton("Rebuild domain", COL_ACCENT, ImVec2(-FLT_MIN, 0)) : ImGui::Button("Rebuild domain", ImVec2(-FLT_MIN, 0)))
        rebuildDomain();
}

static void drawSimulationSection() {
    const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    ImGui::BeginDisabled(A.sim.diverged);
    if (A.running ? coloredButton("Pause", COL_WARN, ImVec2(w, 0)) : coloredButton("Run", COL_GOOD, ImVec2(w, 0)))
        toggleRunning();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reset flow", ImVec2(-FLT_MIN, 0))) resetFlow();
    if (beginProps("##sim")) {
        prop("Budget", "Solver time per rendered frame. Higher = faster\nsimulation but a less responsive UI.");
        ImGui::SliderFloat("##budget", &A.budgetMs, 2.f, 100.f, "%.0f ms/frame");
        ImGui::EndTable();
    }
}

static void drawVisualisationSection() {
    Sim& s = A.sim;
    toggleButton("Box", &A.showBox); ImGui::SameLine();
    toggleButton("Slice", &A.showSlice); ImGui::SameLine();
    toggleButton("Streamlines", &A.part.on); ImGui::SameLine();
    bool cp = A.colorMode == Surface_Pressure;
    if (toggleButton("Surface Cp", &cp)) A.colorMode = cp ? Surface_Pressure : Surface_Solid;

    ImGui::SeparatorText("Slice");
    ImGui::BeginDisabled(!A.showSlice);
    if (beginProps("##slice")) {
        prop("Field");
        ImGui::Combo("##field", &A.sliceField, "Speed |u|/U\0Pressure Cp\0Vorticity\0");
        prop("Axis");
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine();
            const char* names[3] = {"X", "Y", "Z"};
            if (ImGui::RadioButton(names[i], A.sliceAxis == i)) { A.sliceAxis = i; A.sliceIdx = sliceCount() / 2; }
        }
        const int mx = sliceCount() - 1;
        A.sliceIdx = std::clamp(A.sliceIdx, 0, mx);
        prop("Position");
        ImGui::SliderInt("##pos", &A.sliceIdx, 0, mx);
        if (A.sliceField == Field_Speed)         { prop("Speed max", "Colour scale top, in multiples of the free-stream speed."); ImGui::SliderFloat("##smax", &A.speedMax, 0.5f, 3.f, "%.2f"); }
        else if (A.sliceField == Field_Pressure) { prop("Cp range", "Colour scale is \xC2\xB1this value."); ImGui::SliderFloat("##cpr", &A.cpRange, 0.2f, 4.f, "%.2f"); }
        else                                     { prop("Vorticity max", "Colour scale top, in U / L."); ImGui::SliderFloat("##vmax", &A.vortMax, 0.5f, 30.f, "%.1f"); }
        prop("Opacity");
        ImGui::SliderFloat("##salpha", &A.sliceAlpha, 0.f, 1.f, "%.2f");
        ImGui::EndTable();
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Streamlines");
    ImGui::BeginDisabled(!A.part.on);
    if (beginProps("##stream")) {
        int P = A.part.P, T = A.part.T;
        prop("Particles");
        ImGui::SliderInt("##np", &P, 200, 20000);
        prop("Trail length");
        ImGui::SliderInt("##tl", &T, 2, 40);
        if (P != A.part.P || T != A.part.T) { A.part.P = P; A.part.T = T; A.part.init(s); }
        prop("Speed", "Advection speed multiplier (visual only).");
        ImGui::SliderFloat("##pspeed", &A.part.speed, 0.1f, 3.f, "%.2f");
        prop("Seed spread Y", "Height of the seeding region as a fraction of the domain.");
        ImGui::SliderFloat("##seedy", &A.part.seedY, 0.05f, 0.95f, "%.2f");
        prop("Seed spread Z", "Width of the seeding region as a fraction of the domain.");
        ImGui::SliderFloat("##seedz", &A.part.seedZ, 0.05f, 0.95f, "%.2f");
        prop("Opacity");
        ImGui::SliderFloat("##palpha", &A.part.alpha, 0.05f, 1.f, "%.2f");
        ImGui::EndTable();
    }
    ImGui::EndDisabled();

    if (A.colorMode == Surface_Pressure && !A.showSlice) {   // Cp range is otherwise only reachable via the slice
        ImGui::SeparatorText("Surface");
        if (beginProps("##surface")) {
            prop("Cp range");
            ImGui::SliderFloat("##cpr2", &A.cpRange, 0.2f, 4.f, "%.2f");
            ImGui::EndTable();
        }
    }
}

static void drawControls() {
    ImGui::Begin("Controls");
    bool dirty = false;
    if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) drawSimulationSection();
    if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen))      drawModelSection(dirty);
    if (ImGui::CollapsingHeader("Flow", ImGuiTreeNodeFlags_DefaultOpen))       drawFlowSection();
    if (ImGui::CollapsingHeader("Domain"))                                     drawDomainSection();
    if (ImGui::CollapsingHeader("Visualisation", ImGuiTreeNodeFlags_DefaultOpen)) drawVisualisationSection();
    if (dirty) applyModel();
    ImGui::End();
}

// ============================================================================
//  Results panel
// ============================================================================
static void coefficientCard(const char* name, double value) {
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", name);
    ImGui::PushFont(g_monoFont, BASE_FONT_SIZE * 1.6f);
    ImGui::Text("%+.4f", value);
    ImGui::PopFont();
}

static void historyPlot(const char* id, const char* name, const std::vector<float>& h) {
    char overlay[64];
    std::snprintf(overlay, sizeof(overlay), "%s  %+.4f", name, h.empty() ? 0.f : h.back());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PlotLines(id, h.data(), (int)h.size(), 0, overlay, FLT_MAX, FLT_MAX, ImVec2(0, ImGui::GetFontSize() * 4.f));
}

static void drawResults() {
    const Sim& s = A.sim;
    ImGui::Begin("Results");

    if (s.diverged) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.45f, 0.10f, 0.10f, 0.45f));
        ImGui::BeginChild("##diverged", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextColored(COL_ERROR, "Simulation diverged");
        ImGui::TextWrapped("Lower the lattice velocity, raise Smagorinsky C or reduce the wind speed, then reset the flow.");
        if (ImGui::Button("Reset flow")) resetFlow();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    const Forces f = forces();
    ImGui::SeparatorText("Coefficients");
    if (ImGui::BeginTable("##coef", 3, ImGuiTableFlags_SizingStretchSame)) {
        coefficientCard("Cd  drag", f.Cd);
        coefficientCard("Cl  lift", f.Cl);
        coefficientCard("Cs  side", f.Cs);
        ImGui::EndTable();
    }
    if (beginProps("##ref")) {
        prop("Ref. area", "Area the coefficients are normalised by:\nfrontal (projected on YZ) or planform (projected on XZ).");
        if (ImGui::Combo("##refmode", &A.refMode, "Frontal (YZ)\0Planform (XZ)\0")) { A.hCd.clear(); A.hCl.clear(); }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Forces");
    if (ImGui::BeginTable("##forces", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg)) {
        valueRow("Drag", "%.4g N", f.drag);
        valueRow("Lift", "%.4g N", f.lift);
        valueRow("Side", "%.4g N", f.side);
        valueRow("Reference area", "%.4g m\xC2\xB2", f.Aref);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("History");
    if (A.hCd.size() > 1) {
        historyPlot("##cd", "Cd", A.hCd);
        historyPlot("##cl", "Cl", A.hCl);
    } else {
        ImGui::TextDisabled("Run the simulation to plot Cd and Cl.");
    }

    ImGui::SeparatorText("Convergence");
    const double ft = s.flowThroughs();
    char ftLabel[48]; std::snprintf(ftLabel, sizeof(ftLabel), "%.2f flow-throughs", ft);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ft >= 3.0 ? COL_GOOD : COL_ACCENT);
    ImGui::ProgressBar((float)std::min(ft / 3.0, 1.0), ImVec2(-FLT_MIN, 0), ftLabel);
    ImGui::PopStyleColor();
    ImGui::TextDisabled(ft >= 3.0 ? "Flow has had time to develop." : "Wait for ~3 flow-throughs before trusting Cd / Cl.");
    if (ImGui::BeginTable("##perf", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg)) {
        valueRow("Steps", "%lld", s.steps);
        valueRow("Throughput", "%.1f MLUPS", A.running ? A.mlups : 0.0);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Flow");
    const FlowScaling fs = flowScaling();
    const double blockage = (double)A.vox.areaFront / ((double)s.ny * s.nz);
    if (ImGui::BeginTable("##flowinfo", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_RowBg)) {
        valueRow("Re (physical)", "%.3g", fs.Re);
        valueRow("Re (simulated)", "%.3g%s", fs.ReSim, fs.ReSim < fs.Re * 0.99f ? " (LES)" : "");
        valueRow("Mach (lattice)", "%.3f", fs.mach);
        valueRow("Relaxation \xCF\x84" "0", "%.4f", fs.tau0);
        valueRow("Solid cells", "%lld", A.vox.solidCells);
        valueRow("Blockage", "%.1f %%", blockage * 100.0);
        ImGui::EndTable();
    }
    // Validation against a sphere at Re 100: ~6% extra Cd at 5.5% blockage vs 1.4%; worse beyond.
    if (blockage > 0.05) {
        ImGui::PushStyleColor(ImGuiCol_Text, blockage > 0.10 ? COL_ERROR : COL_WARN);
        ImGui::TextWrapped("The model fills %.0f%% of the tunnel cross-section, which inflates Cd%s. "
                           "Aim for under 5%%: shrink Length (cells) or enlarge the grid.", blockage * 100.0, blockage > 0.10 ? " strongly" : "");
        ImGui::PopStyleColor();
    }
    if (fs.ReSim < fs.Re * 0.99f)
        ImGui::TextWrapped("The grid cannot resolve the physical Re; the solver runs at a lower Re and relies on the LES model.");
    ImGui::End();
}

// ============================================================================
//  Viewport
// ============================================================================
static void drawToolbar() {
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(st.ItemSpacing.x, st.FramePadding.y + 1));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, st.Colors[ImGuiCol_MenuBarBg]);
    const float h = ImGui::GetFrameHeight() + 2 * (st.FramePadding.y + 1);
    ImGui::BeginChild("##toolbar", ImVec2(0, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    const float runW = ImGui::CalcTextSize("Pause").x + st.FramePadding.x * 4;
    ImGui::BeginDisabled(A.sim.diverged);
    if (A.running ? coloredButton("Pause", COL_WARN, ImVec2(runW, 0)) : coloredButton("Run", COL_GOOD, ImVec2(runW, 0))) toggleRunning();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Run / pause the solver (Space)");
    ImGui::SameLine();
    if (ImGui::Button("Reset flow")) resetFlow();
    ImGui::SetItemTooltip("Restart the flow from a uniform stream (R)");
    ImGui::SameLine();
    if (ImGui::Button("Reset camera")) A.cam.frame(A.sim);
    ImGui::SetItemTooltip("Frame the whole domain (F)");

    ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
    toggleButton("Box", &A.showBox); ImGui::SameLine();
    toggleButton("Slice", &A.showSlice); ImGui::SameLine();
    toggleButton("Streamlines", &A.part.on); ImGui::SameLine();
    bool cp = A.colorMode == Surface_Pressure;
    if (toggleButton("Surface Cp", &cp)) A.colorMode = cp ? Surface_Pressure : Surface_Solid;
    if (A.showSlice) {
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.f);
        ImGui::Combo("##tbfield", &A.sliceField, "Speed |u|/U\0Pressure Cp\0Vorticity\0");
    }
    ImGui::EndChild();
}

// Colour bar for the active slice field, anchored at the bottom-left of the viewport.
static void drawLegend(ImDrawList* dl, ImVec2 p0, ImVec2 p1) {
    const float s = uiScale();
    const float barW = 180 * s, barH = 10 * s, pad = 12 * s;
    const char* title; float lo, hi; bool diverging = false;
    if (A.sliceField == Field_Speed)         { title = "Speed |u| / U";  lo = 0; hi = A.speedMax; }
    else if (A.sliceField == Field_Pressure) { title = "Pressure Cp";    lo = -A.cpRange; hi = A.cpRange; diverging = true; }
    else                                     { title = "Vorticity |\xCF\x89| L / U"; lo = 0; hi = A.vortMax; }

    const float lineH = ImGui::GetTextLineHeight();
    ImVec2 o(p0.x + pad, p1.y - pad - barH - 2 * lineH - 8 * s);
    dl->AddRectFilled(ImVec2(o.x - 8 * s, o.y - 6 * s), ImVec2(o.x + barW + 8 * s, p1.y - pad + 4 * s), IM_COL32(15, 17, 21, 190), 4 * s);
    dl->AddText(o, IM_COL32(220, 224, 230, 255), title);
    const float by = o.y + lineH + 4 * s;
    const int n = 32;
    for (int i = 0; i < n; ++i) {
        float t0 = i / (float)n, t1 = (i + 1) / (float)n;
        vec3 c0 = diverging ? colormapBwr(t0 * 2 - 1) : colormapJet(t0);
        vec3 c1 = diverging ? colormapBwr(t1 * 2 - 1) : colormapJet(t1);
        ImU32 a = ImGui::ColorConvertFloat4ToU32(ImVec4(c0.x, c0.y, c0.z, 1)), b = ImGui::ColorConvertFloat4ToU32(ImVec4(c1.x, c1.y, c1.z, 1));
        dl->AddRectFilledMultiColor(ImVec2(o.x + barW * t0, by), ImVec2(o.x + barW * t1 + 0.5f, by + barH), a, b, b, a);
    }
    char lbl[32];
    std::snprintf(lbl, sizeof(lbl), "%.2g", lo);
    dl->AddText(ImVec2(o.x, by + barH + 2 * s), IM_COL32(180, 186, 196, 255), lbl);
    std::snprintf(lbl, sizeof(lbl), "%.2g", hi);
    dl->AddText(ImVec2(o.x + barW - ImGui::CalcTextSize(lbl).x, by + barH + 2 * s), IM_COL32(180, 186, 196, 255), lbl);
}

// XYZ orientation gizmo at the bottom-right of the viewport.
static void drawAxisGizmo(ImDrawList* dl, ImVec2 p1) {
    const float s = uiScale(), len = 30 * s;
    const ImVec2 c(p1.x - 50 * s, p1.y - 50 * s);
    const mat3 R(A.cam.view());
    struct Ax { vec3 v; ImU32 col; const char* name; } ax[3] = {
        {R * vec3(1, 0, 0), IM_COL32(235, 90, 90, 255), "X"},
        {R * vec3(0, 1, 0), IM_COL32(110, 210, 110, 255), "Y"},
        {R * vec3(0, 0, 1), IM_COL32(100, 150, 255, 255), "Z"}};
    std::sort(std::begin(ax), std::end(ax), [](const Ax& a, const Ax& b) { return a.v.z < b.v.z; });   // far first
    dl->AddCircleFilled(c, len + 12 * s, IM_COL32(15, 17, 21, 140));
    for (const Ax& a : ax) {
        ImVec2 e(c.x + a.v.x * len, c.y - a.v.y * len);
        dl->AddLine(c, e, a.col, 2.f * s);
        dl->AddCircleFilled(e, 7 * s, a.col);
        ImVec2 ts = ImGui::CalcTextSize(a.name);
        dl->AddText(ImVec2(e.x - ts.x * 0.5f, e.y - ts.y * 0.5f), IM_COL32(20, 20, 24, 255), a.name);
    }
}

static void drawViewport() {
    ImGuiWindowClass wc;
    wc.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_AutoHideTabBar;
    ImGui::SetNextWindowClass(&wc);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    drawToolbar();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - ImGui::GetStyle().ItemSpacing.y);   // image flush under the toolbar
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int w = (int)avail.x, h = (int)avail.y;
    if (w > 16 && h > 16) {
        renderViewport(w, h);
        const ImVec2 p0 = ImGui::GetCursorScreenPos(), p1(p0.x + w, p0.y + h);
        ImGui::InvisibleButton("##vp", ImVec2((float)w, (float)h),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddImage((ImTextureID)(intptr_t)A.view.tex, p0, p1, ImVec2(0, 1), ImVec2(1, 0));

        // camera: left-drag orbit, right/middle-drag pan, wheel zoom, double-click reframe
        ImGuiIO& io = ImGui::GetIO();
        OrbitCamera& cam = A.cam;
        if (ImGui::IsItemActive()) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                cam.yaw += io.MouseDelta.x * 0.005f;
                cam.pitch = std::clamp(cam.pitch + io.MouseDelta.y * 0.005f, -1.5f, 1.5f);
            } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                const vec3 eye = cam.dir();
                const vec3 right = glm::normalize(glm::cross(vec3(0, 1, 0), eye)), up = glm::cross(eye, right);
                cam.target += (-right * io.MouseDelta.x + up * io.MouseDelta.y) * (cam.dist * 0.0015f);
            }
        }
        if (ImGui::IsItemHovered()) {
            if (io.MouseWheel != 0.f) cam.dist = std::clamp(cam.dist * (1.f - 0.1f * io.MouseWheel), 5.f, 3000.f);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) cam.frame(A.sim);
        }

        // overlays
        const float s = uiScale(), pad = 12 * s;
        char info[256];
        std::snprintf(info, sizeof(info), "%s\nFlow \xE2\x86\x92 +X    Lift \xE2\x86\x91 +Y", A.haveModel ? A.model.name.c_str() : "No model");
        dl->AddText(ImVec2(p0.x + pad + 1, p0.y + pad + 1), IM_COL32(0, 0, 0, 160), info);
        dl->AddText(ImVec2(p0.x + pad, p0.y + pad), IM_COL32(215, 220, 228, 255), info);
        if (A.showSlice) drawLegend(dl, p0, p1);
        drawAxisGizmo(dl, p1);
        if (!A.running && A.sim.steps == 0 && !A.sim.diverged) {
            const char* hint = "Press Space or Run to start the simulation";
            ImVec2 ts = ImGui::CalcTextSize(hint);
            ImVec2 hp((p0.x + p1.x - ts.x) * 0.5f, p1.y - pad - ts.y - 8 * s);
            dl->AddRectFilled(ImVec2(hp.x - 10 * s, hp.y - 5 * s), ImVec2(hp.x + ts.x + 10 * s, hp.y + ts.y + 5 * s), IM_COL32(15, 17, 21, 200), 4 * s);
            dl->AddText(hp, IM_COL32(220, 224, 230, 255), hint);
        }
    }
    ImGui::End();
}

// ============================================================================
//  Help / about windows
// ============================================================================
static void drawHelpWindows() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    if (g_showHelp) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Controls##help", &g_showHelp, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::BeginTable("##keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                auto row = [](const char* k, const char* d) {
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(k);
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", d);
                };
                row("Left drag", "Orbit camera");
                row("Right / middle drag", "Pan camera");
                row("Mouse wheel", "Zoom");
                row("Double-click / F", "Reset camera");
                row("Space", "Run / pause");
                row("R", "Reset flow");
                row("Ctrl+O", "Open model");
                row("Drop file", "Load .fbx / .obj / .stl");
                row("Ctrl+click slider", "Type an exact value");
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
    if (g_showAbout) {
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("About AeroLBM", &g_showAbout, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextUnformatted("AeroLBM - virtual wind tunnel");
            ImGui::TextDisabled("D3Q19 lattice-Boltzmann, BGK + Smagorinsky LES");
            ImGui::Separator();
            ImGui::TextUnformatted("Results are numerical estimates. They depend on grid\nresolution, voxelisation, flow settings and run time.");
            ImGui::TextDisabled("Dear ImGui %s", ImGui::GetVersion());
        }
        ImGui::End();
    }
}

// ============================================================================
void drawUI() {
    handleShortcuts();
    drawMenuBar();
    drawStatusBar();
    drawDockspace();
    drawControls();
    drawResults();
    drawViewport();
    drawHelpWindows();
}
