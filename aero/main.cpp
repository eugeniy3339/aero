// ============================================================================
//  AeroLBM - virtual wind tunnel
//  - Import .fbx / .obj (ufbx) and .stl (built-in reader)            model.cpp
//  - Voxelises the mesh into a 3D lattice-Boltzmann domain             scene.cpp
//  - D3Q19, BGK + Smagorinsky LES, momentum-exchange forces            lbm.cpp
//  - Velocity/pressure/vorticity slice, streamline trails, surface Cp  scene.cpp
//  - UI: Dear ImGui (docking branch), 3D view in a dockable viewport   ui.cpp
//  - Windows only: Win32 window + WGL OpenGL 3.3 core context          main.cpp
//
//  Flow direction is +X, "up" (lift) is +Y.
// ============================================================================
#include "app.h"
#include <shellapi.h>         // drag & drop

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static std::string g_drop;          // file dropped onto the window, loaded at the start of the next frame
static float g_pendingDpi = 0.f;    // new DPI scale after WM_DPICHANGED, applied before the next frame

static LRESULT WINAPI wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_DROPFILES: {
        HDROP drop = (HDROP)wParam;
        UINT len = DragQueryFileW(drop, 0, nullptr, 0);
        std::wstring buf(len, L'\0');
        if (len && DragQueryFileW(drop, 0, buf.data(), len + 1)) g_drop = narrow(buf);
        DragFinish(drop);
        return 0;
    }
    case WM_DPICHANGED: {
        const RECT* r = (const RECT*)lParam;
        SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        g_pendingDpi = HIWORD(wParam) / 96.f;
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize = POINT{800, 500};
        return 0;
    }
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;   // disable ALT application menu
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Core 3.3 context: a legacy context is needed first to fetch wglCreateContextAttribsARB.
static bool createGLContext() {
    constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091, WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
    constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126, WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x1;
    using CreateCtxFn = HGLRC(WINAPI*)(HDC, HGLRC, const int*);
    using SwapIntervalFn = BOOL(WINAPI*)(int);

    A.hdc = GetDC(A.hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.cDepthBits = 24; pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(A.hdc, &pfd);
    if (!pf || !SetPixelFormat(A.hdc, pf, &pfd)) return false;

    HGLRC legacy = wglCreateContext(A.hdc);
    if (!legacy || !wglMakeCurrent(A.hdc, legacy)) return false;
    auto createCtx = (CreateCtxFn)wglGetProcAddress("wglCreateContextAttribsARB");
    const int attribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
    A.glrc = createCtx ? createCtx(A.hdc, nullptr, attribs) : nullptr;
    wglMakeCurrent(nullptr, nullptr); wglDeleteContext(legacy);
    if (!A.glrc || !wglMakeCurrent(A.hdc, A.glrc)) return false;

    if (auto swapInterval = (SwapIntervalFn)wglGetProcAddress("wglSwapIntervalEXT")) swapInterval(1);
    return true;
}

static void fatal(const wchar_t* msg) {
    MessageBoxW(A.hwnd, msg, L"AeroLBM", MB_ICONERROR | MB_OK);
}

// Saved panel layout lives next to the executable, not in whatever the working directory happens to be.
static std::string iniPath() {
    wchar_t exe[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe, n);
    dir = dir.substr(0, dir.find_last_of(L"\\/") + 1);
    return narrow(dir + L"aero_layout.ini");
}

int main() {
    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW wc = { sizeof(wc), CS_OWNDC, wndProc, 0, 0, GetModuleHandleW(nullptr), LoadIconW(nullptr, IDI_APPLICATION),
                       LoadCursorW(nullptr, IDC_ARROW), CreateSolidBrush(RGB(27, 30, 36)), nullptr, L"AeroLBM", nullptr };
    RegisterClassExW(&wc);
    const float primaryDpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    A.hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, L"AeroLBM - virtual wind tunnel", WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, (int)(1600 * primaryDpi), (int)(900 * primaryDpi),
                             nullptr, nullptr, wc.hInstance, nullptr);
    if (!A.hwnd) return 1;
    if (!createGLContext()) { fatal(L"Could not create an OpenGL 3.3 core context."); return 1; }
    if (!glLoadFunctions()) { fatal(L"Required OpenGL functions are missing. Update your graphics driver."); return 1; }
    ShowWindow(A.hwnd, SW_SHOWMAXIMIZED); UpdateWindow(A.hwnd);

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    static const std::string ini = iniPath();
    io.IniFilename = ini.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    setupStyle(ImGui_ImplWin32_GetDpiScaleForHwnd(A.hwnd));
    ImGui_ImplWin32_InitForOpenGL(A.hwnd);
    ImGui_ImplOpenGL3_Init("#version 330");

    initScene();

    for (bool quit = false; !quit;) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg); DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) quit = true;
        }
        if (quit) break;
        if (!g_drop.empty()) { std::snprintf(A.path, sizeof(A.path), "%s", g_drop.c_str()); loadModelPath(g_drop); g_drop.clear(); }
        if (g_pendingDpi > 0.f) { setupStyle(g_pendingDpi); g_pendingDpi = 0.f; }

        advanceSimulation();

        // Minimised: keep the solver going, skip drawing (no vsync to throttle us here).
        if (IsIconic(A.hwnd)) { if (!A.running) Sleep(10); continue; }

        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        drawUI();
        ImGui::Render();

        RECT rc; GetClientRect(A.hwnd, &rc);
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, rc.right - rc.left, rc.bottom - rc.top);
        glClearColor(0.075f, 0.082f, 0.098f, 1.f); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(A.hdc);
    }

    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    wglMakeCurrent(nullptr, nullptr); wglDeleteContext(A.glrc);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);   // window is already gone (WM_DESTROY ended the loop)
    return 0;
}
