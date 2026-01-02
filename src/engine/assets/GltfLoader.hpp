
#pragma once
#include "engine/gfx/Mesh.hpp"

#include <glm/glm.hpp>
#include <string>
#include <vector>

struct GltfMaterialData {
    glm::vec4 baseColorFactor{ 1.0f };
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;

    std::string baseColorUri;
    std::string normalUri;
    std::string metallicRoughnessUri;
};

struct GltfSceneData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    GltfMaterialData material;
};

bool loadGltfScene(const std::string& path, GltfSceneData& out, std::string& err);
