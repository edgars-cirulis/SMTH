#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory_resource>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/core/FrameArena.hpp"
#include "engine/core/SmallFn.hpp"

class VulkanContext;

class RenderGraph {
   public:
    struct ImageHandle {
        uint32_t id = 0;
    };

    struct BufferHandle {
        uint32_t id = 0;
    };

    enum class PassType { Compute, Graphics };

    enum class ImageUse {
        ColorAttachment,
        DepthAttachment,
        Sampled,
        Present,
    };

    enum class BufferUse {
        Uniform,
        Storage,
        Indirect,
        Transfer,
    };

    struct ImageAccess {
        uint32_t id = 0;
        ImageUse use = ImageUse::Sampled;
        bool write = false;
    };

    struct BufferAccess {
        uint32_t id = 0;
        BufferUse use = BufferUse::Storage;
        bool write = false;
    };

    class PassBuilder;

    using SetupFn = SmallFn<void(PassBuilder&), 96>;
    using ExecFn = SmallFn<void(VkCommandBuffer), 96>;

    VkCommandBuffer begin(VulkanContext& vk);
    void addPass(std::string_view name, PassType type, SetupFn setup, ExecFn exec);
    void execute(VulkanContext& vk);
    void end(VulkanContext& vk);

    VkCommandBuffer cmd() const { return cmdBuf; }

    ImageHandle backbuffer() const { return backbufferHandle; }
    ImageHandle depth() const { return depthHandle; }

    BufferHandle importBuffer(VkBuffer buffer);
    ImageHandle createTransientImage2D(VulkanContext& vk,
                                       std::string_view name,
                                       VkFormat format,
                                       VkExtent2D extent,
                                       VkImageUsageFlags usage,
                                       VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
    BufferHandle createTransientBuffer(VulkanContext& vk,
                                       std::string_view name,
                                       VkDeviceSize size,
                                       VkBufferUsageFlags usage,
                                       VkMemoryPropertyFlags memFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

   private:
    struct Pass {
        std::pmr::string name;
        PassType type = PassType::Graphics;
        SetupFn setup;
        ExecFn exec;

        std::pmr::vector<ImageAccess> images;
        std::pmr::vector<BufferAccess> buffers;

        std::pmr::vector<uint32_t> colorAttachments;
        std::pmr::vector<VkAttachmentLoadOp> colorLoad;
        std::pmr::vector<VkAttachmentStoreOp> colorStore;
        std::pmr::vector<uint8_t> colorHasClear;
        std::pmr::vector<VkClearValue> colorClear;

        bool hasDepth = false;
        uint32_t depthAttachment = 0;
        VkAttachmentLoadOp depthLoad = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkAttachmentStoreOp depthStore = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        uint8_t depthHasClear = 0;
        VkClearValue depthClear{};
    };

   public:
    class PassBuilder {
       public:
        explicit PassBuilder(Pass& p) : pass(p) {}

        void readImage(ImageHandle h, ImageUse use);
        void writeImage(ImageHandle h, ImageUse use);

        void readBuffer(BufferHandle h, BufferUse use);
        void writeBuffer(BufferHandle h, BufferUse use);

        void colorAttachment(ImageHandle h,
                             VkAttachmentLoadOp loadOp,
                             VkAttachmentStoreOp storeOp,
                             const std::optional<VkClearValue>& clear = std::nullopt);

        void depthAttachment(ImageHandle h,
                             VkAttachmentLoadOp loadOp,
                             VkAttachmentStoreOp storeOp,
                             const std::optional<VkClearValue>& clear = std::nullopt);

       private:
        Pass& pass;
    };

   private:
    struct ImageResource {
        std::pmr::string name;
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
        VkImageAspectFlags aspectMask = 0;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout* externalLayoutPtr = nullptr;
        VkPipelineStageFlags2 lastStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        VkAccessFlags2 lastAccess = 0;
        bool owned = false;
        bool pooled = false;
    };

    struct BufferResource {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkPipelineStageFlags2 lastStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        VkAccessFlags2 lastAccess = 0;
        bool owned = false;
        bool pooled = false;
    };

    ImageHandle importBackbuffer(VulkanContext& vk);
    ImageHandle importDepth(VulkanContext& vk);

    void stagesAccessForImageUse(ImageUse use,
                                 bool write,
                                 VkPipelineStageFlags2& stage,
                                 VkAccessFlags2& access,
                                 VkImageLayout& layout,
                                 VkImageAspectFlags& aspect) const;

    void stagesAccessForBufferUse(BufferUse use, bool write, VkPipelineStageFlags2& stage, VkAccessFlags2& access) const;

    void applyBarriers(VulkanContext& vk, VkCommandBuffer cmd, const Pass& pass);

   private:
    VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
    FrameArena<> arena;
    std::pmr::vector<ImageResource> images;
    std::pmr::vector<BufferResource> buffers;
    std::pmr::vector<Pass> passes;

    std::vector<VkImageMemoryBarrier2> scratchImgBarriers;
    std::vector<VkBufferMemoryBarrier2> scratchBufBarriers;
    VkDependencyInfo scratchDep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    std::vector<VkRenderingAttachmentInfoKHR> scratchColorAtts;

    struct RetireImage2D {
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
        VkImageUsageFlags usage = 0;
        VkImageAspectFlags aspect = 0;
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    struct RetireBuffer {
        VkDeviceSize size = 0;
        VkBufferUsageFlags usage = 0;
        VkMemoryPropertyFlags memFlags = 0;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    std::vector<RetireImage2D> retireImages;
    std::vector<RetireBuffer> retireBuffers;

    ImageHandle backbufferHandle{};
    ImageHandle depthHandle{};
};
