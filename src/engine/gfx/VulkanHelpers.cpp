#include "VulkanHelpers.hpp"
#include <fstream>
#include <stdexcept>

[[noreturn]] void vkFail(const char* where, VkResult r)
{
    throw std::runtime_error(std::string(where) + " failed with VkResult=" + std::to_string((int)r));
}
void vkCheck(VkResult r, const char* where)
{
    if (r != VK_SUCCESS)
        vkFail(where, r);
}

std::vector<uint8_t> readFile(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error(std::string("readFile: can't open ") + path);
    f.seekg(0, std::ios::end);
    size_t sz = (size_t)f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(sz);
    f.read((char*)data.data(), (std::streamsize)sz);
    return data;
}

uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("findMemoryType: no compatible type");
}

void createBuffer(VkDevice dev,
                  VkPhysicalDevice phys,
                  VkDeviceSize size,
                  VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags memFlags,
                  VkBuffer& outBuf,
                  VkDeviceMemory& outMem,
                  const char* debugWhere)
{
    VkBufferCreateInfo bci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCheck(vkCreateBuffer(dev, &bci, nullptr, &outBuf), debugWhere);

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(dev, outBuf, &req);

    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(phys, req.memoryTypeBits, memFlags);
    vkCheck(vkAllocateMemory(dev, &mai, nullptr, &outMem), "vkAllocateMemory");
    vkCheck(vkBindBufferMemory(dev, outBuf, outMem, 0), "vkBindBufferMemory");
}

void* mapMemory(VkDevice dev, VkDeviceMemory mem, VkDeviceSize size, VkDeviceSize offset)
{
    void* p = nullptr;
    vkCheck(vkMapMemory(dev, mem, offset, size, 0, &p), "vkMapMemory");
    return p;
}

void createImage2D(VkDevice dev,
                   VkPhysicalDevice phys,
                   const VkImageCreateInfo& info,
                   VkMemoryPropertyFlags memFlags,
                   VkImage& outImage,
                   VkDeviceMemory& outMem,
                   const char* debugWhere)
{
    vkCheck(vkCreateImage(dev, &info, nullptr, &outImage), debugWhere);

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(dev, outImage, &req);

    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(phys, req.memoryTypeBits, memFlags);
    vkCheck(vkAllocateMemory(dev, &mai, nullptr, &outMem), "vkAllocateMemory(image)");
    vkCheck(vkBindImageMemory(dev, outImage, outMem, 0), "vkBindImageMemory");
}

VkImageView createImageView2D(VkDevice dev, VkImage image, VkFormat format, VkImageAspectFlags aspect, uint32_t mipLevels)
{
    VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = format;
    vci.subresourceRange.aspectMask = aspect;
    vci.subresourceRange.baseMipLevel = 0;
    vci.subresourceRange.levelCount = mipLevels;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount = 1;

    VkImageView view = VK_NULL_HANDLE;
    vkCheck(vkCreateImageView(dev, &vci, nullptr, &view), "vkCreateImageView");
    return view;
}
