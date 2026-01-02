#pragma once
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

[[noreturn]] void vkFail(const char* where, VkResult r);
void vkCheck(VkResult r, const char* where);

std::vector<uint8_t> readFile(const char* path);

uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeBits, VkMemoryPropertyFlags props);

void createBuffer(VkDevice dev,
                  VkPhysicalDevice phys,
                  VkDeviceSize size,
                  VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags memFlags,
                  VkBuffer& outBuf,
                  VkDeviceMemory& outMem,
                  const char* debugWhere = "vkCreateBuffer");

void* mapMemory(VkDevice dev, VkDeviceMemory mem, VkDeviceSize size, VkDeviceSize offset = 0);

void createImage2D(VkDevice dev,
                   VkPhysicalDevice phys,
                   const VkImageCreateInfo& info,
                   VkMemoryPropertyFlags memFlags,
                   VkImage& outImage,
                   VkDeviceMemory& outMem,
                   const char* debugWhere = "vkCreateImage");

VkImageView createImageView2D(VkDevice dev, VkImage image, VkFormat format, VkImageAspectFlags aspect, uint32_t mipLevels = 1);
