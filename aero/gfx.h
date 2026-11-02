// ============================================================================
//  Small OpenGL 3.3 helpers: shader programs, mesh / line buffers, an MSAA
//  render target for the 3D viewport, and colour maps.
// ============================================================================
#pragma once
#include "gl_loader.h"

#include <glm/glm.hpp>

#include <vector>

GLuint makeProgram(const char* vs, const char* fs);

// The three shader programs, with their uniform locations looked up once.
struct MeshProgram { GLuint id = 0; GLint uMVP = -1, uView = -1, uBase = -1, uUseVC = -1; void init(); };
struct LineProgram { GLuint id = 0; GLint uMVP = -1; void init(); };
struct TexProgram  { GLuint id = 0; GLint uMVP = -1, uTex = -1, uAlpha = -1; void init(); };

struct GLMesh {
    GLuint vao = 0, b[4] = {0, 0, 0, 0}; GLsizei count = 0;
    void upload(const std::vector<glm::vec3>& p, const std::vector<glm::vec3>& n, const std::vector<glm::vec3>& c,
                const std::vector<unsigned>& idx);
    void updateColors(const std::vector<glm::vec3>& c);
};

struct LV { glm::vec3 p; glm::vec4 c; };
struct DynLines {
    GLuint vao = 0, vbo = 0;
    void init();
    void draw(const std::vector<LV>& v, GLenum mode);
};

// Textured quad with position (xyz) + uv, re-uploaded on every draw.
struct DynQuad {
    GLuint vao = 0, vbo = 0;
    void init();
    void draw(const glm::vec3 corners[4]);
};

// Renders into a multisampled FBO, then resolves into `tex` for display in ImGui.
struct RenderTarget {
    int w = 0, h = 0, samples = 4;
    GLuint msFbo = 0, msColor = 0, msDepth = 0;   // multisampled
    GLuint fbo = 0, tex = 0;                      // resolved
    void ensure(int width, int height);
    void bind() const;
    void resolve() const;
private:
    void release();
};

glm::vec3 colormapJet(float t);   // 0..1 blue -> red
glm::vec3 colormapBwr(float t);   // -1 blue .. 0 white .. +1 red
