#include "gl_loader.h"

#include <windows.h>
#include <cstdio>

#define AERO_GL_DEFINE(type, name) type name = nullptr;
AERO_GL_FUNCS(AERO_GL_DEFINE)
#undef AERO_GL_DEFINE

// wglGetProcAddress only returns GL 1.2+ entry points; 1.0/1.1 ones are plain exports of opengl32.dll.
static void* getProc(HMODULE gl, const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (p == nullptr || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1)
        p = (void*)GetProcAddress(gl, name);
    return p;
}

bool glLoadFunctions() {
    HMODULE gl = GetModuleHandleW(L"opengl32.dll");
    if (!gl) return false;
    bool ok = true;
#define AERO_GL_LOAD(type, name) \
    name = (type)getProc(gl, #name); \
    if (!name) { std::fprintf(stderr, "Missing GL function: %s\n", #name); ok = false; }
    AERO_GL_FUNCS(AERO_GL_LOAD)
#undef AERO_GL_LOAD
    return ok;
}
