#pragma once

#include "UploadManager.hpp"
#include "VulkanContext.hpp"

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

struct Vertex {
    glm::vec3 pos{};
    glm::vec3 nrm{ 0.0f, 1.0f, 0.0f };
    glm::vec2 uv{};

    glm::vec4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
};

class Mesh {
   public:
    void destroy(VulkanContext& vk);

    bool create(VulkanContext& vk, UploadManager& up, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    VkBuffer vertexBuffer() const { return vb; }
    VkBuffer indexBuffer() const { return ib; }
    uint32_t indexCount() const { return idxCount; }

    glm::vec3 boundsMin() const { return bmin; }
    glm::vec3 boundsMax() const { return bmax; }

   private:
    VkBuffer vb{};
    VkDeviceMemory vbMem{};
    VkBuffer ib{};
    VkDeviceMemory ibMem{};
    uint32_t idxCount = 0;
    glm::vec3 bmin{ 0.0f };
    glm::vec3 bmax{ 0.0f };
};
