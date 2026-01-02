#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace ShaderLayout {

static constexpr uint32_t SET_FRAME = 0;
static constexpr uint32_t SET_MATERIAL = 1;

static constexpr uint32_t BIND_CAMERA = 0;
static constexpr uint32_t BIND_LIGHT = 1;
static constexpr uint32_t BIND_TRANSFORMS = 2;

static constexpr uint32_t BIND_BASE_COLOR = 0;
static constexpr uint32_t BIND_NORMAL = 1;
static constexpr uint32_t BIND_METAL_ROUGH = 2;
static constexpr uint32_t BIND_MATERIAL = 3;

struct CameraUBO {
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
    glm::vec3 camPos{ 0.0f };
    float _pad0 = 0.0f;
};

struct LightUBO {
    glm::vec3 lightDir{ 0.0f, 1.0f, 0.0f };
    float lightIntensity = 1.0f;
    glm::vec3 lightColor{ 1.0f };
    float exposure = 1.0f;
};

struct MaterialUBO {
    glm::vec4 baseColorFactor{ 1.0f };
    glm::vec2 metallicRoughnessFactor{ 1.0f, 1.0f };
    glm::vec2 _pad0{ 0.0f };
};

struct SkyPC {
    glm::mat4 invViewProj{ 1.0f };
};

}  // namespace ShaderLayout
