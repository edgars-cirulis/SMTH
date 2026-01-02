#include "engine/gfx/RenderGraph.hpp"

#include "engine/gfx/VulkanContext.hpp"
#include "engine/gfx/VulkanHelpers.hpp"

#include <algorithm>

template <typename Vec>
static void addUniqueImageAccess(Vec& v, uint32_t id, RenderGraph::ImageUse use, bool write)

{
    for (auto& a : v) {
        if (a.id == id && a.use == use) {
            a.write = a.write || write;
            return;
        }
    }
    v.push_back(RenderGraph::ImageAccess{ id, use, write });
}

template <typename Vec>
static void addUniqueBufferAccess(Vec& v, uint32_t id, RenderGraph::BufferUse use, bool write)
{
    for (auto& a : v) {
        if (a.id == id && a.use == use) {
            a.write = a.write || write;
            return;
        }
    }
    v.push_back(RenderGraph::BufferAccess{ id, use, write });
}

void RenderGraph::PassBuilder::readImage(ImageHandle h, ImageUse use)
{
    addUniqueImageAccess(pass.images, h.id, use, false);
}
void RenderGraph::PassBuilder::writeImage(ImageHandle h, ImageUse use)
{
    addUniqueImageAccess(pass.images, h.id, use, true);
}
void RenderGraph::PassBuilder::readBuffer(BufferHandle h, BufferUse use)
{
    addUniqueBufferAccess(pass.buffers, h.id, use, false);
}
void RenderGraph::PassBuilder::writeBuffer(BufferHandle h, BufferUse use)
{
    addUniqueBufferAccess(pass.buffers, h.id, use, true);
}

void RenderGraph::PassBuilder::colorAttachment(ImageHandle h,
                                               VkAttachmentLoadOp loadOp,
                                               VkAttachmentStoreOp storeOp,
                                               const std::optional<VkClearValue>& clear)
{
    pass.type = PassType::Graphics;
    writeImage(h, ImageUse::ColorAttachment);
    pass.colorAttachments.push_back(h.id);
    pass.colorLoad.push_back(loadOp);
    pass.colorStore.push_back(storeOp);
    if (clear) {
        pass.colorHasClear.push_back(1);
        pass.colorClear.push_back(*clear);
    } else {
        pass.colorHasClear.push_back(0);
        pass.colorClear.push_back(VkClearValue{});
    }
}

void RenderGraph::PassBuilder::depthAttachment(ImageHandle h,
                                               VkAttachmentLoadOp loadOp,
                                               VkAttachmentStoreOp storeOp,
                                               const std::optional<VkClearValue>& clear)
{
    pass.type = PassType::Graphics;
    writeImage(h, ImageUse::DepthAttachment);
    pass.hasDepth = true;
    pass.depthAttachment = h.id;
    pass.depthLoad = loadOp;
    pass.depthStore = storeOp;
    if (clear) {
        pass.depthHasClear = 1;
        pass.depthClear = *clear;
    } else {
        pass.depthHasClear = 0;
        pass.depthClear = VkClearValue{};
    }
}

VkCommandBuffer RenderGraph::begin(VulkanContext& vk)
{
    passes.clear();
    images.clear();
    buffers.clear();
    retireImages.clear();
    retireBuffers.clear();

    arena.reset();
    images = std::pmr::vector<ImageResource>(arena.resource());
    buffers = std::pmr::vector<BufferResource>(arena.resource());
    passes = std::pmr::vector<Pass>(arena.resource());

    if (passes.capacity() < 16)
        passes.reserve(16);
    if (images.capacity() < 8)
        images.reserve(8);
    if (buffers.capacity() < 32)
        buffers.reserve(32);

    cmdBuf = vk.beginFrame();
    if (!cmdBuf)
        return VK_NULL_HANDLE;

    backbufferHandle = importBackbuffer(vk);
    depthHandle = importDepth(vk);
    return cmdBuf;
}

RenderGraph::BufferHandle RenderGraph::importBuffer(VkBuffer buffer)
{
    BufferResource r{};
    r.buffer = buffer;
    r.lastStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    r.lastAccess = 0;
    const uint32_t id = (uint32_t)buffers.size();
    buffers.push_back(r);
    return BufferHandle{ id };
}

void RenderGraph::addPass(std::string_view name, PassType type, SetupFn setup, ExecFn exec)
{
    Pass p;
    p.name = std::pmr::string(name, arena.resource());
    p.type = type;
    p.setup = std::move(setup);
    p.exec = std::move(exec);

    p.images = std::pmr::vector<ImageAccess>(arena.resource());
    p.buffers = std::pmr::vector<BufferAccess>(arena.resource());
    p.colorAttachments = std::pmr::vector<uint32_t>(arena.resource());
    p.colorLoad = std::pmr::vector<VkAttachmentLoadOp>(arena.resource());
    p.colorStore = std::pmr::vector<VkAttachmentStoreOp>(arena.resource());
    p.colorHasClear = std::pmr::vector<uint8_t>(arena.resource());
    p.colorClear = std::pmr::vector<VkClearValue>(arena.resource());

    PassBuilder b(p);
    if (p.setup)
        p.setup(b);

    passes.push_back(std::move(p));
}

static VkClearValue defaultColorClear()
{
    VkClearValue c{};
    c.color = { { 0.05f, 0.06f, 0.08f, 1.0f } };
    return c;
}

static VkClearValue defaultDepthClear()
{
    VkClearValue d{};
    d.depthStencil = { 1.0f, 0 };
    return d;
}

void RenderGraph::stagesAccessForImageUse(ImageUse use,
                                          bool write,
                                          VkPipelineStageFlags2& stage,
                                          VkAccessFlags2& access,
                                          VkImageLayout& layout,
                                          VkImageAspectFlags& aspect) const
{
    stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    access = 0;
    layout = VK_IMAGE_LAYOUT_UNDEFINED;
    aspect = VK_IMAGE_ASPECT_COLOR_BIT;

    switch (use) {
        case ImageUse::ColorAttachment:
            stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            access = write ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
            layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            break;
        case ImageUse::DepthAttachment:
            stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
            access = write ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            break;
        case ImageUse::Sampled:
            stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;
            layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            break;
        case ImageUse::Present:
            stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
            access = 0;
            layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            break;
    }
}

void RenderGraph::stagesAccessForBufferUse(BufferUse use, bool write, VkPipelineStageFlags2& stage, VkAccessFlags2& access) const
{
    stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    access = 0;

    switch (use) {
        case BufferUse::Uniform:
            stage = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            access = VK_ACCESS_2_UNIFORM_READ_BIT;
            break;
        case BufferUse::Storage:
            stage =
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            access = write ? (VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT) : VK_ACCESS_2_SHADER_READ_BIT;
            break;
        case BufferUse::Indirect:
            stage = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
            access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
            break;
        case BufferUse::Transfer:
            stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            access = write ? VK_ACCESS_2_TRANSFER_WRITE_BIT : VK_ACCESS_2_TRANSFER_READ_BIT;
            break;
    }
}

void RenderGraph::applyBarriers(VulkanContext& vk, VkCommandBuffer cmd, const Pass& pass)
{
    scratchImgBarriers.clear();
    scratchBufBarriers.clear();
    if (scratchImgBarriers.capacity() < 32)
        scratchImgBarriers.reserve(32);
    if (scratchBufBarriers.capacity() < 32)
        scratchBufBarriers.reserve(32);

    scratchDep = VkDependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };

    for (const auto& ia : pass.images) {
        if (ia.id >= images.size())
            continue;
        auto& res = images[ia.id];
        VkImageLayout& curLayout = res.externalLayoutPtr ? *res.externalLayoutPtr : res.layout;

        VkPipelineStageFlags2 dstStage{};
        VkAccessFlags2 dstAccess{};
        VkImageLayout desiredLayout{};
        VkImageAspectFlags aspect{};
        stagesAccessForImageUse(ia.use, ia.write, dstStage, dstAccess, desiredLayout, aspect);

        VkImageLayout oldLayout = curLayout;

        const VkAccessFlags2 writeBits = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                                         VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT |
                                         VK_ACCESS_2_MEMORY_WRITE_BIT;

        const bool needsMemoryBarrier = (res.lastAccess & writeBits) || (dstAccess & writeBits);

        const bool sameLayout = (oldLayout == desiredLayout);

        if (sameLayout && !needsMemoryBarrier) {
            res.lastStage = dstStage;
            res.lastAccess = dstAccess;
            res.aspectMask = aspect;
            continue;
        }

        VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        b.srcStageMask = res.lastStage;
        b.srcAccessMask = res.lastAccess;
        b.dstStageMask = dstStage;
        b.dstAccessMask = dstAccess;
        b.oldLayout = oldLayout;
        b.newLayout = desiredLayout;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = res.image;
        b.subresourceRange.aspectMask = aspect;
        b.subresourceRange.baseMipLevel = 0;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.baseArrayLayer = 0;
        b.subresourceRange.layerCount = 1;

        scratchImgBarriers.push_back(b);

        curLayout = desiredLayout;
        res.lastStage = dstStage;
        res.lastAccess = dstAccess;
        res.aspectMask = aspect;
    }

    for (const auto& ba : pass.buffers) {
        if (ba.id >= buffers.size())
            continue;
        auto& res = buffers[ba.id];

        VkPipelineStageFlags2 dstStage{};
        VkAccessFlags2 dstAccess{};
        stagesAccessForBufferUse(ba.use, ba.write, dstStage, dstAccess);

        if (res.lastAccess == 0) {
            res.lastStage = dstStage;
            res.lastAccess = dstAccess;
            continue;
        }

        const VkAccessFlags2 writeBits =
            VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;

        const bool needsMemoryBarrier = (res.lastAccess & writeBits) || (dstAccess & writeBits);

        if (!needsMemoryBarrier && res.lastStage == dstStage && res.lastAccess == dstAccess)
            continue;

        VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
        b.srcStageMask = res.lastStage;
        b.srcAccessMask = res.lastAccess;
        b.dstStageMask = dstStage;
        b.dstAccessMask = dstAccess;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = res.buffer;
        b.offset = 0;
        b.size = VK_WHOLE_SIZE;

        scratchBufBarriers.push_back(b);

        res.lastStage = dstStage;
        res.lastAccess = dstAccess;
    }

    if (scratchImgBarriers.empty() && scratchBufBarriers.empty())
        return;

    scratchDep.imageMemoryBarrierCount = (uint32_t)scratchImgBarriers.size();
    scratchDep.pImageMemoryBarriers = scratchImgBarriers.data();
    scratchDep.bufferMemoryBarrierCount = (uint32_t)scratchBufBarriers.size();
    scratchDep.pBufferMemoryBarriers = scratchBufBarriers.data();

    vk.cmdPipelineBarrier2(cmd, scratchDep);
}

void RenderGraph::execute(VulkanContext& vk)
{
    if (!cmdBuf)
        return;

    const bool dyn = vk.dynamicRenderingEnabled();

    bool legacyRenderPassOpen = false;
    for (size_t i = 0; i < passes.size(); ++i) {
        const Pass& p = passes[i];

        vk.cmdBeginLabel(cmdBuf, p.name.c_str());

        applyBarriers(vk, cmdBuf, p);

        if (p.type == PassType::Graphics) {
            if (dyn) {
                scratchColorAtts.clear();
                scratchColorAtts.reserve(p.colorAttachments.size());

                for (size_t ci = 0; ci < p.colorAttachments.size(); ++ci) {
                    const uint32_t imgId = p.colorAttachments[ci];
                    const auto& img = images[imgId];

                    VkRenderingAttachmentInfoKHR a{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
                    a.imageView = img.view;
                    a.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    a.loadOp = p.colorLoad[ci];
                    a.storeOp = p.colorStore[ci];
                    if (p.colorHasClear[ci]) {
                        a.clearValue = p.colorClear[ci];
                    } else if (a.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
                        a.clearValue = defaultColorClear();
                    }
                    scratchColorAtts.push_back(a);
                }

                VkRenderingAttachmentInfoKHR depthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
                const VkRenderingAttachmentInfoKHR* depthPtr = nullptr;
                if (p.hasDepth) {
                    const auto& img = images[p.depthAttachment];
                    depthAtt.imageView = img.view;
                    depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                    depthAtt.loadOp = p.depthLoad;
                    depthAtt.storeOp = p.depthStore;
                    if (p.depthHasClear) {
                        depthAtt.clearValue = p.depthClear;
                    } else if (depthAtt.loadOp == VK_ATTACHMENT_LOAD_OP_CLEAR) {
                        depthAtt.clearValue = defaultDepthClear();
                    }
                    depthPtr = &depthAtt;
                }

                VkRenderingInfoKHR ri{ VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
                ri.renderArea = VkRect2D{ { 0, 0 }, vk.swapchainExtent() };
                ri.layerCount = 1;
                ri.colorAttachmentCount = (uint32_t)scratchColorAtts.size();
                ri.pColorAttachments = scratchColorAtts.empty() ? nullptr : scratchColorAtts.data();
                ri.pDepthAttachment = depthPtr;

                vk.beginRendering(cmdBuf, ri);
                if (p.exec)
                    p.exec(cmdBuf);
                vk.endRendering(cmdBuf);
                vk.cmdEndLabel(cmdBuf);
            } else {
                if (!legacyRenderPassOpen) {
                    vk.beginMainPass(cmdBuf);
                    legacyRenderPassOpen = true;
                }
                if (p.exec)
                    p.exec(cmdBuf);

                const bool nextIsGraphics = (i + 1 < passes.size()) && (passes[i + 1].type == PassType::Graphics);
                if (!nextIsGraphics) {
                    vk.endMainPass(cmdBuf);
                    legacyRenderPassOpen = false;
                }
                vk.cmdEndLabel(cmdBuf);
            }
        } else {
            if (p.exec)
                p.exec(cmdBuf);
            vk.cmdEndLabel(cmdBuf);
        }
    }
}

void RenderGraph::end(VulkanContext& vk)
{
    if (!cmdBuf)
        return;

    if (backbufferHandle.id < images.size()) {
        Pass p;
        p.images = std::pmr::vector<ImageAccess>(arena.resource());
        p.buffers = std::pmr::vector<BufferAccess>(arena.resource());
        p.colorAttachments = std::pmr::vector<uint32_t>(arena.resource());
        p.colorLoad = std::pmr::vector<VkAttachmentLoadOp>(arena.resource());
        p.colorStore = std::pmr::vector<VkAttachmentStoreOp>(arena.resource());
        p.colorHasClear = std::pmr::vector<uint8_t>(arena.resource());
        p.colorClear = std::pmr::vector<VkClearValue>(arena.resource());
        p.type = PassType::Graphics;
        p.images.push_back(ImageAccess{ backbufferHandle.id, ImageUse::Present, false });
        applyBarriers(vk, cmdBuf, p);
    }

    for (auto& r : images) {
        if (!r.owned)
            continue;
        VkDevice dev = vk.device();
        VkImage img = r.image;
        VkImageView view = r.view;
        VkDeviceMemory mem = r.memory;
        vk.frameDeletionQueue().push([dev, img, view, mem]() {
            if (view)
                vkDestroyImageView(dev, view, nullptr);
            if (img)
                vkDestroyImage(dev, img, nullptr);
            if (mem)
                vkFreeMemory(dev, mem, nullptr);
        });
    }
    for (auto& r : buffers) {
        if (!r.owned)
            continue;
        VkDevice dev = vk.device();
        VkBuffer buf = r.buffer;
        VkDeviceMemory mem = r.memory;
        vk.frameDeletionQueue().push([dev, buf, mem]() {
            if (buf)
                vkDestroyBuffer(dev, buf, nullptr);
            if (mem)
                vkFreeMemory(dev, mem, nullptr);
        });
    }

    for (const auto& ri : retireImages)
        vk.retireTransientImage2D({ ri.format, ri.extent, ri.usage, ri.aspect, ri.image, ri.view, ri.memory });
    for (const auto& rb : retireBuffers)
        vk.retireTransientBuffer({ rb.size, rb.usage, rb.memFlags, rb.buffer, rb.memory });

    vk.endFrame();
    cmdBuf = VK_NULL_HANDLE;
}

RenderGraph::ImageHandle RenderGraph::importBackbuffer(VulkanContext& vk)
{
    ImageResource r{};
    r.name = std::pmr::string("backbuffer", arena.resource());
    r.image = vk.currentSwapchainImage();
    r.view = vk.currentSwapchainImageView();
    r.format = vk.swapchainFormat();
    r.extent = vk.swapchainExtent();
    r.externalLayoutPtr = vk.currentSwapchainImageLayoutPtr();
    r.lastStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    r.lastAccess = 0;
    r.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    const uint32_t id = (uint32_t)images.size();
    images.push_back(r);
    return ImageHandle{ id };
}

RenderGraph::ImageHandle RenderGraph::importDepth(VulkanContext& vk)
{
    ImageResource r{};
    r.name = std::pmr::string("depth", arena.resource());
    r.image = vk.depthImage();
    r.view = vk.depthView();
    r.format = vk.depthFormat();
    r.extent = vk.swapchainExtent();
    r.externalLayoutPtr = vk.depthImageLayoutPtr();
    r.lastStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    r.lastAccess = 0;
    r.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

    const uint32_t id = (uint32_t)images.size();
    images.push_back(r);
    return ImageHandle{ id };
}

RenderGraph::ImageHandle RenderGraph::createTransientImage2D(VulkanContext& vk,
                                                             std::string_view name,
                                                             VkFormat format,
                                                             VkExtent2D extent,
                                                             VkImageUsageFlags usage,
                                                             VkImageAspectFlags aspect)
{
    ImageResource r{};
    r.name = std::pmr::string(name, arena.resource());
    r.format = format;
    r.extent = extent;
    r.aspectMask = aspect;
    r.pooled = true;

    auto t = vk.acquireTransientImage2D(r.name, format, extent, usage, aspect);
    r.image = t.image;
    r.view = t.view;
    r.memory = t.memory;
    r.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    r.externalLayoutPtr = nullptr;
    r.lastStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    r.lastAccess = 0;

    retireImages.push_back(RetireImage2D{ format, extent, usage, aspect, r.image, r.view, r.memory });

    const uint32_t id = (uint32_t)images.size();
    images.push_back(r);
    return ImageHandle{ id };
}

RenderGraph::BufferHandle RenderGraph::createTransientBuffer(VulkanContext& vk,
                                                             std::string_view name,
                                                             VkDeviceSize size,
                                                             VkBufferUsageFlags usage,
                                                             VkMemoryPropertyFlags memFlags)
{
    BufferResource r{};
    r.pooled = true;
    auto t = vk.acquireTransientBuffer(name, size, usage, memFlags);
    r.buffer = t.buffer;
    r.memory = t.memory;
    r.lastStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    r.lastAccess = 0;

    retireBuffers.push_back(RetireBuffer{ size, usage, memFlags, r.buffer, r.memory });

    const uint32_t id = (uint32_t)buffers.size();
    buffers.push_back(r);
    return BufferHandle{ id };
}
