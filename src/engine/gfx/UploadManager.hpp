#pragma once

#include "VulkanContext.hpp"

#include <cstdint>
#include <vector>

class UploadManager {
   public:
    struct Allocation {
        void* cpu = nullptr;
        VkDeviceSize srcOffset = 0;
        VkDeviceSize size = 0;
    };

    void init(VulkanContext& vk, VkDeviceSize perFrameBytes = 64ull * 1024ull * 1024ull);
    void shutdown(VulkanContext& vk);

    void beginFrame(VulkanContext& vk);
    void endFrame(VulkanContext& vk);

    Allocation alloc(VkDeviceSize size, VkDeviceSize alignment = 16);
    void copyToBuffer(VkBuffer dst, VkDeviceSize dstOffset, VkDeviceSize srcOffset, VkDeviceSize size);
    bool uploadToBuffer(VkBuffer dst, VkDeviceSize dstOffset, const void* data, VkDeviceSize size, VkDeviceSize alignment = 16);

    VkCommandBuffer cmd() const { return currentCmd; }
    VkBuffer stagingBuffer() const { return cur ? cur->staging : VK_NULL_HANDLE; }

   private:
    struct Frame {
        VkCommandPool pool{};
        VkCommandBuffer cmd{};
        VkFence fence{};

        VkBuffer staging{};
        VkDeviceMemory stagingMem{};
        uint8_t* mapped = nullptr;

        VkDeviceSize capacity = 0;
        VkDeviceSize head = 0;
        bool recorded = false;
    };

    static VkDeviceSize alignUp(VkDeviceSize v, VkDeviceSize a);

    std::vector<Frame> frames;
    Frame* cur = nullptr;
    VkCommandBuffer currentCmd = VK_NULL_HANDLE;
};
