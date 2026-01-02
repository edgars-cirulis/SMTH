#pragma once
#include <vulkan/vulkan.h>
#include "../render/RenderScene.hpp"
#include "VulkanContext.hpp"

#include "Mesh.hpp"
#include "RenderGraph.hpp"
#include "UploadManager.hpp"

#include "../render/ShaderLayouts.hpp"

class Renderer {
   public:
    void init(VulkanContext& vk);
    void shutdown(VulkanContext& vk);

    void drawFrame(VulkanContext& vk, const RenderScene& scene);

    void setGpuDriven(bool enabled) { gpuDriven = enabled; }

    struct Texture {
        VkImage image{};
        VkDeviceMemory mem{};
        VkImageView view{};
        VkSampler sampler{};
        VkFormat format{};
        uint32_t width = 0, height = 0;
        void destroy(VulkanContext& vk)
        {
            const VkDevice dev = vk.device();
            const VkSampler oldSampler = sampler;
            const VkImageView oldView = view;
            const VkImage oldImage = image;
            const VkDeviceMemory oldMem = mem;

            if (oldSampler || oldView || oldImage || oldMem) {
                vk.frameDeletionQueue().push([dev, oldSampler, oldView, oldImage, oldMem]() {
                    if (oldSampler)
                        vkDestroySampler(dev, oldSampler, nullptr);
                    if (oldView)
                        vkDestroyImageView(dev, oldView, nullptr);
                    if (oldImage)
                        vkDestroyImage(dev, oldImage, nullptr);
                    if (oldMem)
                        vkFreeMemory(dev, oldMem, nullptr);
                });
            }
            sampler = {};
            view = {};
            image = {};
            mem = {};
        }
    };

   private:
    VkShaderModule makeShader(VulkanContext& vk, const char* path);

    void createPipelines(VulkanContext& vk);
    void destroyPipelines(VulkanContext& vk);

    void createScene(VulkanContext& vk);
    void destroyScene(VulkanContext& vk);

    void createFrameResources(VulkanContext& vk);
    void destroyFrameResources(VulkanContext& vk);
    void createMaterialResources(VulkanContext& vk);
    void destroyMaterialResources(VulkanContext& vk);

    void createGpuDrivenResources(VulkanContext& vk);
    void destroyGpuDrivenResources(VulkanContext& vk);
    uint32_t recordGpuCulling(VulkanContext& vk, VkCommandBuffer cmd, const RenderScene& scene);

    VkPipelineLayout meshLayout{};
    VkPipeline meshPipeline{};

    VkPipelineLayout skyLayout{};
    VkPipeline skyPipeline{};

    UploadManager upload;
    Mesh sceneMesh;

    Texture baseColorTex;
    Texture normalTex;
    Texture metalRoughTex;

    float sceneBaseColorFactor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
    float sceneMetallicRoughness[2]{ 1.0f, 1.0f };

    VkDescriptorSetLayout frameSetLayout{};
    VkDescriptorPool framePool{};

    VkDescriptorSetLayout materialSetLayout{};
    VkDescriptorPool materialPool{};
    VkDescriptorSet materialSet{};
    VkBuffer materialUbo{};
    VkDeviceMemory materialUboMem{};
    void* materialUboMapped = nullptr;

    struct FrameResources {
        VkDescriptorSet frameSet{};

        VkBuffer cameraUbo{};
        VkDeviceMemory cameraUboMem{};
        void* cameraUboMapped = nullptr;

        VkBuffer lightUbo{};
        VkDeviceMemory lightUboMem{};
        void* lightUboMapped = nullptr;

        VkBuffer transformSsbo{};
        VkDeviceMemory transformSsboMem{};
        void* transformSsboMapped = nullptr;

        VkBuffer drawTransformSsbo{};
        VkDeviceMemory drawTransformSsboMem{};
        void* drawTransformSsboMapped = nullptr;

        VkBuffer cullUbo{};
        VkDeviceMemory cullUboMem{};
        void* cullUboMapped = nullptr;

        VkBuffer indirectCmdBuffer{};
        VkDeviceMemory indirectCmdMem{};
        uint32_t indirectMaxDraws = 0;

        VkBuffer drawCountBuffer{};
        VkDeviceMemory drawCountMem{};

        VkDescriptorSet cullSet{};
    };

    static constexpr uint32_t kFramesInFlight = 2;
    FrameResources frames[kFramesInFlight]{};

    RenderGraph graph;

    VkDescriptorSetLayout cullSetLayout{};
    VkDescriptorPool cullPool{};
    VkPipelineLayout cullLayout{};
    VkPipeline cullPipeline{};

    VkBuffer meshBoundsSsbo{};
    VkDeviceMemory meshBoundsMem{};

    bool gpuDriven = true;

    uint64_t lastSwapchainGen = ~0ull;
    double startTimeSeconds = 0.0;
};
