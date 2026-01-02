#include "Mesh.hpp"

#include "VulkanHelpers.hpp"

bool Mesh::create(VulkanContext& vk, UploadManager& up, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    destroy(vk);
    if (vertices.empty() || indices.empty())
        return false;

    bmin = vertices[0].pos;
    bmax = vertices[0].pos;
    for (const auto& v : vertices) {
        bmin = glm::min(bmin, v.pos);
        bmax = glm::max(bmax, v.pos);
    }

    const VkDevice dev = vk.device();
    const VkPhysicalDevice phys = vk.physicalDevice();

    const VkDeviceSize vbytes = sizeof(Vertex) * static_cast<VkDeviceSize>(vertices.size());
    const VkDeviceSize ibytes = sizeof(uint32_t) * static_cast<VkDeviceSize>(indices.size());

    createBuffer(dev, phys, vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vb, vbMem, "vkCreateBuffer(mesh vb)");

    createBuffer(dev, phys, ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, ib, ibMem, "vkCreateBuffer(mesh ib)");

    if (!up.uploadToBuffer(vb, 0, vertices.data(), vbytes, alignof(Vertex)))
        return false;
    if (!up.uploadToBuffer(ib, 0, indices.data(), ibytes, alignof(uint32_t)))
        return false;

    idxCount = static_cast<uint32_t>(indices.size());
    return true;
}

void Mesh::destroy(VulkanContext& vk)
{
    const VkDevice dev = vk.device();
    const VkBuffer oldVb = vb;
    const VkDeviceMemory oldVbMem = vbMem;
    const VkBuffer oldIb = ib;
    const VkDeviceMemory oldIbMem = ibMem;

    if (oldVb || oldVbMem || oldIb || oldIbMem) {
        vk.frameDeletionQueue().push([dev, oldVb, oldVbMem, oldIb, oldIbMem]() {
            if (oldVb)
                vkDestroyBuffer(dev, oldVb, nullptr);
            if (oldVbMem)
                vkFreeMemory(dev, oldVbMem, nullptr);
            if (oldIb)
                vkDestroyBuffer(dev, oldIb, nullptr);
            if (oldIbMem)
                vkFreeMemory(dev, oldIbMem, nullptr);
        });
    }

    vb = {};
    vbMem = {};
    ib = {};
    ibMem = {};
    idxCount = 0;
}
