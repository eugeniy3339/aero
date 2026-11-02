// ============================================================================
//  Scene: model placement + voxelisation, solver driving, particles and the
//  3D viewport rendering.
// ============================================================================
#include "app.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>

using glm::vec2; using glm::vec3; using glm::vec4; using glm::mat3; using glm::mat4;

App A;

static double nowSeconds() {
    static const double invFreq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return 1.0 / (double)f.QuadPart; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart * invFreq;
}

// ============================================================================
//  Particles
// ============================================================================
void Particles::respawn(int p, bool anywhere, const Sim& s) {
    vec3 pos(anywhere ? 1.f + rnd() * (s.nx - 3) : 1.5f + rnd() * 1.5f,
             s.ny * (0.5f + (rnd() - 0.5f) * seedY), s.nz * (0.5f + (rnd() - 0.5f) * seedZ));
    for (int k = 0; k < T; ++k) tr[(size_t)p * T + k] = pos;
    age[p] = anywhere ? (int)(rnd() * maxAge) : 0; spd[p] = 1.f;
}

void Particles::init(const Sim& s) {
    tr.assign((size_t)P * T, vec3(0)); age.assign(P, 0); spd.assign(P, 1.f);
    for (int p = 0; p < P; ++p) respawn(p, true, s);
}

void Particles::step(const Sim& s) {
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

// ============================================================================
//  Camera
// ============================================================================
vec3 OrbitCamera::dir() const {
    return vec3(std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw));
}

mat4 OrbitCamera::view() const {
    return glm::lookAt(target + dist * dir(), target, vec3(0, 1, 0));
}

void OrbitCamera::frame(const Sim& s) {
    target = vec3(s.nx / 2.f, s.ny / 2.f, s.nz / 2.f);
    dist = 1.5f * std::max({s.nx, s.ny, s.nz});
    yaw = 1.2f; pitch = 0.3f;
}

// ============================================================================
//  Derived quantities
// ============================================================================
FlowScaling flowScaling() {
    FlowScaling f;
    const float u = A.sim.uLB;   // the velocity the solver actually runs with
    f.Re   = std::max(1.f, A.wind * A.modelSizeM / A.nuAir);
    f.nuLB = std::max(u * A.lenCells / f.Re, 0.001f);   // clamp keeps tau0 stable; Smagorinsky models the rest
    f.ReSim = u * A.lenCells / f.nuLB;
    f.tau0 = 3.f * f.nuLB + 0.5f;
    f.mach = u * 1.7320508f;
    return f;
}

Forces forces() {
    const Sim& s = A.sim;
    Forces r;
    const int Acells = A.refMode == Ref_Frontal ? A.vox.areaFront : A.vox.areaPlan;
    const double dx = A.modelSizeM / A.lenCells;
    const double qLB = 0.5 * s.uLB * s.uLB * Acells, qPh = 0.5 * A.rhoAir * A.wind * A.wind;
    r.Aref = Acells * dx * dx;
    r.Cd = s.Fs[0] / qLB; r.Cl = s.Fs[1] / qLB; r.Cs = s.Fs[2] / qLB;
    r.drag = r.Cd * qPh * r.Aref; r.lift = r.Cl * qPh * r.Aref; r.side = r.Cs * qPh * r.Aref;
    return r;
}

int sliceCount() {
    return A.sliceAxis == 0 ? A.sim.nx : A.sliceAxis == 1 ? A.sim.ny : A.sim.nz;
}

// ============================================================================
//  Model / domain management
// ============================================================================
void resetFlow() {
    A.sim.resetFlow(A.uLB);
    A.hCd.clear(); A.hCl.clear();
}

void applyModel() {
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
    A.vox = A.sim.voxelize(A.gridPos, A.model.idx);
    resetFlow();
}

void rebuildDomain() {
    for (int i = 0; i < 3; ++i) A.gridDims[i] = std::clamp(A.gridDims[i], 32, 384);
    A.sim.alloc(A.gridDims[0], A.gridDims[1], A.gridDims[2]);
    A.lenCells = std::min(A.lenCells, 0.8f * A.sim.nx);
    A.sliceIdx = sliceCount() / 2;
    A.cam.frame(A.sim);
    A.part.init(A.sim);
    if (A.haveModel) applyModel(); else resetFlow();
}

void loadModelPath(const std::string& path) {
    if (path.empty()) return;
    Model m; std::string err;
    if (!loadModelFile(path, m, err)) { A.status = "Could not load " + path + ": " + err; A.statusError = true; return; }
    A.model = std::move(m); A.haveModel = true;
    A.status = "Loaded " + A.model.name + " (" + std::to_string(A.model.idx.size() / 3) + " triangles)";
    A.statusError = false;
    applyModel();
}

void useBuiltin(int which) {
    A.builtin = std::clamp(which, 0, (int)Builtin_Count - 1);
    A.model = makeBuiltin((BuiltinShape)A.builtin); A.haveModel = true;
    A.status = "Using " + A.model.name; A.statusError = false;
    applyModel();
}

// ============================================================================
//  Solver driving
// ============================================================================
void advanceSimulation() {
    A.sim.tau0 = flowScaling().tau0;
    if (A.running && !A.sim.diverged) {
        double t0 = nowSeconds(); int n = 0;
        do { A.sim.step(); ++n; } while (nowSeconds() - t0 < A.budgetMs * 1e-3 && n < 200 && !A.sim.diverged);
        double dt = nowSeconds() - t0;
        if (dt > 0) A.mlups = 0.9 * A.mlups + 0.1 * (n * (double)A.sim.N / 1e6 / dt);
        Forces f = forces();
        A.hCd.push_back((float)f.Cd); A.hCl.push_back((float)f.Cl);
        if (A.hCd.size() > 600) { A.hCd.erase(A.hCd.begin()); A.hCl.erase(A.hCl.begin()); }
        if (A.sim.diverged) { A.running = false; A.status = "Simulation diverged - see Results."; A.statusError = true; }
    }
    if (A.part.on) A.part.step(A.sim);
}

// ============================================================================
//  Rendering
// ============================================================================
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
        A.vcol[i] = colormapBwr(cp / A.cpRange);
    }
    A.mesh.updateColors(A.vcol);
}

static void updateSliceTexture() {
    const Sim& s = A.sim;
    int W, H;
    if (A.sliceAxis == 0) { W = s.ny; H = s.nz; } else if (A.sliceAxis == 1) { W = s.nx; H = s.nz; } else { W = s.nx; H = s.ny; }
    const int k = std::clamp(A.sliceIdx, 0, sliceCount() - 1);
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
        else if (A.sliceField == Field_Speed) col = colormapJet(std::sqrt(s.ux[id] * s.ux[id] + s.uy[id] * s.uy[id] + s.uz[id] * s.uz[id]) * invU / A.speedMax);
        else if (A.sliceField == Field_Pressure) col = colormapBwr((s.rho[id] - 1.f) / 3.f * cpScale / A.cpRange);
        else {
            float w = 0;
            if (x > 0 && y > 0 && z > 0 && x < s.nx - 1 && y < s.ny - 1 && z < s.nz - 1) {
                float wx = (s.uz[s.id(x, y + 1, z)] - s.uz[s.id(x, y - 1, z)]) - (s.uy[s.id(x, y, z + 1)] - s.uy[s.id(x, y, z - 1)]);
                float wy = (s.ux[s.id(x, y, z + 1)] - s.ux[s.id(x, y, z - 1)]) - (s.uz[s.id(x + 1, y, z)] - s.uz[s.id(x - 1, y, z)]);
                float wz = (s.uy[s.id(x + 1, y, z)] - s.uy[s.id(x - 1, y, z)]) - (s.ux[s.id(x, y + 1, z)] - s.ux[s.id(x, y - 1, z)]);
                w = 0.5f * std::sqrt(wx * wx + wy * wy + wz * wz) * lenScale;
            }
            col = colormapJet(w / A.vortMax);
        }
        uint8_t* px = &A.sliceImg[((size_t)j * W + i) * 4];
        px[0] = (uint8_t)(255 * col.x); px[1] = (uint8_t)(255 * col.y); px[2] = (uint8_t)(255 * col.z); px[3] = 255;
    }
    glBindTexture(GL_TEXTURE_2D, A.sliceTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, A.sliceImg.data());
}

void renderViewport(int w, int h) {
    const Sim& s = A.sim;
    A.view.ensure(w, h);
    if (A.colorMode == Surface_Pressure && A.haveModel && (A.cpFrame++ % 3 == 0)) updateSurfaceColors();

    A.view.bind();
    glDepthMask(GL_TRUE);
    glClearColor(0.075f, 0.082f, 0.098f, 1.f); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    const mat4 V = A.cam.view();
    const mat4 P = glm::perspective(glm::radians(45.f), (float)w / (float)h, 0.5f, 5000.f);
    const mat4 MVP = P * V;

    // model
    if (A.mesh.count > 0) {
        glUseProgram(A.progMesh.id);
        glUniformMatrix4fv(A.progMesh.uMVP, 1, GL_FALSE, glm::value_ptr(MVP));
        glUniformMatrix4fv(A.progMesh.uView, 1, GL_FALSE, glm::value_ptr(V));
        glUniform3f(A.progMesh.uBase, 0.75f, 0.77f, 0.80f);
        glUniform1i(A.progMesh.uUseVC, A.colorMode == Surface_Pressure);
        glBindVertexArray(A.mesh.vao);
        glDrawElements(GL_TRIANGLES, A.mesh.count, GL_UNSIGNED_INT, nullptr);
    }

    // domain box
    if (A.showBox) {
        glUseProgram(A.progLine.id);
        glUniformMatrix4fv(A.progLine.uMVP, 1, GL_FALSE, glm::value_ptr(MVP));
        std::vector<LV> v;
        const vec4 edge(0.45f, 0.52f, 0.65f, 1.f), inlet(0.35f, 0.85f, 0.45f, 1.f);   // inlet face (x = 0) in green
        const vec3 d((float)s.nx, (float)s.ny, (float)s.nz);
        vec3 cor[8]; for (int i = 0; i < 8; ++i) cor[i] = vec3((i & 1) ? d.x : 0, (i & 2) ? d.y : 0, (i & 4) ? d.z : 0);
        const int e[12][2] = {{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
        for (auto& ed : e) {
            const vec4 c = ((ed[0] | ed[1]) & 1) ? edge : inlet;
            v.push_back({cor[ed[0]], c}); v.push_back({cor[ed[1]], c});
        }
        A.boxLines.draw(v, GL_LINES);
    }

    // slice
    if (A.showSlice) {
        updateSliceTexture();
        const float k = std::clamp(A.sliceIdx, 0, sliceCount() - 1) + 0.5f;
        const float X = (float)s.nx, Y = (float)s.ny, Z = (float)s.nz;
        vec3 q[4];
        if (A.sliceAxis == 0)      { q[0] = {k, 0, 0}; q[1] = {k, Y, 0}; q[2] = {k, Y, Z}; q[3] = {k, 0, Z}; }
        else if (A.sliceAxis == 1) { q[0] = {0, k, 0}; q[1] = {X, k, 0}; q[2] = {X, k, Z}; q[3] = {0, k, Z}; }
        else                       { q[0] = {0, 0, k}; q[1] = {X, 0, k}; q[2] = {X, Y, k}; q[3] = {0, Y, k}; }
        glUseProgram(A.progTex.id);
        glUniformMatrix4fv(A.progTex.uMVP, 1, GL_FALSE, glm::value_ptr(MVP));
        glUniform1f(A.progTex.uAlpha, A.sliceAlpha);
        glUniform1i(A.progTex.uTex, 0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, A.sliceTex);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        A.sliceQuad.draw(q);
        glDepthMask(GL_TRUE);
    }

    // particle trails
    if (A.part.on) {
        const Particles& pt = A.part;
        std::vector<LV> v; v.reserve((size_t)pt.P * (pt.T - 1) * 2);
        for (int p = 0; p < pt.P; ++p) {
            vec3 col = colormapJet(pt.spd[p] / A.speedMax);
            for (int k = 0; k < pt.T - 1; ++k) {
                vec3 a = pt.tr[(size_t)p * pt.T + k], b = pt.tr[(size_t)p * pt.T + k + 1];
                if (a == b) continue;
                float al = (1.f - (float)k / (pt.T - 1)) * pt.alpha;
                v.push_back({a, vec4(col, al)}); v.push_back({b, vec4(col, al * 0.8f)});
            }
        }
        glUseProgram(A.progLine.id);
        glUniformMatrix4fv(A.progLine.uMVP, 1, GL_FALSE, glm::value_ptr(MVP));
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        A.trails.draw(v, GL_LINES);
        glDepthMask(GL_TRUE);
    }
    glDisable(GL_BLEND);
    A.view.resolve();
}

// ============================================================================
//  Init
// ============================================================================
void initScene() {
    A.progMesh.init(); A.progLine.init(); A.progTex.init();
    A.trails.init(); A.boxLines.init(); A.sliceQuad.init();
    glGenTextures(1, &A.sliceTex); glBindTexture(GL_TEXTURE_2D, A.sliceTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    rebuildDomain();
    useBuiltin(Builtin_Sphere);
}
