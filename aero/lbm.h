// ============================================================================
//  D3Q19 lattice-Boltzmann solver (BGK + Smagorinsky LES), lattice units.
//  Flow enters at x = 0 and leaves at x = nx-1; the other faces are far-field.
// ============================================================================
#pragma once
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

struct Smp { float rho = 1.f; glm::vec3 u{0.f}; bool ok = false; };

struct Sim {
    int nx = 0, ny = 0, nz = 0;
    size_t N = 0;
    std::vector<float> f, ft, rho, ux, uy, uz;   // f/ft are SoA: f[q * N + cell]
    std::vector<uint8_t> solid;
    float uLB = 0.08f, tau0 = 0.55f, smagC = 0.12f;
    double Fs[3] = {0, 0, 0};          // smoothed force on the solid (lattice units)
    long long steps = 0;
    bool diverged = false;

    size_t id(int x, int y, int z) const { return (size_t)x + (size_t)nx * ((size_t)y + (size_t)ny * (size_t)z); }

    void alloc(int X, int Y, int Z);
    void resetFlow(float u);
    bool isSolid(const glm::vec3& p) const;

    // Trilinear sample at cell-centre convention (cell i centre = i + 0.5).
    // mask=true : solid cells are excluded and weights renormalised (for pressure)
    // mask=false: solid cells count as zero velocity (for particle advection)
    Smp sample(const glm::vec3& p, bool mask) const;

    // One fused pull-stream + collide step. Interior only; domain faces are boundary cells.
    void step();

    // Domain lengths travelled by the free stream since the last reset.
    double flowThroughs() const { return nx > 0 ? steps * (double)uLB / nx : 0.0; }
};
