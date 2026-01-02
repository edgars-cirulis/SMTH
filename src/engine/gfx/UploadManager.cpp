#include "UploadManager.hpp"

#include "VulkanHelpers.hpp"

#include <cstring>

VkDeviceSize UploadManager::alignUp(VkDeviceSize v, VkDeviceSize a)
{
    return (v + a - 1) & ~(a - 1);
}

void UploadManager::init(VulkanContext& vk, VkDeviceSize perFrameBytes)
{
    VkDevice dev = vk.device();
    VkPhysicalDevice phys = vk.physicalDevice();

    frames.clear();
    frames.resize(2);

    for (auto& f : frames) {
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = vk.graphicsFamilyIndex();
        vkCheck(vkCreateCommandPool(dev, &pci, nullptr, &f.pool), "vkCreateCommandPool(upload)");

        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        vkCheck(vkAllocateCommandBuffers(dev, &ai, &f.cmd), "vkAllocateCommandBuffers(upload)");

        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCheck(vkCreateFence(dev, &fci, nullptr, &f.fence), "vkCreateFence(upload)");

        f.capacity = perFrameBytes;
        createBuffer(dev, phys, f.capacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, f.staging, f.stagingMem,
                     "vkCreateBuffer(upload staging)");

        f.mapped = reinterpret_cast<uint8_t*>(mapMemory(dev, f.stagingMem, f.capacity));
    }
}

void UploadManager::shutdown(VulkanContext& vk)
{
    VkDevice dev = vk.device();
    for (auto& f : frames) {
        if (f.mapped) {
            vkUnmapMemory(dev, f.stagingMem);
            f.mapped = nullptr;
        }
        if (f.staging)
            vkDestroyBuffer(dev, f.staging, nullptr);
        if (f.stagingMem)
            vkFreeMemory(dev, f.stagingMem, nullptr);
        if (f.fence)
            vkDestroyFence(dev, f.fence, nullptr);
        if (f.pool)
            vkDestroyCommandPool(dev, f.pool, nullptr);
        f = {};
    }
    frames.clear();
    cur = nullptr;
    currentCmd = VK_NULL_HANDLE;
}

void UploadManager::beginFrame(VulkanContext& vk)
{
    if (frames.empty())
        return;
    Frame& f = frames[vk.currentFrameIndex() % frames.size()];
    cur = &f;
    currentCmd = f.cmd;

    VkDevice dev = vk.device();
    vkCheck(vkWaitForFences(dev, 1, &f.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(upload)");
    vkCheck(vkResetFences(dev, 1, &f.fence), "vkResetFences(upload)");
    vkResetCommandPool(dev, f.pool, 0);
    f.head = 0;
    f.recorded = false;

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(f.cmd, &bi), "vkBeginCommandBuffer(upload)");
}

void UploadManager::endFrame(VulkanContext& vk)
{
    if (!cur)
        return;
    Frame& f = *cur;

    vkCheck(vkEndCommandBuffer(f.cmd), "vkEndCommandBuffer(upload)");

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    if (f.recorded) {
        si.commandBufferCount = 1;
        si.pCommandBuffers = &f.cmd;
    }

    vkCheck(vkQueueSubmit(vk.graphicsQueue(), 1, &si, f.fence), "vkQueueSubmit(upload)");

    cur = nullptr;
    currentCmd = VK_NULL_HANDLE;
}

UploadManager::Allocation UploadManager::alloc(VkDeviceSize size, VkDeviceSize alignment)
{
    Allocation a{};
    if (!cur)
        return a;

    VkDeviceSize off = alignUp(cur->head, alignment);
    if (off + size > cur->capacity)
        return a;

    a.cpu = cur->mapped + off;
    a.srcOffset = off;
    a.size = size;
    cur->head = off + size;
    return a;
}

void UploadManager::copyToBuffer(VkBuffer dst, VkDeviceSize dstOffset, VkDeviceSize srcOffset, VkDeviceSize size)
{
    if (!cur)
        return;
    VkBufferCopy c{ srcOffset, dstOffset, size };
    vkCmdCopyBuffer(cur->cmd, cur->staging, dst, 1, &c);
    cur->recorded = true;
}

bool UploadManager::uploadToBuffer(VkBuffer dst, VkDeviceSize dstOffset, const void* data, VkDeviceSize size, VkDeviceSize alignment)
{
    Allocation a = alloc(size, alignment);
    if (!a.cpu)
        return false;
    std::memcpy(a.cpu, data, static_cast<size_t>(size));
    copyToBuffer(dst, dstOffset, a.srcOffset, size);
    return true;
}
