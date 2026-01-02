#pragma once
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

struct RenderCameraData {
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
    glm::vec3 position{ 0.0f };
    float _pad0 = 0.0f;

    glm::vec3 forward{ 0.0f, 0.0f, -1.0f };
    float fovRadians = 1.0f;
    glm::vec3 right{ 1.0f, 0.0f, 0.0f };
    float aspect = 1.0f;
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };
    float _pad1 = 0.0f;
};

struct DrawItem {
    uint32_t meshId = 0;
    uint32_t materialId = 0;
    uint32_t transformIndex = 0;
    uint32_t _pad0 = 0;

    glm::vec4 baseColorFactor{ 1.0f };
    glm::vec2 metallicRoughnessFactor{ 1.0f, 1.0f };
    glm::vec2 _pad1{ 0.0f };
};

struct DirectionalLight {
    glm::vec3 direction{ 0.3f, 0.8f, 0.2f };
    float intensity = 6.0f;
    glm::vec3 color{ 1.0f, 0.98f, 0.92f };
    float _pad0 = 0.0f;
};

struct RenderScene {
    RenderCameraData camera{};
    DirectionalLight sun{};
    float exposure = 1.0f;
    float timeSeconds = 0.0f;
    std::vector<glm::mat4> transforms;
    std::vector<DrawItem> draws;

    void clear()
    {
        transforms.clear();
        draws.clear();
    }
};
