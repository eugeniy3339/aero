// ============================================================================
//  AeroLBM - virtual wind tunnel
//  - Import .fbx (and obj/glb/gltf/dae/3ds/stl...) through Assimp
//  - Voxelises the mesh into a 3D lattice-Boltzmann domain (D3Q19, BGK + Smagorinsky LES)
//  - Drag / lift / side force via momentum-exchange method
//  - Visualisation: velocity/pressure/vorticity slice, streamline trails, surface Cp
//  - UI: Dear ImGui (docking branch), 3D view rendered into a dockable viewport
//
//  Flow direction is +X, "up" (lift) is +Y.
// ============================================================================
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_internal.h"   // DockBuilder
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using glm::vec2; using glm::vec3; using glm::vec4; using glm::mat3; using glm::mat4;

// ============================================================================
//  D3Q19 lattice
// ============================================================================
static int   C[19][3];
static float W[19];
static int   OPP[19];

static void initLattice() {
    int n = 0;
    for (int z = -1; z <= 1; ++z) for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        int nzr = (x != 0) + (y != 0) + (z != 0);
        if (nzr > 2) continue;
        C[n][0] = x; C[n][1] = y; C[n][2] = z;
        W[n] = nzr == 0 ? 1.f / 3.f : (nzr == 1 ? 1.f / 18.f : 1.f / 36.f);
        ++n;
    }
    for (int i = 0; i < 19; ++i)
        for (int j = 0; j < 19; ++j)
            if (C[j][0] == -C[i][0] && C[j][1] == -C[i][1] && C[j][2] == -C[i][2]) OPP[i] = j;
}

static inline float feqf(int q, float r, float ux, float uy, float uz) {
    float cu  = 3.f * (C[q][0] * ux + C[q][1] * uy + C[q][2] * uz);
    float usq = 1.5f * (ux * ux + uy * uy + uz * uz);
    return W[q] * r * (1.f + cu + 0.5f * cu * cu - usq);
}

// ============================================================================
//  Lattice-Boltzmann solver
// ============================================================================
struct Smp { float rho = 1.f; vec3 u{0.f}; bool ok = false; };

struct Sim {
    int nx = 0, ny = 0, nz = 0;
    size_t N = 0;
    std::vector<float> f, ft, rho, ux, uy, uz;
    std::vector<uint8_t> solid;
    float uLB = 0.08f, tau0 = 0.55f, smagC = 0.12f;
    double Fs[3] = {0, 0, 0};          // smoothed force (lattice units)
    long long steps = 0;
    bool diverged = false;

    size_t id(int x, int y, int z) const { return (size_t)x + (size_t)nx * ((size_t)y + (size_t)ny * (size_t)z); }

    void alloc(int X, int Y, int Z) {
        nx = X; ny = Y; nz = Z; N = (size_t)nx * ny * nz;
        f.assign(19 * N, 0.f); ft.assign(19 * N, 0.f);
        rho.assign(N, 1.f); ux.assign(N, 0.f); uy.assign(N, 0.f); uz.assign(N, 0.f);
        solid.assign(N, 0);
        steps = 0; diverged = false; Fs[0] = Fs[1] = Fs[2] = 0;
    }

    void resetFlow(float u) {
        uLB = u; steps = 0; diverged = false; Fs[0] = Fs[1] = Fs[2] = 0;
        float fe[19];
        for (int q = 0; q < 19; ++q) fe[q] = feqf(q, 1.f, uLB, 0, 0);
        #pragma omp parallel for
        for (long long i = 0; i < (long long)N; ++i) {
            for (int q = 0; q < 19; ++q) { f[(size_t)q * N + i] = fe[q]; ft[(size_t)q * N + i] = fe[q]; }
            rho[i] = 1.f; ux[i] = solid[i] ? 0.f : uLB; uy[i] = 0.f; uz[i] = 0.f;
        }
    }

    bool isSolid(const vec3& p) const {
        int x = (int)std::floor(p.x), y = (int)std::floor(p.y), z = (int)std::floor(p.z);
        if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) return false;
        return solid[id(x, y, z)] != 0;
    }

    // Trilinear sample at cell-centre convention (cell i centre = i + 0.5).
    // mask=true : solid cells are excluded and weights renormalised (for pressure)
    // mask=false: solid cells count as zero velocity (for particle advection)
    Smp sample(const vec3& p, bool mask) const {
        Smp r;
        float fx = p.x - 0.5f, fy = p.y - 0.5f, fz = p.z - 0.5f;
        int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy), z0 = (int)std::floor(fz);
        float tx = fx - x0, ty = fy - y0, tz = fz - z0;
        float wsum = 0.f, rr = 0.f; vec3 uu(0.f);
        for (int dz = 0; dz < 2; ++dz) for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
            int x = std::clamp(x0 + dx, 0, nx - 1), y = std::clamp(y0 + dy, 0, ny - 1), z = std::clamp(z0 + dz, 0, nz - 1);
            float w = (dx ? tx : 1 - tx) * (dy ? ty : 1 - ty) * (dz ? tz : 1 - tz);
            size_t i = id(x, y, z);
            if (solid[i]) { if (!mask) wsum += w; continue; }
            wsum += w; rr += w * rho[i]; uu += w * vec3(ux[i], uy[i], uz[i]);
        }
        if (mask) {
            if (wsum < 1e-3f) return r;
            r.rho = rr / wsum; r.u = uu / wsum; r.ok = true;
        } else { r.u = uu; r.rho = 1.f; r.ok = true; }
        return r;
    }

    // One fused pull-stream + collide step. Interior only; domain faces are boundary cells.
    void step() {
        float feIn[19];
        for (int q = 0; q < 19; ++q) feIn[q] = feqf(q, 1.f, uLB, 0, 0);
        const float cs2 = smagC * smagC;
        const float t0 = tau0;
        double fx = 0, fy = 0, fz = 0;

        #pragma omp parallel for collapse(2) reduction(+:fx,fy,fz) schedule(static)
        for (int z = 1; z < nz - 1; ++z)
        for (int y = 1; y < ny - 1; ++y) {
            for (int x = 1; x < nx - 1; ++x) {
                const size_t i = id(x, y, z);
                if (solid[i]) continue;
                float fi[19];
                for (int q = 0; q < 19; ++q) {
                    size_t s = id(x - C[q][0], y - C[q][1], z - C[q][2]);
                    if (solid[s]) {            // half-way bounce-back + momentum exchange
                        float fo = f[(size_t)OPP[q] * N + i];
                        fi[q] = fo;
                        fx += -2.0 * C[q][0] * fo;
                        fy += -2.0 * C[q][1] * fo;
                        fz += -2.0 * C[q][2] * fo;
                    } else fi[q] = f[(size_t)q * N + s];
                }
                float r = 0, mx = 0, my = 0, mz = 0;
                for (int q = 0; q < 19; ++q) { r += fi[q]; mx += C[q][0] * fi[q]; my += C[q][1] * fi[q]; mz += C[q][2] * fi[q]; }
                const float inv = 1.f / r, vx = mx * inv, vy = my * inv, vz = mz * inv;
                const float usq = 1.5f * (vx * vx + vy * vy + vz * vz);
                float fe[19];
                float pxx = 0, pyy = 0, pzz = 0, pxy = 0, pxz = 0, pyz = 0;
                for (int q = 0; q < 19; ++q) {
                    float cu = 3.f * (C[q][0] * vx + C[q][1] * vy + C[q][2] * vz);
                    fe[q] = W[q] * r * (1.f + cu + 0.5f * cu * cu - usq);
                    float ne = fi[q] - fe[q];
                    pxx += C[q][0] * C[q][0] * ne; pyy += C[q][1] * C[q][1] * ne; pzz += C[q][2] * C[q][2] * ne;
                    pxy += C[q][0] * C[q][1] * ne; pxz += C[q][0] * C[q][2] * ne; pyz += C[q][1] * C[q][2] * ne;
                }
                // Smagorinsky sub-grid viscosity (Hou et al.)
                float Q   = std::sqrt(pxx * pxx + pyy * pyy + pzz * pzz + 2.f * (pxy * pxy + pxz * pxz + pyz * pyz));
                float tau = 0.5f * (t0 + std::sqrt(t0 * t0 + 18.f * 1.41421356f * cs2 * Q / r));
                float om  = 1.f / tau;
                for (int q = 0; q < 19; ++q) ft[(size_t)q * N + i] = fi[q] - om * (fi[q] - fe[q]);
                rho[i] = r; ux[i] = vx; uy[i] = vy; uz[i] = vz;
            }
        }

        // ---- boundaries: inlet + far-field faces = equilibrium, outlet = zero gradient ----
        auto setEq = [&](size_t i) {
            for (int q = 0; q < 19; ++q) ft[(size_t)q * N + i] = feIn[q];
            rho[i] = 1.f; ux[i] = uLB; uy[i] = 0.f; uz[i] = 0.f;
        };
        #pragma omp parallel for collapse(2)
        for (int z = 0; z < nz; ++z)
        for (int y = 0; y < ny; ++y) {
            setEq(id(0, y, z));
            size_t o = id(nx - 1, y, z);
            if (y > 0 && y < ny - 1 && z > 0 && z < nz - 1) {
                size_t s = id(nx - 2, y, z);
                for (int q = 0; q < 19; ++q) ft[(size_t)q * N + o] = ft[(size_t)q * N + s];
                rho[o] = rho[s]; ux[o] = ux[s]; uy[o] = uy[s]; uz[o] = uz[s];
            } else setEq(o);
        }
        #pragma omp parallel for collapse(2)
        for (int z = 0; z < nz; ++z)
        for (int x = 0; x < nx; ++x) { setEq(id(x, 0, z)); setEq(id(x, ny - 1, z)); }
        #pragma omp parallel for collapse(2)
        for (int y = 0; y < ny; ++y)
        for (int x = 0; x < nx; ++x) { setEq(id(x, y, 0)); setEq(id(x, y, nz - 1)); }

        f.swap(ft);
        ++steps;
        const double a = 0.01;
        Fs[0] += a * (fx - Fs[0]); Fs[1] += a * (fy - Fs[1]); Fs[2] += a * (fz - Fs[2]);

        if (steps % 200 == 0) {
            for (size_t i = 0; i < N; ++i)
                if (!std::isfinite(rho[i]) || rho[i] < 0.2f || rho[i] > 3.f) { diverged = true; break; }
        }
    }
};

// ============================================================================
//  Model (CPU side)
// ============================================================================
struct Model {
    std::vector<vec3> p0, n0;      // normalised: centred, longest bbox side = 1
    std::vector<unsigned> idx;
    std::string name;
};

static void finalizeModel(Model& m) {
    vec3 mn(FLT_MAX), mx(-FLT_MAX);
    for (auto& p : m.p0) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
    vec3 c = 0.5f * (mn + mx);
    vec3 e = mx - mn;
    float s = std::max({e.x, e.y, e.z, 1e-9f});
    for (auto& p : m.p0) p = (p - c) / s;
}

static void computeNormals(Model& m) {
    m.n0.assign(m.p0.size(), vec3(0));
    for (size_t t = 0; t + 2 < m.idx.size(); t += 3) {
        unsigned a = m.idx[t], b = m.idx[t + 1], c = m.idx[t + 2];
        vec3 fn = glm::cross(m.p0[b] - m.p0[a], m.p0[c] - m.p0[a]);
        m.n0[a] += fn; m.n0[b] += fn; m.n0[c] += fn;
    }
    for (auto& n : m.n0) { float l = glm::length(n); n = l > 1e-12f ? n / l : vec3(0, 1, 0); }
}

static bool loadFile(const std::string& path, Model& out, std::string& err) {
    Assimp::Importer imp;
    imp.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
    const aiScene* sc = imp.ReadFile(path,
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenSmoothNormals |
        aiProcess_PreTransformVertices | aiProcess_FindDegenerates);
    if (!sc || !sc->mNumMeshes) { err = imp.GetErrorString(); if (err.empty()) err = "file contains no meshes"; return false; }
    Model m; m.name = path;
    for (unsigned mi = 0; mi < sc->mNumMeshes; ++mi) {
        const aiMesh* am = sc->mMeshes[mi];
        unsigned base = (unsigned)m.p0.size();
        for (unsigned v = 0; v < am->mNumVertices; ++v) {
            m.p0.emplace_back(am->mVertices[v].x, am->mVertices[v].y, am->mVertices[v].z);
            if (am->HasNormals()) m.n0.emplace_back(am->mNormals[v].x, am->mNormals[v].y, am->mNormals[v].z);
            else m.n0.emplace_back(0, 1, 0);
        }
        for (unsigned fI = 0; fI < am->mNumFaces; ++fI) {
            const aiFace& fc = am->mFaces[fI];
            if (fc.mNumIndices != 3) continue;
            m.idx.push_back(base + fc.mIndices[0]); m.idx.push_back(base + fc.mIndices[1]); m.idx.push_back(base + fc.mIndices[2]);
        }
    }
    if (m.idx.empty()) { err = "no triangles found"; return false; }
    finalizeModel(m);
    out = std::move(m);
    return true;
}

static Model makeSphere() {
    Model m; m.name = "Built-in: sphere";
    const int nu = 64, nv = 32;
    for (int j = 0; j <= nv; ++j) {
        float th = 3.14159265f * j / nv;
        for (int i = 0; i <= nu; ++i) {
            float ph = 6.2831853f * i / nu;
            vec3 p(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
            m.p0.push_back(p * 0.5f); m.n0.push_back(p);
        }
    }
    for (int j = 0; j < nv; ++j) for (int i = 0; i < nu; ++i) {
        unsigned a = j * (nu + 1) + i, b = a + nu + 1;
        m.idx.insert(m.idx.end(), {a, b, a + 1, a + 1, b, b + 1});
    }
    finalizeModel(m);
    return m;
}

static Model makeWing() {   // NACA 2412, aspect ratio 2 (span = 2 chords)
    Model m; m.name = "Built-in: NACA 2412 wing";
    const int n = 40; const float mc = 0.02f, pc = 0.4f, tt = 0.12f, span = 2.0f;
    auto foil = [&](float x, bool up) {
        float yt = 5 * tt * (0.2969f * std::sqrt(x) - 0.1260f * x - 0.3516f * x * x + 0.2843f * x * x * x - 0.1036f * x * x * x * x);
        float yc, dy;
        if (x < pc) { yc = mc / (pc * pc) * (2 * pc * x - x * x); dy = 2 * mc / (pc * pc) * (pc - x); }
        else { yc = mc / ((1 - pc) * (1 - pc)) * ((1 - 2 * pc) + 2 * pc * x - x * x); dy = 2 * mc / ((1 - pc) * (1 - pc)) * (pc - x); }
        float th = std::atan(dy);
        return up ? vec2(x - yt * std::sin(th), yc + yt * std::cos(th)) : vec2(x + yt * std::sin(th), yc - yt * std::cos(th));
    };
    std::vector<vec2> loop;
    for (int i = 0; i <= n; ++i) loop.push_back(foil(0.5f * (1 - std::cos(3.14159265f * i / n)), true));
    for (int i = n - 1; i >= 1; --i) loop.push_back(foil(0.5f * (1 - std::cos(3.14159265f * i / n)), false));
    const unsigned M = (unsigned)loop.size();
    for (unsigned k = 0; k < M; ++k) m.p0.emplace_back(loop[k].x, loop[k].y, -span / 2);   // ring A
    for (unsigned k = 0; k < M; ++k) m.p0.emplace_back(loop[k].x, loop[k].y, +span / 2);   // ring B
    for (unsigned k = 0; k < M; ++k) {
        unsigned k2 = (k + 1) % M;
        m.idx.insert(m.idx.end(), {k, M + k, k2, k2, M + k, M + k2});
    }
    vec2 cen(0); for (auto& p : loop) cen += p; cen /= (float)M;
    for (int side = 0; side < 2; ++side) {
        float z = side ? span / 2 : -span / 2;
        unsigned b = (unsigned)m.p0.size();
        for (unsigned k = 0; k < M; ++k) m.p0.emplace_back(loop[k].x, loop[k].y, z);
        m.p0.emplace_back(cen.x, cen.y, z);
        for (unsigned k = 0; k < M; ++k) m.idx.insert(m.idx.end(), {b + M, b + k, b + (k + 1) % M});
    }
    computeNormals(m);
    finalizeModel(m);
    return m;
}

// ============================================================================
//  GL helpers
// ============================================================================
static GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type); glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log); std::fprintf(stderr, "Shader error:\n%s\n", log); }
    return s;
}
static GLuint makeProgram(const char* vs, const char* fs) {
    GLuint p = glCreateProgram(), a = compile(GL_VERTEX_SHADER, vs), b = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, a); glAttachShader(p, b); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log); std::fprintf(stderr, "Link error:\n%s\n", log); }
    glDeleteShader(a); glDeleteShader(b);
    return p;
}

static const char* VS_MESH = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec3 aCol;
uniform mat4 uMVP; uniform mat4 uView; out vec3 vN; out vec3 vC;
void main(){ vN = mat3(uView)*aNrm; vC = aCol; gl_Position = uMVP*vec4(aPos,1.0); })";
static const char* FS_MESH = R"(#version 330 core
in vec3 vN; in vec3 vC; uniform vec3 uBase; uniform int uUseVC; out vec4 o;
void main(){ vec3 n = normalize(vN); float d = abs(dot(n, normalize(vec3(0.3,0.5,1.0))));
  vec3 base = (uUseVC==1) ? vC : uBase; o = vec4(base*(0.28+0.72*d),1.0); })";
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

struct GLMesh {
    GLuint vao = 0, b[4] = {0, 0, 0, 0}; GLsizei count = 0;
    void upload(const std::vector<vec3>& p, const std::vector<vec3>& n, const std::vector<vec3>& c, const std::vector<unsigned>& idx) {
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
    void updateColors(const std::vector<vec3>& c) {
        glBindBuffer(GL_ARRAY_BUFFER, b[2]); glBufferSubData(GL_ARRAY_BUFFER, 0, c.size() * sizeof(vec3), c.data());
    }
};

struct LV { vec3 p; vec4 c; };
struct DynLines {
    GLuint vao = 0, vbo = 0;
    void init() {
        glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LV), nullptr);
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LV), (void*)sizeof(vec3));
    }
    void draw(const std::vector<LV>& v, GLenum mode) {
        if (v.empty()) return;
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, v.size() * sizeof(LV), v.data(), GL_STREAM_DRAW);
        glDrawArrays(mode, 0, (GLsizei)v.size());
    }
};

static vec3 jet(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return vec3(std::clamp(1.5f - std::fabs(4 * t - 3), 0.f, 1.f), std::clamp(1.5f - std::fabs(4 * t - 2), 0.f, 1.f),
                std::clamp(1.5f - std::fabs(4 * t - 1), 0.f, 1.f));
}
static vec3 bwr(float t) {   // -1 blue .. 0 white .. +1 red
    t = std::clamp(t, -1.f, 1.f);
    return t < 0 ? vec3(1 + t, 1 + t, 1.f) : vec3(1.f, 1 - t, 1 - t);
}

// ============================================================================
//  Application state
// ============================================================================
struct Particles {
    int P = 4000, T = 16, maxAge = 500;
    float speed = 1.0f, seedY = 0.5f, seedZ = 0.5f, alpha = 0.8f;
    bool on = true;
    std::vector<vec3> tr; std::vector<int> age; std::vector<float> spd;
    std::mt19937 rng{12345};
    float rnd() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng); }
    void respawn(int p, bool anywhere, const Sim& s) {
        vec3 pos(anywhere ? 1.f + rnd() * (s.nx - 3) : 1.5f + rnd() * 1.5f,
                 s.ny * (0.5f + (rnd() - 0.5f) * seedY), s.nz * (0.5f + (rnd() - 0.5f) * seedZ));
        for (int k = 0; k < T; ++k) tr[(size_t)p * T + k] = pos;
        age[p] = anywhere ? (int)(rnd() * maxAge) : 0; spd[p] = 1.f;
    }
    void init(const Sim& s) {
        tr.assign((size_t)P * T, vec3(0)); age.assign(P, 0); spd.assign(P, 1.f);
        for (int p = 0; p < P; ++p) respawn(p, true, s);
    }
    void step(const Sim& s) {
        for (int p = 0; p < P; ++p) {
            vec3 pos = tr[(size_t)p * T];
            vec3 v = s.sample(pos, false).u / s.uLB;
            vec3 np = pos + v * speed;
            ++age[p];
            bool dead = age[p] > maxAge || !std::isfinite(np.x + np.y + np.z) || np.x > s.nx - 2 || np.x < 1 ||
                        np.y < 1 || np.y > s.ny - 2 || np.z < 1 || np.z > s.nz - 2 || s.isSolid(np);
            if (dead) { respawn(p, false, s); continue; }
            for (int k = T - 1; k >= 1; --k) tr[(size_t)p * T + k] = tr[(size_t)p * T + k - 1];
            tr[(size_t)p * T] = np; spd[p] = glm::length(v);
        }
    }
};

struct App {
    GLFWwindow* win = nullptr;
    Sim sim; Model model; bool haveModel = false;
    GLMesh mesh; std::vector<vec3> gridPos, gridNrm, vcol;
    Particles part;
    std::string status = "Drop an .fbx file onto the window, or type a path.";

    // model params
    char path[512] = "";
    int builtin = 0;
    float modelSizeM = 1.0f, lenCells = 40.f, posX = 0.30f, yaw = 0, pitch = 0, roll = 0;
    bool zUp = false, dirty = false;
    int gridDims[3] = {128, 64, 64};
    // flow params
    float wind = 30.f, rhoAir = 1.225f, nuAir = 1.5e-5f, uLB = 0.08f;
    int refMode = 0;
    // run
    bool running = false; float budgetMs = 25.f; double mlups = 0;
    // viz
    bool showSlice = true, showBox = true;
    int sliceAxis = 2, sliceIdx = 32, sliceField = 0;
    float speedMax = 1.6f, cpRange = 1.5f, vortMax = 6.f, sliceAlpha = 0.85f;
    int colorMode = 0; int cpFrame = 0;
    // voxel stats
    long long solidCount = 0; int areaFront = 1, areaPlan = 1;
    // camera
    float camYaw = 1.2f, camPitch = 0.3f, camDist = 200.f; vec3 camTarget{64, 32, 32};
    // history
    std::vector<float> hCd, hCl;
    // GL
    GLuint progMesh = 0, progLine = 0, progTex = 0, fbo = 0, fboTex = 0, fboDepth = 0, sliceTex = 0, quadVao = 0, quadVbo = 0;
    int fw = 0, fh = 0;
    DynLines trails, boxLines;
    std::vector<uint8_t> sliceImg;
} A;

static std::string g_drop;

// ============================================================================
//  Voxelisation
// ============================================================================
static void voxelize() {
    Sim& s = A.sim; const int nx = s.nx, ny = s.ny, nz = s.nz;
    std::vector<uint8_t> shell(s.N, 0);
    for (size_t t = 0; t + 2 < A.model.idx.size(); t += 3) {
        vec3 a = A.gridPos[A.model.idx[t]], b = A.gridPos[A.model.idx[t + 1]], c = A.gridPos[A.model.idx[t + 2]];
        vec3 mn = glm::min(glm::min(a, b), c), mx = glm::max(glm::max(a, b), c);
        if (mx.x < 0 || mx.y < 0 || mx.z < 0 || mn.x >= nx || mn.y >= ny || mn.z >= nz) continue;
        float L = std::max({glm::length(b - a), glm::length(c - a), glm::length(c - b)});
        int n = std::clamp((int)std::ceil(L / 0.35f), 1, 3000);    // sample spacing < 0.35 cell -> 26-connected shell
        for (int i = 0; i <= n; ++i) for (int j = 0; j <= n - i; ++j) {
            vec3 p = a + (b - a) * (i / (float)n) + (c - a) * (j / (float)n);
            int x = (int)std::floor(p.x), y = (int)std::floor(p.y), z = (int)std::floor(p.z);
            if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) continue;
            shell[s.id(x, y, z)] = 1;
        }
    }
    // Flood-fill the outside from the domain border (6-connected, so a 26-connected shell is watertight)
    std::vector<uint8_t> outside(s.N, 0);
    std::vector<uint32_t> st;
    auto push = [&](int x, int y, int z) {
        size_t i = s.id(x, y, z);
        if (!outside[i] && !shell[i]) { outside[i] = 1; st.push_back((uint32_t)i); }
    };
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        bool border = x == 0 || y == 0 || z == 0 || x == nx - 1 || y == ny - 1 || z == nz - 1;
        if (border) { shell[s.id(x, y, z)] = 0; push(x, y, z); }
    }
    while (!st.empty()) {
        uint32_t i = st.back(); st.pop_back();
        int x = i % nx, y = (i / nx) % ny, z = i / ((size_t)nx * ny);
        if (x > 0) push(x - 1, y, z); if (x < nx - 1) push(x + 1, y, z);
        if (y > 0) push(x, y - 1, z); if (y < ny - 1) push(x, y + 1, z);
        if (z > 0) push(x, y, z - 1); if (z < nz - 1) push(x, y, z + 1);
    }
    std::vector<uint8_t> pyz((size_t)ny * nz, 0), pxz((size_t)nx * nz, 0);
    A.solidCount = 0;
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        size_t i = s.id(x, y, z);
        s.solid[i] = outside[i] ? 0 : 1;
        if (s.solid[i]) { ++A.solidCount; pyz[y + (size_t)ny * z] = 1; pxz[x + (size_t)nx * z] = 1; }
    }
    A.areaFront = std::max(1, (int)std::count(pyz.begin(), pyz.end(), 1));
    A.areaPlan  = std::max(1, (int)std::count(pxz.begin(), pxz.end(), 1));
}

static void applyModel() {
    if (!A.haveModel) return;
    mat4 Rm = glm::rotate(mat4(1), glm::radians(A.yaw), vec3(0, 1, 0)) *
              glm::rotate(mat4(1), glm::radians(A.pitch), vec3(0, 0, 1)) *
              glm::rotate(mat4(1), glm::radians(A.roll), vec3(1, 0, 0));
    if (A.zUp) Rm = Rm * glm::rotate(mat4(1), glm::radians(-90.f), vec3(1, 0, 0));
    mat3 R(Rm);
    const Sim& s = A.sim;
    vec3 c(A.posX * s.nx, 0.5f * s.ny, 0.5f * s.nz);
    size_t n = A.model.p0.size();
    A.gridPos.resize(n); A.gridNrm.resize(n);
    for (size_t i = 0; i < n; ++i) {
        A.gridPos[i] = c + R * (A.model.p0[i] * A.lenCells);
        A.gridNrm[i] = glm::normalize(R * A.model.n0[i]);
    }
    A.vcol.assign(n, vec3(0.7f));
    A.mesh.upload(A.gridPos, A.gridNrm, A.vcol, A.model.idx);
    voxelize();
    A.sim.resetFlow(A.uLB);
    A.hCd.clear(); A.hCl.clear();
}

static void rebuildDomain() {
    for (int i = 0; i < 3; ++i) A.gridDims[i] = std::clamp(A.gridDims[i], 32, 384);
    A.sim.alloc(A.gridDims[0], A.gridDims[1], A.gridDims[2]);
    A.lenCells = std::min(A.lenCells, 0.8f * A.sim.nx);
    A.sliceIdx = std::clamp(A.sliceIdx, 0, A.sim.nz - 1);
    A.camTarget = vec3(A.sim.nx / 2.f, A.sim.ny / 2.f, A.sim.nz / 2.f);
    A.camDist = 1.5f * std::max({A.sim.nx, A.sim.ny, A.sim.nz});
    A.sim.resetFlow(A.uLB);
    A.part.init(A.sim);
    if (A.haveModel) applyModel();
}

static void loadAndApply(const std::string& path) {
    Model m; std::string err;
    if (!loadFile(path, m, err)) { A.status = "Load failed: " + err; return; }
    A.model = std::move(m); A.haveModel = true;
    A.status = "Loaded " + path + "  (" + std::to_string(A.model.idx.size() / 3) + " triangles)";
    applyModel();
}
static void useBuiltin(int which) {
    A.model = which == 0 ? makeSphere() : makeWing(); A.haveModel = true;
    A.status = "Using " + A.model.name; applyModel();
}

// ============================================================================
//  Rendering
// ============================================================================
static void ensureFBO(int w, int h) {
    if (A.fbo && w == A.fw && h == A.fh) return;
    if (A.fbo) { glDeleteFramebuffers(1, &A.fbo); glDeleteTextures(1, &A.fboTex); glDeleteRenderbuffers(1, &A.fboDepth); }
    A.fw = w; A.fh = h;
    glGenTextures(1, &A.fboTex); glBindTexture(GL_TEXTURE_2D, A.fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenRenderbuffers(1, &A.fboDepth); glBindRenderbuffer(GL_RENDERBUFFER, A.fboDepth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &A.fbo); glBindFramebuffer(GL_FRAMEBUFFER, A.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, A.fboTex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, A.fboDepth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void updateSurfaceColors() {
    const Sim& s = A.sim;
    const float scale = 1.f / (0.5f * s.uLB * s.uLB);
    const long long n = (long long)A.gridPos.size();
    #pragma omp parallel for
    for (long long i = 0; i < n; ++i) {
        vec3 p = A.gridPos[i], nn = A.gridNrm[i];
        vec3 q = p + nn * 1.5f;
        if (s.isSolid(q)) q = p - nn * 1.5f;
        Smp sm = s.sample(q, true);
        float cp = sm.ok ? (sm.rho - 1.f) / 3.f * scale : 0.f;
        A.vcol[i] = bwr(cp / A.cpRange);
    }
    A.mesh.updateColors(A.vcol);
}

static void updateSliceTexture() {
    const Sim& s = A.sim;
    int W, H;
    if (A.sliceAxis == 0) { W = s.ny; H = s.nz; } else if (A.sliceAxis == 1) { W = s.nx; H = s.nz; } else { W = s.nx; H = s.ny; }
    int k = std::clamp(A.sliceIdx, 0, (A.sliceAxis == 0 ? s.nx : A.sliceAxis == 1 ? s.ny : s.nz) - 1);
    A.sliceImg.resize((size_t)W * H * 4);
    const float invU = 1.f / s.uLB, cpScale = 1.f / (0.5f * s.uLB * s.uLB);
    const float lenScale = A.lenCells / s.uLB;
    #pragma omp parallel for
    for (int j = 0; j < H; ++j) for (int i = 0; i < W; ++i) {
        int x, y, z;
        if (A.sliceAxis == 0) { x = k; y = i; z = j; } else if (A.sliceAxis == 1) { x = i; y = k; z = j; } else { x = i; y = j; z = k; }
        size_t id = s.id(x, y, z);
        vec3 col;
        if (s.solid[id]) col = vec3(0.25f);
        else if (A.sliceField == 0) col = jet(std::sqrt(s.ux[id] * s.ux[id] + s.uy[id] * s.uy[id] + s.uz[id] * s.uz[id]) * invU / A.speedMax);
        else if (A.sliceField == 1) col = bwr((s.rho[id] - 1.f) / 3.f * cpScale / A.cpRange);
        else {
            float w = 0;
            if (x > 0 && y > 0 && z > 0 && x < s.nx - 1 && y < s.ny - 1 && z < s.nz - 1) {
                float wx = (s.uz[s.id(x, y + 1, z)] - s.uz[s.id(x, y - 1, z)]) - (s.uy[s.id(x, y, z + 1)] - s.uy[s.id(x, y, z - 1)]);
                float wy = (s.ux[s.id(x, y, z + 1)] - s.ux[s.id(x, y, z - 1)]) - (s.uz[s.id(x + 1, y, z)] - s.uz[s.id(x - 1, y, z)]);
                float wz = (s.uy[s.id(x + 1, y, z)] - s.uy[s.id(x - 1, y, z)]) - (s.ux[s.id(x, y + 1, z)] - s.ux[s.id(x, y - 1, z)]);
                w = 0.5f * std::sqrt(wx * wx + wy * wy + wz * wz) * lenScale;
            }
            col = jet(w / A.vortMax);
        }
        uint8_t* px = &A.sliceImg[((size_t)j * W + i) * 4];
        px[0] = (uint8_t)(255 * col.x); px[1] = (uint8_t)(255 * col.y); px[2] = (uint8_t)(255 * col.z); px[3] = 255;
    }
    glBindTexture(GL_TEXTURE_2D, A.sliceTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, A.sliceImg.data());
}

static void renderScene(int w, int h) {
    const Sim& s = A.sim;
    glBindFramebuffer(GL_FRAMEBUFFER, A.fbo); glViewport(0, 0, w, h);
    glDepthMask(GL_TRUE);
    glClearColor(0.09f, 0.10f, 0.12f, 1.f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    vec3 eye = A.camTarget + A.camDist * vec3(std::cos(A.camPitch) * std::cos(A.camYaw), std::sin(A.camPitch), std::cos(A.camPitch) * std::sin(A.camYaw));
    mat4 V = glm::lookAt(eye, A.camTarget, vec3(0, 1, 0));
    mat4 P = glm::perspective(glm::radians(45.f), (float)w / (float)h, 0.5f, 5000.f);
    mat4 MVP = P * V;

    // model
    if (A.mesh.count > 0) {
        glUseProgram(A.progMesh);
        glUniformMatrix4fv(glGetUniformLocation(A.progMesh, "uMVP"), 1, GL_FALSE, glm::value_ptr(MVP));
        glUniformMatrix4fv(glGetUniformLocation(A.progMesh, "uView"), 1, GL_FALSE, glm::value_ptr(V));
        glUniform3f(glGetUniformLocation(A.progMesh, "uBase"), 0.75f, 0.77f, 0.80f);
        glUniform1i(glGetUniformLocation(A.progMesh, "uUseVC"), A.colorMode == 1);
        glBindVertexArray(A.mesh.vao);
        glDrawElements(GL_TRIANGLES, A.mesh.count, GL_UNSIGNED_INT, nullptr);
    }
    glUseProgram(A.progLine);
    glUniformMatrix4fv(glGetUniformLocation(A.progLine, "uMVP"), 1, GL_FALSE, glm::value_ptr(MVP));

    // domain box
    if (A.showBox) {
        std::vector<LV> v; vec4 c(0.5f, 0.55f, 0.65f, 1.f);
        vec3 d((float)s.nx, (float)s.ny, (float)s.nz);
        vec3 cor[8]; for (int i = 0; i < 8; ++i) cor[i] = vec3((i & 1) ? d.x : 0, (i & 2) ? d.y : 0, (i & 4) ? d.z : 0);
        int e[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
        for (auto& ed : e) { v.push_back({cor[ed[0]], c}); v.push_back({cor[ed[1]], c}); }
        A.boxLines.draw(v, GL_LINES);
    }

    // slice
    if (A.showSlice) {
        updateSliceTexture();
        float k = A.sliceIdx + 0.5f;
        vec3 q[4]; vec2 uv[4] = {{0,0},{1,0},{1,1},{0,1}};
        if (A.sliceAxis == 0) { q[0]={k,0,0}; q[1]={k,(float)s.ny,0}; q[2]={k,(float)s.ny,(float)s.nz}; q[3]={k,0,(float)s.nz}; }
        else if (A.sliceAxis == 1) { q[0]={0,k,0}; q[1]={(float)s.nx,k,0}; q[2]={(float)s.nx,k,(float)s.nz}; q[3]={0,k,(float)s.nz}; }
        else { q[0]={0,0,k}; q[1]={(float)s.nx,0,k}; q[2]={(float)s.nx,(float)s.ny,k}; q[3]={0,(float)s.ny,k}; }
        int tri[6] = {0,1,2,0,2,3}; float vb[6 * 5];
        for (int i = 0; i < 6; ++i) { vb[i*5+0]=q[tri[i]].x; vb[i*5+1]=q[tri[i]].y; vb[i*5+2]=q[tri[i]].z; vb[i*5+3]=uv[tri[i]].x; vb[i*5+4]=uv[tri[i]].y; }
        glUseProgram(A.progTex);
        glUniformMatrix4fv(glGetUniformLocation(A.progTex, "uMVP"), 1, GL_FALSE, glm::value_ptr(MVP));
        glUniform1f(glGetUniformLocation(A.progTex, "uAlpha"), A.sliceAlpha);
        glUniform1i(glGetUniformLocation(A.progTex, "uTex"), 0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, A.sliceTex);
        glBindVertexArray(A.quadVao); glBindBuffer(GL_ARRAY_BUFFER, A.quadVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vb), vb, GL_STREAM_DRAW);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDepthMask(GL_TRUE);
    }

    // particle trails
    if (A.part.on) {
        std::vector<LV> v; v.reserve((size_t)A.part.P * (A.part.T - 1) * 2);
        for (int p = 0; p < A.part.P; ++p) {
            vec3 col = jet(A.part.spd[p] / A.speedMax);
            for (int k = 0; k < A.part.T - 1; ++k) {
                vec3 a = A.part.tr[(size_t)p * A.part.T + k], b = A.part.tr[(size_t)p * A.part.T + k + 1];
                if (a == b) continue;
                float al = (1.f - (float)k / (A.part.T - 1)) * A.part.alpha;
                v.push_back({a, vec4(col, al)}); v.push_back({b, vec4(col, al * 0.8f)});
            }
        }
        glUseProgram(A.progLine);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        A.trails.draw(v, GL_LINES);
        glDepthMask(GL_TRUE);
    }
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ============================================================================
//  UI
// ============================================================================
static void drawDockspace() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize); ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGuiWindowFlags fl = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                          ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
    ImGui::Begin("DockHost", nullptr, fl);
    ImGui::PopStyleVar(3);
    ImGuiID dock = ImGui::GetID("MainDock");
    if (!ImGui::DockBuilderGetNode(dock)) {           // first run (no saved layout)
        ImGui::DockBuilderRemoveNode(dock);
        ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dock, vp->WorkSize);
        ImGuiID rest, center;
        ImGuiID left = ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.24f, nullptr, &rest);
        ImGuiID right = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.26f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Controls", left);
        ImGui::DockBuilderDockWindow("Results", right);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderFinish(dock);
    }
    ImGui::DockSpace(dock, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

static void drawControls() {
    Sim& s = A.sim;
    ImGui::Begin("Controls");
    auto commit = [&]() { if (ImGui::IsItemDeactivatedAfterEdit()) A.dirty = true; };

    if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText("##path", A.path, sizeof(A.path)); ImGui::SameLine();
        if (ImGui::Button("Load")) loadAndApply(A.path);
        ImGui::TextDisabled("or drag & drop a file onto the window");
        ImGui::Combo("##builtin", &A.builtin, "Sphere\0NACA 2412 wing\0"); ImGui::SameLine();
        if (ImGui::Button("Use built-in")) useBuiltin(A.builtin);
        ImGui::SliderFloat("Size (m)", &A.modelSizeM, 0.01f, 50.f, "%.2f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Length (cells)", &A.lenCells, 10.f, 0.8f * s.nx, "%.0f"); commit();
        ImGui::SliderFloat("Position X", &A.posX, 0.15f, 0.6f, "%.2f"); commit();
        ImGui::SliderFloat("Pitch / AoA (deg)", &A.pitch, -90.f, 90.f, "%.1f"); commit();
        ImGui::SliderFloat("Yaw (deg)", &A.yaw, -180.f, 180.f, "%.1f"); commit();
        ImGui::SliderFloat("Roll (deg)", &A.roll, -180.f, 180.f, "%.1f"); commit();
        if (ImGui::Checkbox("Model is Z-up", &A.zUp)) A.dirty = true;
        ImGui::TextDisabled("Flow goes +X, lift is +Y. Rotate model so its nose points to +X.");
    }
    if (ImGui::CollapsingHeader("Flow", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Wind speed (m/s)", &A.wind, 1.f, 300.f, "%.1f");
        ImGui::InputFloat("Air density (kg/m3)", &A.rhoAir, 0, 0, "%.4f");
        ImGui::InputFloat("Kinematic visc. (m2/s)", &A.nuAir, 0, 0, "%.3e");
        ImGui::SliderFloat("Lattice velocity", &A.uLB, 0.02f, 0.15f, "%.3f");
        ImGui::SameLine(); ImGui::TextDisabled("(on Reset)");
        ImGui::SliderFloat("Smagorinsky C", &s.smagC, 0.f, 0.3f, "%.3f");
    }
    if (ImGui::CollapsingHeader("Domain")) {
        ImGui::InputInt3("Grid (x y z)", A.gridDims);
        ImGui::Text("%.2f M cells, ~%.0f MB", (double)A.gridDims[0] * A.gridDims[1] * A.gridDims[2] / 1e6,
                    (double)A.gridDims[0] * A.gridDims[1] * A.gridDims[2] * (19 * 2 + 4 + 1) * 4 / 1e6);
        if (ImGui::Button("Rebuild domain")) rebuildDomain();
    }
    if (ImGui::CollapsingHeader("Run", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button(A.running ? "Pause" : "Run", ImVec2(80, 0))) A.running = !A.running;
        ImGui::SameLine();
        if (ImGui::Button("Reset flow")) { s.resetFlow(A.uLB); A.hCd.clear(); A.hCl.clear(); }
        ImGui::SliderFloat("Budget (ms/frame)", &A.budgetMs, 2.f, 100.f, "%.0f");
    }
    if (ImGui::CollapsingHeader("Visualisation", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Domain box", &A.showBox);
        ImGui::Separator();
        ImGui::Checkbox("Slice", &A.showSlice);
        ImGui::Combo("Axis", &A.sliceAxis, "X\0Y\0Z\0");
        int mx = (A.sliceAxis == 0 ? s.nx : A.sliceAxis == 1 ? s.ny : s.nz) - 1;
        A.sliceIdx = std::clamp(A.sliceIdx, 0, mx);
        ImGui::SliderInt("Position", &A.sliceIdx, 0, mx);
        ImGui::Combo("Field", &A.sliceField, "Speed |u|/U\0Pressure Cp\0Vorticity\0");
        ImGui::SliderFloat("Speed max (u/U)", &A.speedMax, 0.5f, 3.f);
        ImGui::SliderFloat("Cp range", &A.cpRange, 0.2f, 4.f);
        ImGui::SliderFloat("Vorticity max", &A.vortMax, 0.5f, 30.f);
        ImGui::SliderFloat("Slice opacity", &A.sliceAlpha, 0.f, 1.f);
        ImGui::Separator();
        ImGui::Checkbox("Streamlines", &A.part.on);
        int P = A.part.P, T = A.part.T;
        ImGui::SliderInt("Particles", &P, 200, 20000); ImGui::SliderInt("Trail length", &T, 2, 40);
        if (P != A.part.P || T != A.part.T) { A.part.P = P; A.part.T = T; A.part.init(s); }
        ImGui::SliderFloat("Particle speed", &A.part.speed, 0.1f, 3.f);
        ImGui::SliderFloat("Seed spread Y", &A.part.seedY, 0.05f, 0.95f);
        ImGui::SliderFloat("Seed spread Z", &A.part.seedZ, 0.05f, 0.95f);
        ImGui::SliderFloat("Trail opacity", &A.part.alpha, 0.05f, 1.f);
        ImGui::Separator();
        ImGui::Combo("Surface colour", &A.colorMode, "Solid\0Pressure Cp\0");
        if (ImGui::Button("Reset camera")) {
            A.camTarget = vec3(s.nx / 2.f, s.ny / 2.f, s.nz / 2.f); A.camDist = 1.5f * std::max({s.nx, s.ny, s.nz});
            A.camYaw = 1.2f; A.camPitch = 0.3f;
        }
    }
    if (A.dirty) { applyModel(); A.dirty = false; }
    ImGui::End();
}

static void drawResults() {
    Sim& s = A.sim;
    ImGui::Begin("Results");
    ImGui::TextWrapped("%s", A.status.c_str());
    ImGui::Separator();
    float Re = std::max(1.f, A.wind * A.modelSizeM / A.nuAir);
    float nuLB = std::max(A.uLB * A.lenCells / Re, 0.001f);
    float ReEff = A.uLB * A.lenCells / nuLB;
    ImGui::Text("Re (physical)  : %.3g", Re);
    ImGui::Text("Re (simulated) : %.3g%s", ReEff, ReEff < Re * 0.99f ? "  (capped, LES)" : "");
    ImGui::Text("tau0 = %.4f   Mach = %.3f", s.tau0, A.uLB * 1.7320508f);
    ImGui::Text("Cells: %lld solid", A.solidCount);
    ImGui::Combo("Ref. area", &A.refMode, "Frontal (YZ)\0Planform (XZ)\0");
    int Acells = A.refMode == 0 ? A.areaFront : A.areaPlan;
    double dx = A.modelSizeM / A.lenCells, Aphys = Acells * dx * dx;
    double qLB = 0.5 * s.uLB * s.uLB * Acells, qPh = 0.5 * A.rhoAir * A.wind * A.wind;
    double Cd = s.Fs[0] / qLB, Cl = s.Fs[1] / qLB, Cs = s.Fs[2] / qLB;
    ImGui::Text("A_ref = %.4g m2", Aphys);
    ImGui::Separator();
    if (s.diverged) ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "SIMULATION DIVERGED - lower lattice velocity / raise\nSmagorinsky C / reduce wind speed, then Reset.");
    ImGui::Text("Cd = %.4f", Cd); ImGui::Text("Cl = %.4f", Cl); ImGui::Text("Cs = %.4f", Cs);
    ImGui::Text("Drag = %.4g N", Cd * qPh * Aphys);
    ImGui::Text("Lift = %.4g N", Cl * qPh * Aphys);
    ImGui::Text("Side = %.4g N", Cs * qPh * Aphys);
    if (A.hCd.size() > 1) {
        ImGui::PlotLines("##cd", A.hCd.data(), (int)A.hCd.size(), 0, "Cd", FLT_MAX, FLT_MAX, ImVec2(0, 60));
        ImGui::PlotLines("##cl", A.hCl.data(), (int)A.hCl.size(), 0, "Cl", FLT_MAX, FLT_MAX, ImVec2(0, 60));
    }
    ImGui::Separator();
    ImGui::Text("Steps: %lld   (%.1f MLUPS)", s.steps, A.mlups);
    ImGui::TextDisabled("Let the flow develop for a few flow-throughs\n(~ nx / lattice velocity steps) before reading Cd/Cl.");
    ImGui::End();
}

static void drawViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x, h = (int)avail.y;
    if (w > 16 && h > 16) {
        ensureFBO(w, h);
        if (A.colorMode == 1 && A.haveModel && (A.cpFrame++ % 3 == 0)) updateSurfaceColors();
        renderScene(w, h);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##vp", ImVec2((float)w, (float)h),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)A.fboTex, p0, ImVec2(p0.x + w, p0.y + h), ImVec2(0, 1), ImVec2(1, 0));
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemActive()) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                A.camYaw -= io.MouseDelta.x * 0.005f;
                A.camPitch = std::clamp(A.camPitch + io.MouseDelta.y * 0.005f, -1.5f, 1.5f);
            } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                vec3 eye = vec3(std::cos(A.camPitch) * std::cos(A.camYaw), std::sin(A.camPitch), std::cos(A.camPitch) * std::sin(A.camYaw));
                vec3 right = glm::normalize(glm::cross(vec3(0, 1, 0), eye)), up = glm::cross(eye, right);
                A.camTarget += (right * io.MouseDelta.x + up * io.MouseDelta.y) * (A.camDist * 0.0015f);
            }
        }
        if (ImGui::IsItemHovered() && io.MouseWheel != 0.f) A.camDist = std::clamp(A.camDist * (1.f - 0.1f * io.MouseWheel), 5.f, 3000.f);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ============================================================================
//  main
// ============================================================================
int main() {
    initLattice();
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    A.win = glfwCreateWindow(1600, 900, "AeroLBM - virtual wind tunnel", nullptr, nullptr);
    if (!A.win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(A.win); glfwSwapInterval(1);
    glfwSetDropCallback(A.win, [](GLFWwindow*, int n, const char** paths) { if (n > 0) g_drop = paths[0]; });
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { std::fprintf(stderr, "GLEW init failed\n"); return 1; }
    glGetError();   // swallow GLEW's spurious error on core profiles

    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(A.win, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    A.progMesh = makeProgram(VS_MESH, FS_MESH); A.progLine = makeProgram(VS_LINE, FS_LINE); A.progTex = makeProgram(VS_TEX, FS_TEX);
    A.trails.init(); A.boxLines.init();
    glGenTextures(1, &A.sliceTex); glBindTexture(GL_TEXTURE_2D, A.sliceTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenVertexArrays(1, &A.quadVao); glGenBuffers(1, &A.quadVbo);
    glBindVertexArray(A.quadVao); glBindBuffer(GL_ARRAY_BUFFER, A.quadVbo);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    A.sim.alloc(A.gridDims[0], A.gridDims[1], A.gridDims[2]);
    A.sliceIdx = A.sim.nz / 2;
    A.camTarget = vec3(A.sim.nx / 2.f, A.sim.ny / 2.f, A.sim.nz / 2.f);
    A.camDist = 1.5f * std::max({A.sim.nx, A.sim.ny, A.sim.nz});
    A.part.init(A.sim);
    useBuiltin(0);

    while (!glfwWindowShouldClose(A.win)) {
        glfwPollEvents();
        if (!g_drop.empty()) { std::snprintf(A.path, sizeof(A.path), "%s", g_drop.c_str()); loadAndApply(g_drop); g_drop.clear(); }

        // ---- solver ----
        {
            float Re = std::max(1.f, A.wind * A.modelSizeM / A.nuAir);
            float nuLB = std::max(A.uLB * A.lenCells / Re, 0.001f);
            A.sim.tau0 = 3.f * nuLB + 0.5f;
        }
        if (A.running && !A.sim.diverged) {
            double t0 = glfwGetTime(); int n = 0;
            do { A.sim.step(); ++n; } while (glfwGetTime() - t0 < A.budgetMs * 1e-3 && n < 200 && !A.sim.diverged);
            double dt = glfwGetTime() - t0;
            if (dt > 0) A.mlups = 0.9 * A.mlups + 0.1 * (n * (double)A.sim.N / 1e6 / dt);
            int Ac = A.refMode == 0 ? A.areaFront : A.areaPlan;
            double q = 0.5 * A.sim.uLB * A.sim.uLB * Ac;
            A.hCd.push_back((float)(A.sim.Fs[0] / q)); A.hCl.push_back((float)(A.sim.Fs[1] / q));
            if (A.hCd.size() > 400) { A.hCd.erase(A.hCd.begin()); A.hCl.erase(A.hCl.begin()); }
        }
        if (A.part.on) A.part.step(A.sim);

        // ---- UI ----
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        drawDockspace(); drawControls(); drawResults(); drawViewport();
        ImGui::Render();

        int fbw, fbh; glfwGetFramebufferSize(A.win, &fbw, &fbh);
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, fbw, fbh);
        glClearColor(0.06f, 0.06f, 0.07f, 1.f); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(A.win);
    }

    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    glfwDestroyWindow(A.win); glfwTerminate();
    return 0;
}
