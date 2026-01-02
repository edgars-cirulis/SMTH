#include "Renderer.hpp"
#include "engine/assets/ImageLoaderWIC.hpp"

#include "VulkanHelpers.hpp"
#include "engine/assets/GltfLoader.hpp"
#include "engine/assets/ObjLoader.hpp"
#include "engine/render/Frustum.hpp"

namespace {
struct CullingUBO {
    glm::vec4 planes[6]{};
};

struct CullPush {
    uint32_t drawCount = 0;
    uint32_t indexCount = 0;
};
}  // namespace

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

static VkPipelineShaderStageCreateInfo shaderStage(VkShaderStageFlagBits stage, VkShaderModule mod, const char* entry = "main")
{
    VkPipelineShaderStageCreateInfo s{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    s.stage = stage;
    s.module = mod;
    s.pName = entry;
    return s;
}

static void writeMat4(float out16[16], const glm::mat4& m)
{
    std::memcpy(out16, &m[0][0], sizeof(float) * 16);
}

static void transitionImage(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        b.srcAccessMask = 0;
        b.dstAccessMask = 0;
        srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

static void createTexture2D(VulkanContext& vk,
                            UploadManager& up,
                            Renderer::Texture& tex,
                            uint32_t w,
                            uint32_t h,
                            VkFormat fmt,
                            const uint8_t* rgbaPixels)
{
    VkDevice dev = vk.device();
    VkPhysicalDevice phys = vk.physicalDevice();

    tex.width = w;
    tex.height = h;
    tex.format = fmt;

    VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = VkExtent3D{ w, h, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vkCheck(vkCreateImage(dev, &ici, nullptr, &tex.image), "vkCreateImage(tex)");
    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(dev, tex.image, &mr);

    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemoryType(phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkCheck(vkAllocateMemory(dev, &mai, nullptr, &tex.mem), "vkAllocateMemory(tex)");
    vkCheck(vkBindImageMemory(dev, tex.image, tex.mem, 0), "vkBindImageMemory(tex)");

    const VkDeviceSize byteSize = (VkDeviceSize)w * (VkDeviceSize)h * 4ull;
    auto a = up.alloc(byteSize, 4);
    if (!a.cpu) {
        std::fprintf(stderr, "[upload] Out of staging memory while uploading texture %ux%u (need %llu bytes)\n", w, h,
                     (unsigned long long)byteSize);
        std::abort();
    }
    std::memcpy((uint8_t*)a.cpu, rgbaPixels, (size_t)byteSize);

    transitionImage(up.cmd(), tex.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy bic{};
    bic.bufferOffset = a.srcOffset;
    bic.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bic.imageSubresource.mipLevel = 0;
    bic.imageSubresource.baseArrayLayer = 0;
    bic.imageSubresource.layerCount = 1;
    bic.imageExtent = VkExtent3D{ w, h, 1 };

    vkCmdCopyBufferToImage(up.cmd(), up.stagingBuffer(), tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);

    transitionImage(up.cmd(), tex.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = tex.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = fmt;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(dev, &vci, nullptr, &tex.view), "vkCreateImageView(tex)");

    VkSamplerCreateInfo sci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.maxLod = 0.0f;
    sci.maxAnisotropy = 1.0f;
    sci.anisotropyEnable = VK_FALSE;
    vkCheck(vkCreateSampler(dev, &sci, nullptr, &tex.sampler), "vkCreateSampler(tex)");
}

static void
createSolidTexture(VulkanContext& vk, UploadManager& up, Renderer::Texture& tex, VkFormat fmt, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint8_t px[4]{ r, g, b, a };
    createTexture2D(vk, up, tex, 1, 1, fmt, px);
}

}  // namespace

VkShaderModule Renderer::makeShader(VulkanContext& vk, const char* path)
{
    auto bytes = readFile(path);
    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = bytes.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(bytes.data());
    VkShaderModule mod{};
    vkCheck(vkCreateShaderModule(vk.device(), &ci, nullptr, &mod), "vkCreateShaderModule");
    return mod;
}

void Renderer::init(VulkanContext& vk)
{
    startTimeSeconds = glfwGetTime();
    upload.init(vk);
    createScene(vk);
    createFrameResources(vk);
    createMaterialResources(vk);
    createGpuDrivenResources(vk);
    createPipelines(vk);
}

void Renderer::shutdown(VulkanContext& vk)
{
    vkDeviceWaitIdle(vk.device());
    destroyPipelines(vk);
    destroyGpuDrivenResources(vk);
    destroyScene(vk);
    destroyMaterialResources(vk);
    destroyFrameResources(vk);
    upload.shutdown(vk);
}

void Renderer::createScene(VulkanContext& vk)
{
    upload.beginFrame(vk);

    std::string err;
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;

    GltfSceneData gltf;
    if (loadGltfScene("assets/map.gltf", gltf, err)) {
        verts = std::move(gltf.vertices);
        idx = std::move(gltf.indices);

        sceneBaseColorFactor[0] = gltf.material.baseColorFactor.x;
        sceneBaseColorFactor[1] = gltf.material.baseColorFactor.y;
        sceneBaseColorFactor[2] = gltf.material.baseColorFactor.z;
        sceneBaseColorFactor[3] = gltf.material.baseColorFactor.w;
        sceneMetallicRoughness[0] = gltf.material.metallicFactor;
        sceneMetallicRoughness[1] = gltf.material.roughnessFactor;

        baseColorTex.destroy(vk);
        normalTex.destroy(vk);
        metalRoughTex.destroy(vk);

        auto createFromGltfOrSolid = [&](const std::string& rel, Renderer::Texture& outTex, VkFormat fmt, uint8_t sr, uint8_t sg,
                                         uint8_t sb, uint8_t sa) {
            if (!rel.empty()) {
                ImageRGBA8 img;
                std::string imErr;
                std::string fullPath = std::string("assets/") + rel;
                if (loadImageRGBA8_WIC(fullPath, img, imErr)) {
                    createTexture2D(vk, upload, outTex, img.width, img.height, fmt, img.pixels.data());
                    return;
                }
            }
            createSolidTexture(vk, upload, outTex, fmt, sr, sg, sb, sa);
        };

        createFromGltfOrSolid(gltf.material.baseColorUri, baseColorTex, VK_FORMAT_R8G8B8A8_SRGB, 255, 255, 255, 255);
        createFromGltfOrSolid(gltf.material.normalUri, normalTex, VK_FORMAT_R8G8B8A8_UNORM, 128, 128, 255, 255);
        createFromGltfOrSolid(gltf.material.metallicRoughnessUri, metalRoughTex, VK_FORMAT_R8G8B8A8_UNORM, 0, 255, 0, 255);
    } else {
        ObjMeshData obj;
        std::string objErr;
        if (loadObj("assets/map.obj", obj, objErr)) {
            verts.reserve(obj.vertices.size());
            for (auto& v : obj.vertices) {
                verts.push_back(Vertex{ v.pos, v.nrm, v.uv });
            }
            idx = std::move(obj.indices);
        } else {
            verts = {
                { { -50, 0, -50 }, { 0, 1, 0 }, { 0, 0 } },
                { { 50, 0, -50 }, { 0, 1, 0 }, { 1, 0 } },
                { { 50, 0, 50 }, { 0, 1, 0 }, { 1, 1 } },
                { { -50, 0, 50 }, { 0, 1, 0 }, { 0, 1 } },
            };
            idx = { 0, 1, 2, 0, 2, 3 };
        }
    }

    sceneMesh.create(vk, upload, verts, idx);
    upload.endFrame(vk);

    vkQueueWaitIdle(vk.graphicsQueue());
}

void Renderer::destroyScene(VulkanContext& vk)
{
    sceneMesh.destroy(vk);
    baseColorTex.destroy(vk);
    normalTex.destroy(vk);
    metalRoughTex.destroy(vk);
}

void Renderer::createFrameResources(VulkanContext& vk)
{
    VkDevice dev = vk.device();
    VkPhysicalDevice phys = vk.physicalDevice();

    VkDescriptorSetLayoutBinding bs[3]{};

    bs[0].binding = ShaderLayout::BIND_CAMERA;
    bs[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bs[0].descriptorCount = 1;
    bs[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bs[1].binding = ShaderLayout::BIND_LIGHT;
    bs[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bs[1].descriptorCount = 1;
    bs[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bs[2].binding = ShaderLayout::BIND_TRANSFORMS;
    bs[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bs[2].descriptorCount = 1;
    bs[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    lci.bindingCount = 3;
    lci.pBindings = bs;
    vkCheck(vkCreateDescriptorSetLayout(dev, &lci, nullptr, &frameSetLayout), "vkCreateDescriptorSetLayout(frameSetLayout)");

    VkDescriptorPoolSize ps[2]{};
    ps[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps[0].descriptorCount = kFramesInFlight * 2;
    ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    ps[1].descriptorCount = kFramesInFlight;

    VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pci.maxSets = kFramesInFlight;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = ps;
    vkCheck(vkCreateDescriptorPool(dev, &pci, nullptr, &framePool), "vkCreateDescriptorPool(framePool)");

    VkDescriptorSetLayout layouts[kFramesInFlight]{};
    for (uint32_t i = 0; i < kFramesInFlight; ++i)
        layouts[i] = frameSetLayout;

    VkDescriptorSetAllocateInfo asi{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    asi.descriptorPool = framePool;
    asi.descriptorSetCount = kFramesInFlight;
    asi.pSetLayouts = layouts;

    VkDescriptorSet sets[kFramesInFlight]{};
    vkCheck(vkAllocateDescriptorSets(dev, &asi, sets), "vkAllocateDescriptorSets(frame sets)");

    constexpr VkDeviceSize CAMERA_SIZE = sizeof(ShaderLayout::CameraUBO);
    constexpr VkDeviceSize LIGHT_SIZE = sizeof(ShaderLayout::LightUBO);
    constexpr VkDeviceSize TRANSFORM_SSBO_SIZE = sizeof(glm::mat4) * 4096ull;

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frames[i].frameSet = sets[i];

        createBuffer(dev, phys, CAMERA_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frames[i].cameraUbo,
                     frames[i].cameraUboMem, "vkCreateBuffer(camera ubo)");
        frames[i].cameraUboMapped = mapMemory(dev, frames[i].cameraUboMem, CAMERA_SIZE);

        createBuffer(dev, phys, LIGHT_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frames[i].lightUbo, frames[i].lightUboMem,
                     "vkCreateBuffer(light ubo)");
        frames[i].lightUboMapped = mapMemory(dev, frames[i].lightUboMem, LIGHT_SIZE);

        createBuffer(dev, phys, TRANSFORM_SSBO_SIZE, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frames[i].transformSsbo,
                     frames[i].transformSsboMem, "vkCreateBuffer(transform ssbo)");
        frames[i].transformSsboMapped = mapMemory(dev, frames[i].transformSsboMem, TRANSFORM_SSBO_SIZE);

        VkDescriptorBufferInfo camBI{};
        camBI.buffer = frames[i].cameraUbo;
        camBI.offset = 0;
        camBI.range = CAMERA_SIZE;

        VkDescriptorBufferInfo lightBI{};
        lightBI.buffer = frames[i].lightUbo;
        lightBI.offset = 0;
        lightBI.range = LIGHT_SIZE;

        VkDescriptorBufferInfo tbi{};
        tbi.buffer = frames[i].transformSsbo;
        tbi.offset = 0;
        tbi.range = TRANSFORM_SSBO_SIZE;

        VkWriteDescriptorSet ws[3]{};

        ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[0].dstSet = frames[i].frameSet;
        ws[0].dstBinding = ShaderLayout::BIND_CAMERA;
        ws[0].descriptorCount = 1;
        ws[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ws[0].pBufferInfo = &camBI;

        ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[1].dstSet = frames[i].frameSet;
        ws[1].dstBinding = ShaderLayout::BIND_LIGHT;
        ws[1].descriptorCount = 1;
        ws[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ws[1].pBufferInfo = &lightBI;

        ws[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[2].dstSet = frames[i].frameSet;
        ws[2].dstBinding = ShaderLayout::BIND_TRANSFORMS;
        ws[2].descriptorCount = 1;
        ws[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws[2].pBufferInfo = &tbi;

        vkUpdateDescriptorSets(dev, 3, ws, 0, nullptr);
    }
}

void Renderer::destroyFrameResources(VulkanContext& vk)
{
    VkDevice dev = vk.device();
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (frames[i].cameraUboMapped) {
            vkUnmapMemory(dev, frames[i].cameraUboMem);
            frames[i].cameraUboMapped = nullptr;
        }
        if (frames[i].cameraUbo)
            vkDestroyBuffer(dev, frames[i].cameraUbo, nullptr);
        if (frames[i].cameraUboMem)
            vkFreeMemory(dev, frames[i].cameraUboMem, nullptr);

        if (frames[i].lightUboMapped) {
            vkUnmapMemory(dev, frames[i].lightUboMem);
            frames[i].lightUboMapped = nullptr;
        }
        if (frames[i].lightUbo)
            vkDestroyBuffer(dev, frames[i].lightUbo, nullptr);
        if (frames[i].lightUboMem)
            vkFreeMemory(dev, frames[i].lightUboMem, nullptr);

        if (frames[i].transformSsboMapped) {
            vkUnmapMemory(dev, frames[i].transformSsboMem);
            frames[i].transformSsboMapped = nullptr;
        }
        if (frames[i].transformSsbo)
            vkDestroyBuffer(dev, frames[i].transformSsbo, nullptr);
        if (frames[i].transformSsboMem)
            vkFreeMemory(dev, frames[i].transformSsboMem, nullptr);

        frames[i] = {};
    }
    if (framePool)
        vkDestroyDescriptorPool(dev, framePool, nullptr);
    if (frameSetLayout)
        vkDestroyDescriptorSetLayout(dev, frameSetLayout, nullptr);
    framePool = {};
    frameSetLayout = {};
}

void Renderer::createMaterialResources(VulkanContext& vk)
{
    VkDevice dev = vk.device();
    VkPhysicalDevice phys = vk.physicalDevice();

    VkDescriptorSetLayoutBinding bs[4]{};
    for (uint32_t i = 0; i < 3; ++i) {
        bs[i].binding = i;
        bs[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bs[i].descriptorCount = 1;
        bs[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bs[3].binding = ShaderLayout::BIND_MATERIAL;
    bs[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bs[3].descriptorCount = 1;
    bs[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    lci.bindingCount = 4;
    lci.pBindings = bs;
    vkCheck(vkCreateDescriptorSetLayout(dev, &lci, nullptr, &materialSetLayout), "vkCreateDescriptorSetLayout(materialSetLayout)");

    VkDescriptorPoolSize ps[2]{};
    ps[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps[0].descriptorCount = 3;
    ps[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    pci.maxSets = 1;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = ps;
    vkCheck(vkCreateDescriptorPool(dev, &pci, nullptr, &materialPool), "vkCreateDescriptorPool(materialPool)");

    VkDescriptorSetAllocateInfo asi{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    asi.descriptorPool = materialPool;
    asi.descriptorSetCount = 1;
    asi.pSetLayouts = &materialSetLayout;
    vkCheck(vkAllocateDescriptorSets(dev, &asi, &materialSet), "vkAllocateDescriptorSets(materialSet)");

    constexpr VkDeviceSize MAT_SIZE = sizeof(ShaderLayout::MaterialUBO);
    createBuffer(dev, phys, MAT_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, materialUbo, materialUboMem,
                 "vkCreateBuffer(material ubo)");
    materialUboMapped = mapMemory(dev, materialUboMem, MAT_SIZE);

    VkDescriptorImageInfo imgBase{};
    imgBase.sampler = baseColorTex.sampler;
    imgBase.imageView = baseColorTex.view;
    imgBase.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo imgNrm{};
    imgNrm.sampler = normalTex.sampler;
    imgNrm.imageView = normalTex.view;
    imgNrm.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo imgMR{};
    imgMR.sampler = metalRoughTex.sampler;
    imgMR.imageView = metalRoughTex.view;
    imgMR.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorBufferInfo mbi{};
    mbi.buffer = materialUbo;
    mbi.offset = 0;
    mbi.range = MAT_SIZE;

    VkWriteDescriptorSet ws[4]{};
    ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[0].dstSet = materialSet;
    ws[0].dstBinding = ShaderLayout::BIND_BASE_COLOR;
    ws[0].descriptorCount = 1;
    ws[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ws[0].pImageInfo = &imgBase;

    ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[1].dstSet = materialSet;
    ws[1].dstBinding = ShaderLayout::BIND_NORMAL;
    ws[1].descriptorCount = 1;
    ws[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ws[1].pImageInfo = &imgNrm;

    ws[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[2].dstSet = materialSet;
    ws[2].dstBinding = ShaderLayout::BIND_METAL_ROUGH;
    ws[2].descriptorCount = 1;
    ws[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ws[2].pImageInfo = &imgMR;

    ws[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ws[3].dstSet = materialSet;
    ws[3].dstBinding = ShaderLayout::BIND_MATERIAL;
    ws[3].descriptorCount = 1;
    ws[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ws[3].pBufferInfo = &mbi;

    vkUpdateDescriptorSets(dev, 4, ws, 0, nullptr);
}

void Renderer::destroyMaterialResources(VulkanContext& vk)
{
    VkDevice dev = vk.device();
    if (materialUboMapped) {
        vkUnmapMemory(dev, materialUboMem);
        materialUboMapped = nullptr;
    }
    if (materialUbo)
        vkDestroyBuffer(dev, materialUbo, nullptr);
    if (materialUboMem)
        vkFreeMemory(dev, materialUboMem, nullptr);
    materialUbo = {};
    materialUboMem = {};

    if (materialPool)
        vkDestroyDescriptorPool(dev, materialPool, nullptr);
    if (materialSetLayout)
        vkDestroyDescriptorSetLayout(dev, materialSetLayout, nullptr);
    materialPool = {};
    materialSetLayout = {};
    materialSet = {};
}

void Renderer::createGpuDrivenResources(VulkanContext& vk)
{
    VkDevice dev = vk.device();
    VkPhysicalDevice phys = vk.physicalDevice();

    {
        const glm::vec3 bmin = sceneMesh.boundsMin();
        const glm::vec3 bmax = sceneMesh.boundsMax();
        const glm::vec3 center = (bmin + bmax) * 0.5f;
        const float radius = glm::length(bmax - center);
        const glm::vec4 centerRadius(center, radius);

        createBuffer(dev, phys, sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, meshBoundsSsbo, meshBoundsMem,
                     "vkCreateBuffer(mesh bounds)");
        void* mapped = mapMemory(dev, meshBoundsMem, sizeof(glm::vec4));
        std::memcpy(mapped, &centerRadius, sizeof(glm::vec4));
        vkUnmapMemory(dev, meshBoundsMem);
    }

    {
        VkDescriptorSetLayoutBinding b[6]{};
        b[0].binding = 0;
        b[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[0].descriptorCount = 1;
        b[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        b[1].binding = 1;
        b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[1].descriptorCount = 1;
        b[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        b[2].binding = 2;
        b[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        b[2].descriptorCount = 1;
        b[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        b[3].binding = 3;
        b[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[3].descriptorCount = 1;
        b[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        b[4].binding = 4;
        b[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[4].descriptorCount = 1;
        b[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        b[5].binding = 5;
        b[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[5].descriptorCount = 1;
        b[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        lci.bindingCount = 6;
        lci.pBindings = b;
        vkCheck(vkCreateDescriptorSetLayout(dev, &lci, nullptr, &cullSetLayout), "vkCreateDescriptorSetLayout(cull)");
    }

    {
        VkDescriptorPoolSize ps[3]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ps[0].descriptorCount = kFramesInFlight * 5;
        ps[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ps[1].descriptorCount = kFramesInFlight * 1;

        VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pci.maxSets = kFramesInFlight;
        pci.poolSizeCount = 2;
        pci.pPoolSizes = ps;
        vkCheck(vkCreateDescriptorPool(dev, &pci, nullptr, &cullPool), "vkCreateDescriptorPool(cull)");
    }

    for (uint32_t fi = 0; fi < kFramesInFlight; ++fi) {
        constexpr VkDeviceSize CULL_UBO_SIZE = sizeof(CullingUBO);
        createBuffer(dev, phys, CULL_UBO_SIZE, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frames[fi].cullUbo, frames[fi].cullUboMem,
                     "vkCreateBuffer(cull ubo)");
        frames[fi].cullUboMapped = mapMemory(dev, frames[fi].cullUboMem, CULL_UBO_SIZE);

        const uint32_t initialDraws = 1024;
        frames[fi].indirectMaxDraws = initialDraws;

        createBuffer(dev, phys, sizeof(uint32_t) * initialDraws, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, frames[fi].drawTransformSsbo,
                     frames[fi].drawTransformSsboMem, "vkCreateBuffer(draw transforms)");
        frames[fi].drawTransformSsboMapped = mapMemory(dev, frames[fi].drawTransformSsboMem, sizeof(uint32_t) * initialDraws);

        createBuffer(dev, phys, sizeof(VkDrawIndexedIndirectCommand) * initialDraws,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     frames[fi].indirectCmdBuffer, frames[fi].indirectCmdMem, "vkCreateBuffer(indirect)");

        createBuffer(dev, phys, sizeof(uint32_t),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, frames[fi].drawCountBuffer, frames[fi].drawCountMem, "vkCreateBuffer(drawCount)");

        VkDescriptorSetAllocateInfo asi{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        asi.descriptorPool = cullPool;
        asi.descriptorSetCount = 1;
        asi.pSetLayouts = &cullSetLayout;
        vkCheck(vkAllocateDescriptorSets(dev, &asi, &frames[fi].cullSet), "vkAllocateDescriptorSets(cull)");
    }

    for (uint32_t fi = 0; fi < kFramesInFlight; ++fi) {
        VkDescriptorBufferInfo transforms{};
        transforms.buffer = frames[fi].transformSsbo;
        transforms.offset = 0;
        transforms.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo drawX{};
        drawX.buffer = frames[fi].drawTransformSsbo;
        drawX.offset = 0;
        drawX.range = VK_WHOLE_SIZE;

        VkDescriptorBufferInfo cullU{};
        cullU.buffer = frames[fi].cullUbo;
        cullU.offset = 0;
        cullU.range = sizeof(CullingUBO);

        VkDescriptorBufferInfo bounds{};
        bounds.buffer = meshBoundsSsbo;
        bounds.offset = 0;
        bounds.range = sizeof(glm::vec4);

        VkDescriptorBufferInfo outCmd{};
        VkDescriptorBufferInfo countB{};
        outCmd.buffer = frames[fi].indirectCmdBuffer;
        outCmd.offset = 0;
        outCmd.range = VK_WHOLE_SIZE;

        countB.buffer = frames[fi].drawCountBuffer;
        countB.offset = 0;
        countB.range = sizeof(uint32_t);

        VkWriteDescriptorSet ws[6]{};
        for (int i = 0; i < 6; ++i)
            ws[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

        ws[0].dstSet = frames[fi].cullSet;
        ws[0].dstBinding = 0;
        ws[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws[0].descriptorCount = 1;
        ws[0].pBufferInfo = &transforms;

        ws[1].dstSet = frames[fi].cullSet;
        ws[1].dstBinding = 1;
        ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws[1].descriptorCount = 1;
        ws[1].pBufferInfo = &drawX;

        ws[2].dstSet = frames[fi].cullSet;
        ws[2].dstBinding = 2;
        ws[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ws[2].descriptorCount = 1;
        ws[2].pBufferInfo = &cullU;

        ws[3].dstSet = frames[fi].cullSet;
        ws[3].dstBinding = 3;
        ws[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws[3].descriptorCount = 1;
        ws[3].pBufferInfo = &bounds;

        ws[4].dstSet = frames[fi].cullSet;
        ws[4].dstBinding = 4;
        ws[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws[4].descriptorCount = 1;
        ws[4].pBufferInfo = &outCmd;

        ws[5].dstSet = frames[fi].cullSet;
        ws[5].dstBinding = 5;
        ws[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws[5].descriptorCount = 1;
        ws[5].pBufferInfo = &countB;

        vkUpdateDescriptorSets(dev, 6, ws, 0, nullptr);
    }

    VkShaderModule cullSm = makeShader(vk, "shaders/cull.comp.spv");
    VkPipelineShaderStageCreateInfo sci{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    sci.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    sci.module = cullSm;
    sci.pName = "main";

    VkPushConstantRange pcr{};
    pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcr.offset = 0;
    pcr.size = sizeof(CullPush);

    VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &cullSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    vkCheck(vkCreatePipelineLayout(dev, &plci, nullptr, &cullLayout), "vkCreatePipelineLayout(cull)");

    VkComputePipelineCreateInfo cpci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    cpci.stage = sci;
    cpci.layout = cullLayout;
    vkCheck(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, nullptr, &cullPipeline), "vkCreateComputePipelines");
    vkDestroyShaderModule(dev, cullSm, nullptr);
}

void Renderer::destroyGpuDrivenResources(VulkanContext& vk)
{
    VkDevice dev = vk.device();

    if (cullPipeline)
        vkDestroyPipeline(dev, cullPipeline, nullptr);
    if (cullLayout)
        vkDestroyPipelineLayout(dev, cullLayout, nullptr);
    if (cullPool)
        vkDestroyDescriptorPool(dev, cullPool, nullptr);
    if (cullSetLayout)
        vkDestroyDescriptorSetLayout(dev, cullSetLayout, nullptr);
    cullPipeline = {};
    cullLayout = {};
    cullPool = {};
    cullSetLayout = {};

    for (uint32_t fi = 0; fi < kFramesInFlight; ++fi) {
        if (frames[fi].cullUboMapped) {
            vkUnmapMemory(dev, frames[fi].cullUboMem);
            frames[fi].cullUboMapped = nullptr;
        }
        if (frames[fi].cullUbo)
            vkDestroyBuffer(dev, frames[fi].cullUbo, nullptr);
        if (frames[fi].cullUboMem)
            vkFreeMemory(dev, frames[fi].cullUboMem, nullptr);

        if (frames[fi].drawTransformSsboMapped) {
            vkUnmapMemory(dev, frames[fi].drawTransformSsboMem);
            frames[fi].drawTransformSsboMapped = nullptr;
        }
        if (frames[fi].drawTransformSsbo)
            vkDestroyBuffer(dev, frames[fi].drawTransformSsbo, nullptr);
        if (frames[fi].drawTransformSsboMem)
            vkFreeMemory(dev, frames[fi].drawTransformSsboMem, nullptr);

        if (frames[fi].indirectCmdBuffer)
            vkDestroyBuffer(dev, frames[fi].indirectCmdBuffer, nullptr);
        if (frames[fi].indirectCmdMem)
            vkFreeMemory(dev, frames[fi].indirectCmdMem, nullptr);

        if (frames[fi].drawCountBuffer)
            vkDestroyBuffer(dev, frames[fi].drawCountBuffer, nullptr);
        if (frames[fi].drawCountMem)
            vkFreeMemory(dev, frames[fi].drawCountMem, nullptr);

        frames[fi].indirectCmdBuffer = {};
        frames[fi].indirectCmdMem = {};
        frames[fi].drawCountBuffer = {};
        frames[fi].drawCountMem = {};
        frames[fi].indirectMaxDraws = 0;
        frames[fi].cullSet = {};
    }

    if (meshBoundsSsbo)
        vkDestroyBuffer(dev, meshBoundsSsbo, nullptr);
    if (meshBoundsMem)
        vkFreeMemory(dev, meshBoundsMem, nullptr);
    meshBoundsSsbo = {};
    meshBoundsMem = {};
}

uint32_t Renderer::recordGpuCulling(VulkanContext& vk, VkCommandBuffer cmd, const RenderScene& scene)
{
    if (!gpuDriven)
        return 0;

    const uint32_t fi = vk.currentFrameIndex();
    const uint32_t drawCount = (uint32_t)scene.draws.size();
    if (drawCount == 0)
        return 0;

    VkDevice dev = vk.device();
    VkPhysicalDevice phys = vk.physicalDevice();

    auto& fr = frames[fi];
    if (drawCount > fr.indirectMaxDraws) {
        const uint32_t newMax = std::max(drawCount, fr.indirectMaxDraws * 2u);

        VkBuffer oldDraw = fr.drawTransformSsbo;
        VkDeviceMemory oldDrawMem = fr.drawTransformSsboMem;
        VkBuffer oldIndirect = fr.indirectCmdBuffer;
        VkDeviceMemory oldIndirectMem = fr.indirectCmdMem;
        void* oldDrawMapped = fr.drawTransformSsboMapped;

        vk.frameDeletionQueue().push([dev, oldDraw, oldDrawMem, oldIndirect, oldIndirectMem]() {
            if (oldDraw)
                vkDestroyBuffer(dev, oldDraw, nullptr);
            if (oldDrawMem)
                vkFreeMemory(dev, oldDrawMem, nullptr);
            if (oldIndirect)
                vkDestroyBuffer(dev, oldIndirect, nullptr);
            if (oldIndirectMem)
                vkFreeMemory(dev, oldIndirectMem, nullptr);
        });

        if (oldDrawMapped) {
            vkUnmapMemory(dev, oldDrawMem);
        }

        createBuffer(dev, phys, sizeof(uint32_t) * newMax, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, fr.drawTransformSsbo,
                     fr.drawTransformSsboMem, "vkCreateBuffer(draw transforms resize)");
        fr.drawTransformSsboMapped = mapMemory(dev, fr.drawTransformSsboMem, sizeof(uint32_t) * newMax);

        createBuffer(dev, phys, sizeof(VkDrawIndexedIndirectCommand) * newMax,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     fr.indirectCmdBuffer, fr.indirectCmdMem, "vkCreateBuffer(indirect resize)");
        fr.indirectMaxDraws = newMax;

        VkDescriptorBufferInfo drawX{ fr.drawTransformSsbo, 0, VK_WHOLE_SIZE };
        VkDescriptorBufferInfo outCmd{ fr.indirectCmdBuffer, 0, VK_WHOLE_SIZE };

        VkWriteDescriptorSet ws[2]{};
        ws[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[0].dstSet = fr.cullSet;
        ws[0].dstBinding = 1;
        ws[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws[0].descriptorCount = 1;
        ws[0].pBufferInfo = &drawX;

        ws[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        ws[1].dstSet = fr.cullSet;
        ws[1].dstBinding = 4;
        ws[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        ws[1].descriptorCount = 1;
        ws[1].pBufferInfo = &outCmd;

        vkUpdateDescriptorSets(dev, 2, ws, 0, nullptr);
    }

    uint32_t* outT = reinterpret_cast<uint32_t*>(fr.drawTransformSsboMapped);
    uint32_t written = 0;
    for (const DrawItem& d : scene.draws) {
        if (d.meshId != 0)
            continue;
        if (d.transformIndex >= scene.transforms.size())
            continue;
        outT[written++] = d.transformIndex;
    }

    const uint32_t finalDrawCount = written;
    if (finalDrawCount == 0)
        return 0;

    const glm::mat4 vp = scene.camera.proj * scene.camera.view;
    const FrustumPlanes frPlanes = makeFrustumPlanes(vp);
    CullingUBO u{};
    for (int i = 0; i < 6; ++i) {
        u.planes[i] = frPlanes.p[(size_t)i];
    }
    std::memcpy(fr.cullUboMapped, &u, sizeof(u));

    vkCmdFillBuffer(cmd, fr.drawCountBuffer, 0, sizeof(uint32_t), 0);
    VkBufferMemoryBarrier countReset{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
    countReset.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    countReset.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    countReset.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    countReset.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    countReset.buffer = fr.drawCountBuffer;
    countReset.offset = 0;
    countReset.size = sizeof(uint32_t);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, &countReset, 0,
                         nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, cullLayout, 0, 1, &fr.cullSet, 0, nullptr);

    CullPush pc{};
    pc.drawCount = finalDrawCount;
    pc.indexCount = sceneMesh.indexCount();
    vkCmdPushConstants(cmd, cullLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    const uint32_t groups = (finalDrawCount + 63u) / 64u;
    vkCmdDispatch(cmd, groups, 1, 1);

    return finalDrawCount;
}

void Renderer::drawFrame(VulkanContext& vk, const RenderScene& scene)
{
    if (lastSwapchainGen != vk.swapchainGeneration()) {
        destroyPipelines(vk);
        createPipelines(vk);
    }

    VkCommandBuffer cmd = graph.begin(vk);
    if (!cmd)
        return;

    const uint32_t fi = vk.currentFrameIndex();

    VkExtent2D ext = vk.swapchainExtent();
    float aspect = (ext.height > 0) ? ((float)ext.width / (float)ext.height) : 1.0f;

    uint32_t visibleDrawCount = 0;

    VkViewport vp{};
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = (float)ext.width;
    vp.height = (float)ext.height;
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D sc{};
    sc.offset = { 0, 0 };
    sc.extent = ext;
    vkCmdSetScissor(cmd, 0, 1, &sc);

    const double t = (double)scene.timeSeconds;

    const RenderCameraData& cam = scene.camera;

    ShaderLayout::CameraUBO camUbo{};
    camUbo.view = cam.view;
    camUbo.proj = cam.proj;
    camUbo.camPos = cam.position;

    ShaderLayout::LightUBO lightUbo{};
    lightUbo.lightDir = scene.sun.direction;
    lightUbo.lightIntensity = scene.sun.intensity;
    lightUbo.lightColor = scene.sun.color;
    lightUbo.exposure = scene.exposure;

    ShaderLayout::MaterialUBO matUbo{};
    matUbo.baseColorFactor = glm::vec4(sceneBaseColorFactor[0], sceneBaseColorFactor[1], sceneBaseColorFactor[2], sceneBaseColorFactor[3]);
    matUbo.metallicRoughnessFactor = glm::vec2(sceneMetallicRoughness[0], sceneMetallicRoughness[1]);

    std::memcpy(frames[fi].cameraUboMapped, &camUbo, sizeof(camUbo));
    std::memcpy(frames[fi].lightUboMapped, &lightUbo, sizeof(lightUbo));
    std::memcpy(materialUboMapped, &matUbo, sizeof(matUbo));
    constexpr uint32_t kMaxTransforms = 4096;
    const uint32_t transformCount = (uint32_t)std::min<size_t>(scene.transforms.size(), kMaxTransforms);
    if (transformCount > 0) {
        std::memcpy(frames[fi].transformSsboMapped, scene.transforms.data(), sizeof(glm::mat4) * transformCount);
    }

    VkClearValue cclear{};
    cclear.color = { { 0.05f, 0.06f, 0.08f, 1.0f } };

    VkClearValue dclear{};
    dclear.depthStencil = { 1.0f, 0 };

    auto indirectH = graph.importBuffer(frames[fi].indirectCmdBuffer);
    auto countH = graph.importBuffer(frames[fi].drawCountBuffer);

    graph.addPass(
        "cull", RenderGraph::PassType::Compute,
        [&](RenderGraph::PassBuilder& b) {
            b.writeBuffer(indirectH, RenderGraph::BufferUse::Storage);
            b.writeBuffer(countH, RenderGraph::BufferUse::Storage);
        },
        [&](VkCommandBuffer pcmd) { visibleDrawCount = recordGpuCulling(vk, pcmd, scene); });

    graph.addPass(
        "sky", RenderGraph::PassType::Graphics,
        [&](RenderGraph::PassBuilder& b) {
            b.colorAttachment(graph.backbuffer(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, cclear);
        },
        [&](VkCommandBuffer pcmd) {
            vkCmdBindPipeline(pcmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);

            struct SkyPC {
                float camForward[3];
                float tanHalfFov;
                float camRight[3];
                float aspect;
                float camUp[3];
                float time;
                float sunDir[3];
                float _pad;
            } pc{};

            const glm::vec3 f = cam.forward;
            const glm::vec3 r = cam.right;
            const glm::vec3 u = cam.up;

            pc.camForward[0] = f.x;
            pc.camForward[1] = f.y;
            pc.camForward[2] = f.z;
            pc.camRight[0] = r.x;
            pc.camRight[1] = r.y;
            pc.camRight[2] = r.z;
            pc.camUp[0] = u.x;
            pc.camUp[1] = u.y;
            pc.camUp[2] = u.z;
            pc.tanHalfFov = std::tan(scene.camera.fovRadians * 0.5f);
            pc.aspect = aspect;
            pc.time = (float)t;
            pc.sunDir[0] = scene.sun.direction.x;
            pc.sunDir[1] = scene.sun.direction.y;
            pc.sunDir[2] = scene.sun.direction.z;

            vkCmdPushConstants(pcmd, skyLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SkyPC), &pc);
            vkCmdDraw(pcmd, 3, 1, 0, 0);
        });

    graph.addPass(
        "opaque", RenderGraph::PassType::Graphics,
        [&](RenderGraph::PassBuilder& b) {
            b.colorAttachment(graph.backbuffer(), VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
            b.depthAttachment(graph.depth(), VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE, dclear);
            b.readBuffer(indirectH, RenderGraph::BufferUse::Indirect);
            b.readBuffer(countH, RenderGraph::BufferUse::Indirect);
        },
        [&](VkCommandBuffer pcmd) {
            vkCmdBindPipeline(pcmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshPipeline);

            VkDeviceSize off = 0;
            VkBuffer vb = sceneMesh.vertexBuffer();
            vkCmdBindVertexBuffers(pcmd, 0, 1, &vb, &off);
            vkCmdBindIndexBuffer(pcmd, sceneMesh.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);
            VkDescriptorSet sets[2] = { frames[fi].frameSet, materialSet };
            vkCmdBindDescriptorSets(pcmd, VK_PIPELINE_BIND_POINT_GRAPHICS, meshLayout, 0, 2, sets, 0, nullptr);

            if (gpuDriven) {
                if (visibleDrawCount > 0) {
                    vk.cmdDrawIndexedIndirectCount(pcmd, frames[fi].indirectCmdBuffer, 0, frames[fi].drawCountBuffer, 0,
                                                   frames[fi].indirectMaxDraws, sizeof(VkDrawIndexedIndirectCommand));
                }
                return;
            }

            const glm::mat4 vp = scene.camera.proj * scene.camera.view;
            const FrustumPlanes fr = makeFrustumPlanes(vp);

            std::vector<const DrawItem*> visible;
            visible.reserve(scene.draws.size());

            for (const DrawItem& d : scene.draws) {
                if (d.meshId != 0)
                    continue;
                if (d.transformIndex >= scene.transforms.size())
                    continue;

                glm::vec3 wmin{}, wmax{};
                transformAABB(scene.transforms[d.transformIndex], sceneMesh.boundsMin(), sceneMesh.boundsMax(), wmin, wmax);
                if (!frustumIntersectsAABB(fr, wmin, wmax))
                    continue;

                visible.push_back(&d);
            }

            std::sort(visible.begin(), visible.end(), [](const DrawItem* a, const DrawItem* b) {
                if (a->materialId != b->materialId)
                    return a->materialId < b->materialId;
                if (a->meshId != b->meshId)
                    return a->meshId < b->meshId;
                return a->transformIndex < b->transformIndex;
            });

            for (const DrawItem* pd : visible) {
                vkCmdDrawIndexed(pcmd, sceneMesh.indexCount(), 1, 0, 0, pd->transformIndex);
            }
        });

    graph.execute(vk);
    graph.end(vk);
}

void Renderer::destroyPipelines(VulkanContext& vk)
{
    VkDevice dev = vk.device();
    if (meshPipeline)
        vkDestroyPipeline(dev, meshPipeline, nullptr);
    if (meshLayout)
        vkDestroyPipelineLayout(dev, meshLayout, nullptr);
    if (skyPipeline)
        vkDestroyPipeline(dev, skyPipeline, nullptr);
    if (skyLayout)
        vkDestroyPipelineLayout(dev, skyLayout, nullptr);
    meshPipeline = {};
    meshLayout = {};
    skyPipeline = {};
    skyLayout = {};
    lastSwapchainGen = ~0ull;
}

void Renderer::createPipelines(VulkanContext& vk)
{
    VkDevice dev = vk.device();

    VkDynamicState dyns[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyns;

    VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    cb.attachmentCount = 1;
    cb.pAttachments = &att;

    VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    const bool dynRender = vk.dynamicRenderingEnabled();

    VkPipelineRenderingCreateInfoKHR renderingInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
    VkFormat colorFmt = vk.swapchainFormat();
    VkFormat depthFmt = vk.depthFormat();
    if (dynRender) {
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFmt;
        renderingInfo.depthAttachmentFormat = depthFmt;
    }

    VkPipelineRenderingCreateInfoKHR skyRenderingInfo = renderingInfo;
    if (dynRender) {
        skyRenderingInfo.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        skyRenderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
    }

    {
        VkShaderModule vs = makeShader(vk, "shaders/sky.vert.spv");
        VkShaderModule fs = makeShader(vk, "shaders/sky.frag.spv");
        VkPipelineShaderStageCreateInfo stages[] = { shaderStage(VK_SHADER_STAGE_VERTEX_BIT, vs),
                                                     shaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fs) };

        struct SkyPC {
            float camForward[3];
            float tanHalfFov;
            float camRight[3];
            float aspect;
            float camUp[3];
            float time;
            float sunDir[3];
            float _pad;
        };

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(SkyPC);

        VkPipelineLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        lci.pushConstantRangeCount = 1;
        lci.pPushConstantRanges = &pcr;
        vkCheck(vkCreatePipelineLayout(dev, &lci, nullptr, &skyLayout), "vkCreatePipelineLayout(sky)");

        VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pci.stageCount = 2;
        pci.pStages = stages;
        pci.pVertexInputState = &vi;
        pci.pInputAssemblyState = &ia;
        pci.pViewportState = &vp;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState = &ms;
        pci.pColorBlendState = &cb;
        pci.pDynamicState = &dyn;
        pci.layout = skyLayout;
        if (dynRender) {
            pci.pNext = &skyRenderingInfo;
            pci.renderPass = VK_NULL_HANDLE;
            pci.subpass = 0;
        } else {
            pci.renderPass = vk.renderPass();
            pci.subpass = 0;
        }
        vkCheck(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr, &skyPipeline), "vkCreateGraphicsPipelines(sky)");
        vkDestroyShaderModule(dev, vs, nullptr);
        vkDestroyShaderModule(dev, fs, nullptr);
    }

    {
        VkShaderModule vs = makeShader(vk, "shaders/mesh.vert.spv");
        VkShaderModule fs = makeShader(vk, "shaders/mesh.frag.spv");
        VkPipelineShaderStageCreateInfo stages[] = { shaderStage(VK_SHADER_STAGE_VERTEX_BIT, vs),
                                                     shaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fs) };

        VkVertexInputBindingDescription bind{};
        bind.binding = 0;
        bind.stride = sizeof(Vertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrs[4]{};
        attrs[0].location = 0;
        attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[0].offset = offsetof(Vertex, pos);

        attrs[1].location = 1;
        attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attrs[1].offset = offsetof(Vertex, nrm);

        attrs[2].location = 2;
        attrs[2].binding = 0;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT;
        attrs[2].offset = offsetof(Vertex, uv);

        attrs[3].location = 3;
        attrs[3].binding = 0;
        attrs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attrs[3].offset = offsetof(Vertex, tangent);

        VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &bind;

        vi.vertexAttributeDescriptionCount = 4;
        vi.pVertexAttributeDescriptions = attrs;

        VkPipelineLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        VkDescriptorSetLayout setLayouts[2] = { frameSetLayout, materialSetLayout };
        lci.setLayoutCount = 2;
        lci.pSetLayouts = setLayouts;
        lci.pushConstantRangeCount = 0;
        lci.pPushConstantRanges = nullptr;
        vkCheck(vkCreatePipelineLayout(dev, &lci, nullptr, &meshLayout), "vkCreatePipelineLayout(mesh)");

        VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_BACK_BIT;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;

        VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pci.stageCount = 2;
        pci.pStages = stages;
        pci.pVertexInputState = &vi;
        pci.pInputAssemblyState = &ia;
        pci.pViewportState = &vp;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState = &ms;
        pci.pDepthStencilState = &ds;
        pci.pColorBlendState = &cb;
        pci.pDynamicState = &dyn;
        pci.layout = meshLayout;
        if (dynRender) {
            pci.pNext = &renderingInfo;
            pci.renderPass = VK_NULL_HANDLE;
            pci.subpass = 0;
        } else {
            pci.renderPass = vk.renderPass();
            pci.subpass = 0;
        }
        vkCheck(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr, &meshPipeline), "vkCreateGraphicsPipelines(mesh)");
        vkDestroyShaderModule(dev, vs, nullptr);
        vkDestroyShaderModule(dev, fs, nullptr);
    }

    lastSwapchainGen = vk.swapchainGeneration();
}
