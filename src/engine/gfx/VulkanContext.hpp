#pragma once
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/DeletionQueue.hpp"

class VulkanContext {
   public:
    void initWindow(int w, int h, const char* title);
    void initVulkan();
    void shutdown();

    bool shouldClose() const;
    void pollEvents() const;
    void deviceWaitIdle() const;

    GLFWwindow* window() const { return win; }

    VkInstance instance() const { return inst; }
    VkPhysicalDevice physicalDevice() const { return phys; }
    VkDevice device() const { return dev; }
    VkQueue graphicsQueue() const { return gfxQ; }
    uint32_t graphicsFamilyIndex() const { return gfxFamily; }
    VkSurfaceKHR surface() const { return surf; }

    VkSwapchainKHR swapchain() const { return swap; }
    VkFormat swapchainFormat() const { return swapFormat; }
    VkExtent2D swapchainExtent() const { return swapExtent; }
    const std::vector<VkImageView>& swapchainViews() const { return swapViews; }
    VkImageView depthView() const { return depthIv; }
    VkFormat depthFormat() const { return depthFmt; }

    VkImage currentSwapchainImage() const { return swapImages[acquiredImage]; }
    VkImageView currentSwapchainImageView() const { return swapViews[acquiredImage]; }
    VkImageLayout currentSwapchainImageLayout() const
    {
        return swapImageLayouts.empty() ? VK_IMAGE_LAYOUT_UNDEFINED : swapImageLayouts[acquiredImage];
    }
    VkImageLayout* currentSwapchainImageLayoutPtr() { return swapImageLayouts.empty() ? nullptr : &swapImageLayouts[acquiredImage]; }

    VkImage depthImage() const { return depthImg; }
    VkImageLayout depthImageLayout() const { return depthLayout; }
    VkImageLayout* depthImageLayoutPtr() { return &depthLayout; }

    void beginRendering(VkCommandBuffer cmd, const VkRenderingInfoKHR& info) const
    {
        if (pfnCmdBeginRendering)
            pfnCmdBeginRendering(cmd, &info);
    }
    void endRendering(VkCommandBuffer cmd) const
    {
        if (pfnCmdEndRendering)
            pfnCmdEndRendering(cmd);
    }

    VkRenderPass renderPass() const { return rp; }
    bool dynamicRenderingEnabled() const { return useDynamicRendering; }

    void cmdBeginLabel(VkCommandBuffer cmd, const char* name) const;
    void cmdEndLabel(VkCommandBuffer cmd) const;
    void setObjectName(VkObjectType type, uint64_t handle, const char* name) const;

    void cmdPipelineBarrier2(VkCommandBuffer cmd, const VkDependencyInfo& dep) const;

    struct TransientImage2D {
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
        VkImageUsageFlags usage = 0;
        VkImageAspectFlags aspect = 0;
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    struct TransientBuffer {
        VkDeviceSize size = 0;
        VkBufferUsageFlags usage = 0;
        VkMemoryPropertyFlags memFlags = 0;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    TransientImage2D acquireTransientImage2D(std::string_view debugName,
                                             VkFormat format,
                                             VkExtent2D extent,
                                             VkImageUsageFlags usage,
                                             VkImageAspectFlags aspect);
    void retireTransientImage2D(TransientImage2D img);

    TransientBuffer acquireTransientBuffer(std::string_view debugName,
                                           VkDeviceSize size,
                                           VkBufferUsageFlags usage,
                                           VkMemoryPropertyFlags memFlags);
    void retireTransientBuffer(TransientBuffer buf);

    uint32_t currentFrameIndex() const { return frameIndex; }
    uint32_t imageIndex() const { return acquiredImage; }

    VkCommandBuffer beginFrame();
    void cmdDrawIndexedIndirectCount(VkCommandBuffer cmd,
                                     VkBuffer indirectBuffer,
                                     VkDeviceSize indirectOffset,
                                     VkBuffer countBuffer,
                                     VkDeviceSize countOffset,
                                     uint32_t maxDrawCount,
                                     uint32_t stride) const;
    void beginMainPass(VkCommandBuffer cmd);
    void endMainPass(VkCommandBuffer cmd);
    void endFrame();

    DeletionQueue& frameDeletionQueue() { return frameDeletion[frameIndex]; }
    DeletionQueue& deviceDeletionQueue() { return deviceDeletion; }

    void requestSwapchainRebuild();
    void recreateSwapchain();
    bool swapchainRebuildRequested() const { return framebufferResized; }
    uint64_t swapchainGeneration() const { return swapchainGen; }

   private:
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createDevice();
    void createSwapchain();
    void createDepthResources();
    void createRenderPass();
    void createFramebuffers();
    void createCommands();
    void createSync();

    void cleanupSwapchain();

    void createOrResizeSwapchain();

    void loadDeviceFunctionPointers();

   private:
    GLFWwindow* win = nullptr;

    VkInstance inst{};
    VkSurfaceKHR surf{};
    VkPhysicalDevice phys{};
    VkDevice dev{};

    uint32_t gfxFamily = 0;
    VkQueue gfxQ{};

    VkSwapchainKHR swap{};
    VkFormat swapFormat{};
    VkExtent2D swapExtent{};
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;

    VkFormat depthFmt = VK_FORMAT_UNDEFINED;
    VkImage depthImg{};
    VkDeviceMemory depthMem{};
    VkImageView depthIv{};

    VkRenderPass rp{};
    std::vector<VkFramebuffer> framebuffers;

    bool useDynamicRendering = false;
    PFN_vkCmdBeginRenderingKHR pfnCmdBeginRendering = nullptr;
    PFN_vkCmdEndRenderingKHR pfnCmdEndRendering = nullptr;

    PFN_vkCmdDrawIndexedIndirectCount pfnCmdDrawIndexedIndirectCount = nullptr;
    PFN_vkCmdDrawIndexedIndirectCountKHR pfnCmdDrawIndexedIndirectCountKHR = nullptr;

    bool useSync2 = false;
    PFN_vkCmdPipelineBarrier2 pfnCmdPipelineBarrier2 = nullptr;
    PFN_vkCmdPipelineBarrier2KHR pfnCmdPipelineBarrier2KHR = nullptr;

    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    PFN_vkCreateDebugUtilsMessengerEXT pfnCreateDebugUtilsMessenger = nullptr;
    PFN_vkDestroyDebugUtilsMessengerEXT pfnDestroyDebugUtilsMessenger = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT pfnCmdBeginLabel = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT pfnCmdEndLabel = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT pfnSetObjectName = nullptr;

    bool validationEnabled = false;

    VkCommandPool cmdPool{};
    std::vector<VkCommandBuffer> cmdBuffers;

    static constexpr uint32_t MAX_FRAMES = 2;
    uint32_t frameIndex = 0;
    uint32_t acquiredImage = 0;

    VkSemaphore imageAvailable[MAX_FRAMES]{};
    VkSemaphore renderFinished[MAX_FRAMES]{};
    VkFence inFlight[MAX_FRAMES]{};

    DeletionQueue frameDeletion[MAX_FRAMES]{};
    DeletionQueue deviceDeletion{};

    std::vector<TransientImage2D> transientImagesFree;
    std::vector<TransientBuffer> transientBuffersFree;
    std::vector<TransientImage2D> transientImagesInFlight[MAX_FRAMES]{};
    std::vector<TransientBuffer> transientBuffersInFlight[MAX_FRAMES]{};

    bool framebufferResized = false;
    uint64_t swapchainGen = 0;

    std::vector<VkImageLayout> swapImageLayouts;
    VkImageLayout depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};
