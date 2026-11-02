// ============================================================================
//  Triangle meshes: file import (.fbx / .obj via ufbx, .stl built-in) and
//  procedural built-in shapes.
// ============================================================================
#pragma once
#include <glm/glm.hpp>

#include <string>
#include <vector>

struct Model {
    std::vector<glm::vec3> p0, n0;      // normalised: centred, longest bbox side = 1
    std::vector<unsigned> idx;
    std::string name;
};

enum BuiltinShape { Builtin_Sphere, Builtin_Wing, Builtin_Count };
extern const char* const BUILTIN_NAMES[Builtin_Count];

// Path is UTF-8. On failure returns false and fills err.
bool loadModelFile(const std::string& path, Model& out, std::string& err);
Model makeBuiltin(BuiltinShape shape);

// UTF-8 <-> UTF-16 helpers for Win32 calls.
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
