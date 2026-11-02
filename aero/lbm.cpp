#include "lbm.h"

#include <algorithm>
#include <cmath>

using glm::vec3;

// ============================================================================
//  D3Q19 lattice
// ============================================================================
static int   C[19][3];
static float W[19];
static int   OPP[19];

static const bool g_latticeReady = [] {
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
    return true;
}();

static inline float feqf(int q, float r, float ux, float uy, float uz) {
    float cu  = 3.f * (C[q][0] * ux + C[q][1] * uy + C[q][2] * uz);
    float usq = 1.5f * (ux * ux + uy * uy + uz * uz);
    return W[q] * r * (1.f + cu + 0.5f * cu * cu - usq);
}

// ============================================================================
//  Sim
// ============================================================================
void Sim::alloc(int X, int Y, int Z) {
    nx = X; ny = Y; nz = Z; N = (size_t)nx * ny * nz;
    f.assign(19 * N, 0.f); ft.assign(19 * N, 0.f);
    rho.assign(N, 1.f); ux.assign(N, 0.f); uy.assign(N, 0.f); uz.assign(N, 0.f);
    solid.assign(N, 0);
    steps = 0; diverged = false; Fs[0] = Fs[1] = Fs[2] = 0;
}

void Sim::resetFlow(float u) {
    uLB = u; steps = 0; diverged = false; Fs[0] = Fs[1] = Fs[2] = 0;
    float fe[19];
    for (int q = 0; q < 19; ++q) fe[q] = feqf(q, 1.f, uLB, 0, 0);
    #pragma omp parallel for
    for (long long i = 0; i < (long long)N; ++i) {
        for (int q = 0; q < 19; ++q) { f[(size_t)q * N + i] = fe[q]; ft[(size_t)q * N + i] = fe[q]; }
        rho[i] = 1.f; ux[i] = solid[i] ? 0.f : uLB; uy[i] = 0.f; uz[i] = 0.f;
    }
}

bool Sim::isSolid(const vec3& p) const {
    int x = (int)std::floor(p.x), y = (int)std::floor(p.y), z = (int)std::floor(p.z);
    if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) return false;
    return solid[id(x, y, z)] != 0;
}

Smp Sim::sample(const vec3& p, bool mask) const {
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

void Sim::step() {
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

// ============================================================================
//  Voxelisation
// ============================================================================
VoxelStats Sim::voxelize(const std::vector<vec3>& pos, const std::vector<unsigned>& idx, bool cellCentres) {
    Sim& s = *this;
    VoxelStats r;
    std::vector<uint8_t> shell(s.N, 0);
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        vec3 a = pos[idx[t]], b = pos[idx[t + 1]], c = pos[idx[t + 2]];
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
        int x = i % nx, y = (i / nx) % ny, z = (int)(i / ((size_t)nx * ny));
        if (x > 0) push(x - 1, y, z);
        if (x < nx - 1) push(x + 1, y, z);
        if (y > 0) push(x, y - 1, z);
        if (y < ny - 1) push(x, y + 1, z);
        if (z > 0) push(x, y, z - 1);
        if (z < nz - 1) push(x, y, z + 1);
    }

    // The shell over-marks: any cell the surface touches is solid, which fattens the body by
    // ~half a cell all round. Re-classify shell cells by whether their centre is inside the mesh,
    // using crossing parity along an x ray through each (y, z) row of cell centres. Rows with an
    // odd crossing count (open or broken meshes) keep the conservative shell.
    if (cellCentres) {
        const float oy = 0.5f + 1.3e-4f, oz = 0.5f + 2.7e-4f;      // nudge rays off vertices / edges
        std::vector<std::vector<float>> hits((size_t)ny * nz);
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            const vec3 a = pos[idx[t]], b = pos[idx[t + 1]], c = pos[idx[t + 2]];
            const float det = (b.y - a.y) * (c.z - a.z) - (c.y - a.y) * (b.z - a.z);
            if (std::fabs(det) < 1e-12f) continue;                  // edge-on to the ray
            const int y0 = std::max(0, (int)std::ceil(std::min({a.y, b.y, c.y}) - oy));
            const int y1 = std::min(ny - 1, (int)std::floor(std::max({a.y, b.y, c.y}) - oy));
            const int z0 = std::max(0, (int)std::ceil(std::min({a.z, b.z, c.z}) - oz));
            const int z1 = std::min(nz - 1, (int)std::floor(std::max({a.z, b.z, c.z}) - oz));
            for (int z = z0; z <= z1; ++z) for (int y = y0; y <= y1; ++y) {
                const float py = y + oy - a.y, pz = z + oz - a.z;
                const float u = (py * (c.z - a.z) - (c.y - a.y) * pz) / det;
                const float v = ((b.y - a.y) * pz - py * (b.z - a.z)) / det;
                if (u < 0 || v < 0 || u + v > 1) continue;
                hits[y + (size_t)ny * z].push_back(a.x + u * (b.x - a.x) + v * (c.x - a.x));
            }
        }
        std::vector<size_t> cleared;
        for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) {
            std::vector<float>& h = hits[y + (size_t)ny * z];
            if (h.size() % 2) continue;
            std::sort(h.begin(), h.end());
            for (int x = 0; x < nx; ++x) {
                const size_t i = s.id(x, y, z);
                if (!shell[i] || outside[i]) continue;
                const size_t before = std::lower_bound(h.begin(), h.end(), x + 0.5f) - h.begin();
                if (before % 2 == 0) cleared.push_back(i);
            }
        }
        // Keep the shell if re-classification would erase the body (thinner than a cell everywhere).
        size_t solidBefore = 0;
        for (size_t i = 0; i < s.N; ++i) solidBefore += !outside[i];
        if (cleared.size() < solidBefore)
            for (size_t i : cleared) outside[i] = 1;
    }

    std::vector<uint8_t> pyz((size_t)ny * nz, 0), pxz((size_t)nx * nz, 0);
    for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
        size_t i = s.id(x, y, z);
        s.solid[i] = outside[i] ? 0 : 1;
        if (s.solid[i]) { ++r.solidCells; pyz[y + (size_t)ny * z] = 1; pxz[x + (size_t)nx * z] = 1; }
    }
    r.areaFront = std::max(1, (int)std::count(pyz.begin(), pyz.end(), 1));
    r.areaPlan  = std::max(1, (int)std::count(pxz.begin(), pxz.end(), 1));
    return r;
}
