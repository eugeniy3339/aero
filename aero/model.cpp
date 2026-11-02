#include "model.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <ufbx/ufbx.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>

using glm::vec2; using glm::vec3;

const char* const BUILTIN_NAMES[Builtin_Count] = { "Sphere", "NACA 2412 wing" };

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

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

// FBX / OBJ via ufbx. Every mesh instance is baked into world space (Y-up); vertices are
// shared per mesh position so the normals come out smooth.
static bool loadUfbx(const std::string& path, Model& m, std::string& err) {
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;
    ufbx_error uerr;
    ufbx_scene* sc = ufbx_load_file(path.c_str(), &opts, &uerr);
    if (!sc) { char buf[512]; ufbx_format_error(buf, sizeof(buf), &uerr); err = buf; return false; }
    std::vector<uint32_t> tri;
    for (size_t ni = 0; ni < sc->nodes.count; ++ni) {
        const ufbx_node* node = sc->nodes.data[ni];
        const ufbx_mesh* mesh = node->mesh;
        if (!mesh) continue;
        unsigned base = (unsigned)m.p0.size();
        for (size_t v = 0; v < mesh->vertices.count; ++v) {
            ufbx_vec3 p = ufbx_transform_position(&node->geometry_to_world, mesh->vertices.data[v]);
            m.p0.emplace_back((float)p.x, (float)p.y, (float)p.z);
        }
        tri.resize(mesh->max_face_triangles * 3);
        for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
            uint32_t nt = ufbx_triangulate_face(tri.data(), tri.size(), mesh, mesh->faces.data[fi]);
            for (uint32_t k = 0; k < nt * 3; ++k) m.idx.push_back(base + mesh->vertex_indices.data[tri[k]]);
        }
    }
    ufbx_free_scene(sc);
    return true;
}

// Binary or ASCII STL. Identical corners are welded.
static bool loadStl(const std::string& path, Model& m, std::string& err) {
    FILE* fp = _wfopen(widen(path).c_str(), L"rb");
    if (!fp) { err = "cannot open file"; return false; }
    std::vector<char> data;
    char chunk[65536]; size_t got;
    while ((got = std::fread(chunk, 1, sizeof(chunk), fp)) > 0) data.insert(data.end(), chunk, chunk + got);
    std::fclose(fp);

    std::vector<vec3> corners;
    uint32_t nTri = 0;
    if (data.size() >= 84) std::memcpy(&nTri, &data[80], 4);
    if (data.size() >= 84 && data.size() == 84 + (size_t)nTri * 50) {
        for (uint32_t t = 0; t < nTri; ++t)
            for (int k = 0; k < 3; ++k) {
                float v[3]; std::memcpy(v, &data[84 + (size_t)t * 50 + 12 + k * 12], 12);
                corners.emplace_back(v[0], v[1], v[2]);
            }
    } else {
        data.push_back('\0');
        const char* s = data.data();
        while ((s = std::strstr(s, "vertex")) != nullptr) {
            s += 6;
            float x, y, z;
            if (std::sscanf(s, "%f %f %f", &x, &y, &z) == 3) corners.emplace_back(x, y, z);
        }
        corners.resize(corners.size() / 3 * 3);
    }
    struct Key { uint32_t a, b, c; bool operator==(const Key& o) const { return a == o.a && b == o.b && c == o.c; } };
    struct KeyHash { size_t operator()(const Key& k) const { return ((size_t)k.a * 73856093u) ^ ((size_t)k.b * 19349663u) ^ ((size_t)k.c * 83492791u); } };
    std::unordered_map<Key, unsigned, KeyHash> weld;
    for (const vec3& p : corners) {
        Key k; std::memcpy(&k.a, &p.x, 4); std::memcpy(&k.b, &p.y, 4); std::memcpy(&k.c, &p.z, 4);
        auto [it, inserted] = weld.try_emplace(k, (unsigned)m.p0.size());
        if (inserted) m.p0.push_back(p);
        m.idx.push_back(it->second);
    }
    return true;
}

bool loadModelFile(const std::string& path, Model& out, std::string& err) {
    Model m;
    size_t slash = path.find_last_of("/\\");
    m.name = slash == std::string::npos ? path : path.substr(slash + 1);
    std::string ext = path.substr(std::min(path.size(), path.find_last_of('.')));
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    bool ok = ext == ".stl" ? loadStl(path, m, err) : loadUfbx(path, m, err);
    if (!ok) return false;
    if (m.idx.empty()) { err = "no triangles found"; return false; }
    computeNormals(m);
    finalizeModel(m);
    out = std::move(m);
    return true;
}

static Model makeSphere() {
    Model m;
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
    Model m;
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

Model makeBuiltin(BuiltinShape shape) {
    Model m = shape == Builtin_Wing ? makeWing() : makeSphere();
    m.name = std::string("Built-in ") + BUILTIN_NAMES[shape];
    return m;
}
