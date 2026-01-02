#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct ObjVertex {
    glm::vec3 pos{};
    glm::vec3 nrm{ 0.0f, 1.0f, 0.0f };
    glm::vec2 uv{};
};

struct ObjMeshData {
    std::vector<ObjVertex> vertices;
    std::vector<uint32_t> indices;
};

bool loadObj(const std::string& path, ObjMeshData& out, std::string& error);
