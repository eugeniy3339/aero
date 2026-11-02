// ============================================================================
//  Minimal OpenGL 3.3 core function loader for Windows (WGL).
//  Only the entry points main.cpp uses; add new ones to AERO_GL_FUNCS.
// ============================================================================
#pragma once
#include <GL/glcorearb.h>

#define AERO_GL_FUNCS(X) \
    X(PFNGLACTIVETEXTUREPROC,           glActiveTexture) \
    X(PFNGLATTACHSHADERPROC,            glAttachShader) \
    X(PFNGLBINDBUFFERPROC,              glBindBuffer) \
    X(PFNGLBINDFRAMEBUFFERPROC,         glBindFramebuffer) \
    X(PFNGLBINDRENDERBUFFERPROC,        glBindRenderbuffer) \
    X(PFNGLBINDTEXTUREPROC,             glBindTexture) \
    X(PFNGLBINDVERTEXARRAYPROC,         glBindVertexArray) \
    X(PFNGLBLENDFUNCPROC,               glBlendFunc) \
    X(PFNGLBUFFERDATAPROC,              glBufferData) \
    X(PFNGLBUFFERSUBDATAPROC,           glBufferSubData) \
    X(PFNGLCLEARPROC,                   glClear) \
    X(PFNGLCLEARCOLORPROC,              glClearColor) \
    X(PFNGLCOMPILESHADERPROC,           glCompileShader) \
    X(PFNGLCREATEPROGRAMPROC,           glCreateProgram) \
    X(PFNGLCREATESHADERPROC,            glCreateShader) \
    X(PFNGLDELETEFRAMEBUFFERSPROC,      glDeleteFramebuffers) \
    X(PFNGLDELETERENDERBUFFERSPROC,     glDeleteRenderbuffers) \
    X(PFNGLDELETESHADERPROC,            glDeleteShader) \
    X(PFNGLDELETETEXTURESPROC,          glDeleteTextures) \
    X(PFNGLDEPTHMASKPROC,               glDepthMask) \
    X(PFNGLDISABLEPROC,                 glDisable) \
    X(PFNGLDRAWARRAYSPROC,              glDrawArrays) \
    X(PFNGLDRAWELEMENTSPROC,            glDrawElements) \
    X(PFNGLENABLEPROC,                  glEnable) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer) \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC,    glFramebufferTexture2D) \
    X(PFNGLGENBUFFERSPROC,              glGenBuffers) \
    X(PFNGLGENFRAMEBUFFERSPROC,         glGenFramebuffers) \
    X(PFNGLGENRENDERBUFFERSPROC,        glGenRenderbuffers) \
    X(PFNGLGENTEXTURESPROC,             glGenTextures) \
    X(PFNGLGENVERTEXARRAYSPROC,         glGenVertexArrays) \
    X(PFNGLGETERRORPROC,                glGetError) \
    X(PFNGLGETPROGRAMINFOLOGPROC,       glGetProgramInfoLog) \
    X(PFNGLGETPROGRAMIVPROC,            glGetProgramiv) \
    X(PFNGLGETSHADERINFOLOGPROC,        glGetShaderInfoLog) \
    X(PFNGLGETSHADERIVPROC,             glGetShaderiv) \
    X(PFNGLGETUNIFORMLOCATIONPROC,      glGetUniformLocation) \
    X(PFNGLLINKPROGRAMPROC,             glLinkProgram) \
    X(PFNGLRENDERBUFFERSTORAGEPROC,     glRenderbufferStorage) \
    X(PFNGLSHADERSOURCEPROC,            glShaderSource) \
    X(PFNGLTEXIMAGE2DPROC,              glTexImage2D) \
    X(PFNGLTEXPARAMETERIPROC,           glTexParameteri) \
    X(PFNGLUNIFORM1FPROC,               glUniform1f) \
    X(PFNGLUNIFORM1IPROC,               glUniform1i) \
    X(PFNGLUNIFORM3FPROC,               glUniform3f) \
    X(PFNGLUNIFORMMATRIX4FVPROC,        glUniformMatrix4fv) \
    X(PFNGLUSEPROGRAMPROC,              glUseProgram) \
    X(PFNGLVERTEXATTRIBPOINTERPROC,     glVertexAttribPointer) \
    X(PFNGLVIEWPORTPROC,                glViewport)

#define AERO_GL_DECLARE(type, name) extern type name;
AERO_GL_FUNCS(AERO_GL_DECLARE)
#undef AERO_GL_DECLARE

// Call with a current GL context. Returns false (and prints the name) if any entry point is missing.
bool glLoadFunctions();
