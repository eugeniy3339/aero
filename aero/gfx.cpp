#include "gfx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using glm::vec3; using glm::vec4;

static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log); std::fprintf(stderr, "Shader error:\n%s\n", log); }
    return s;
}

GLuint makeProgram(const char* vs, const char* fs) {
    GLuint p = glCreateProgram(), a = compile(GL_VERTEX_SHADER, vs), b = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, a); glAttachShader(p, b); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log); std::fprintf(stderr, "Link error:\n%s\n", log); }
    glDeleteShader(a); glDeleteShader(b);
    return p;
}

// ---- shaders ----
// Mesh: two-sided headlight-ish diffuse with a soft rim so silhouettes read against the dark background.
static const char* VS_MESH = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec3 aCol;
uniform mat4 uMVP; uniform mat4 uView; out vec3 vN; out vec3 vC;
void main(){ vN = mat3(uView)*aNrm; vC = aCol; gl_Position = uMVP*vec4(aPos,1.0); })";
static const char* FS_MESH = R"(#version 330 core
in vec3 vN; in vec3 vC; uniform vec3 uBase; uniform int uUseVC; out vec4 o;
void main(){ vec3 n = normalize(vN); float d = abs(dot(n, normalize(vec3(0.3,0.5,1.0))));
  float rim = pow(1.0 - abs(n.z), 3.0);
  vec3 base = (uUseVC==1) ? vC : uBase; o = vec4(base*(0.25+0.75*d) + 0.12*rim, 1.0); })";
static const char* VS_LINE = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec4 aCol; uniform mat4 uMVP; out vec4 vC;
void main(){ vC = aCol; gl_Position = uMVP*vec4(aPos,1.0); })";
static const char* FS_LINE = R"(#version 330 core
in vec4 vC; out vec4 o; void main(){ o = vC; })";
static const char* VS_TEX = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec2 aUV; uniform mat4 uMVP; out vec2 vUV;
void main(){ vUV = aUV; gl_Position = uMVP*vec4(aPos,1.0); })";
static const char* FS_TEX = R"(#version 330 core
in vec2 vUV; uniform sampler2D uTex; uniform float uAlpha; out vec4 o;
void main(){ o = vec4(texture(uTex,vUV).rgb, uAlpha); })";

void MeshProgram::init() {
    id = makeProgram(VS_MESH, FS_MESH);
    uMVP = glGetUniformLocation(id, "uMVP"); uView = glGetUniformLocation(id, "uView");
    uBase = glGetUniformLocation(id, "uBase"); uUseVC = glGetUniformLocation(id, "uUseVC");
}
void LineProgram::init() {
    id = makeProgram(VS_LINE, FS_LINE);
    uMVP = glGetUniformLocation(id, "uMVP");
}
void TexProgram::init() {
    id = makeProgram(VS_TEX, FS_TEX);
    uMVP = glGetUniformLocation(id, "uMVP"); uTex = glGetUniformLocation(id, "uTex"); uAlpha = glGetUniformLocation(id, "uAlpha");
}

// ---- buffers ----
void GLMesh::upload(const std::vector<vec3>& p, const std::vector<vec3>& n, const std::vector<vec3>& c, const std::vector<unsigned>& idx) {
    if (!vao) { glGenVertexArrays(1, &vao); glGenBuffers(4, b); }
    glBindVertexArray(vao);
    auto setb = [&](GLuint loc, GLuint buf, const std::vector<vec3>& d, GLenum use) {
        glBindBuffer(GL_ARRAY_BUFFER, buf); glBufferData(GL_ARRAY_BUFFER, d.size() * sizeof(vec3), d.data(), use);
        glEnableVertexAttribArray(loc); glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
    };
    setb(0, b[0], p, GL_STATIC_DRAW); setb(1, b[1], n, GL_STATIC_DRAW); setb(2, b[2], c, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b[3]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
    count = (GLsizei)idx.size();
}

void GLMesh::updateColors(const std::vector<vec3>& c) {
    glBindBuffer(GL_ARRAY_BUFFER, b[2]); glBufferSubData(GL_ARRAY_BUFFER, 0, c.size() * sizeof(vec3), c.data());
}

void DynLines::init() {
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LV), nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LV), (void*)sizeof(vec3));
}

void DynLines::draw(const std::vector<LV>& v, GLenum mode) {
    if (v.empty()) return;
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(LV), v.data(), GL_STREAM_DRAW);
    glDrawArrays(mode, 0, (GLsizei)v.size());
}

void DynQuad::init() {
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
}

void DynQuad::draw(const vec3 q[4]) {
    static const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    static const int tri[6] = {0, 1, 2, 0, 2, 3};
    float vb[6 * 5];
    for (int i = 0; i < 6; ++i) {
        const int k = tri[i];
        vb[i * 5 + 0] = q[k].x; vb[i * 5 + 1] = q[k].y; vb[i * 5 + 2] = q[k].z;
        vb[i * 5 + 3] = uv[k][0]; vb[i * 5 + 4] = uv[k][1];
    }
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vb), vb, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

// ---- render target ----
void RenderTarget::release() {
    if (!fbo) return;
    glDeleteFramebuffers(1, &msFbo); glDeleteRenderbuffers(1, &msColor); glDeleteRenderbuffers(1, &msDepth);
    glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &tex);
    msFbo = msColor = msDepth = fbo = tex = 0;
}

void RenderTarget::ensure(int width, int height) {
    if (fbo && width == w && height == h) return;
    release();
    w = width; h = height;

    glGenRenderbuffers(1, &msColor); glBindRenderbuffer(GL_RENDERBUFFER, msColor);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, w, h);
    glGenRenderbuffers(1, &msDepth); glBindRenderbuffer(GL_RENDERBUFFER, msDepth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &msFbo); glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msColor);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);

    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderTarget::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, msFbo);
    glViewport(0, 0, w, h);
}

void RenderTarget::resolve() const {
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ---- colour maps ----
vec3 colormapJet(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return vec3(std::clamp(1.5f - std::fabs(4 * t - 3), 0.f, 1.f), std::clamp(1.5f - std::fabs(4 * t - 2), 0.f, 1.f),
                std::clamp(1.5f - std::fabs(4 * t - 1), 0.f, 1.f));
}

vec3 colormapBwr(float t) {
    t = std::clamp(t, -1.f, 1.f);
    return t < 0 ? vec3(1 + t, 1 + t, 1.f) : vec3(1.f, 1 - t, 1 - t);
}
