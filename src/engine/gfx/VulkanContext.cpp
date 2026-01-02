#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "VulkanContext.hpp"
#include "VulkanHelpers.hpp"
#include "engine/core/Log.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

void VulkanContext::initWindow(int w, int h, const char* title)
{
    if (!glfwInit())
        throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    win = glfwCreateWindow(w, h, title, nullptr, nullptr);
    if (!win)
        throw std::runtime_error("glfwCreateWindow failed");

    glfwSetWindowUserPointer(win, this);
    glfwSetFramebufferSizeCallback(win, [](GLFWwindow* w, int, int) {
        auto* self = reinterpret_cast<VulkanContext*>(glfwGetWindowUserPointer(w));
        if (self)
            self->requestSwapchainRebuild();
    });
}

bool VulkanContext::shouldClose() const
{
    return glfwWindowShouldClose(win);
}
void VulkanContext::pollEvents() const
{
    glfwPollEvents();
}
void VulkanContext::deviceWaitIdle() const
{
    if (dev)
        vkDeviceWaitIdle(dev);
}

void VulkanContext::initVulkan()
{
    createInstance();

    createSurface();

    pickPhysicalDevice();
    createDevice();
    loadDeviceFunctionPointers();

    createOrResizeSwapchain();
    createCommands();
    createSync();
}

void VulkanContext::requestSwapchainRebuild()
{
    framebufferResized = true;
}

void VulkanContext::recreateSwapchain()
{
    framebufferResized = false;

    int w = 0, h = 0;
    glfwGetFramebufferSize(win, &w, &h);
    while (w == 0 || h == 0) {
        glfwWaitEvents();
        glfwGetFramebufferSize(win, &w, &h);
    }

    vkDeviceWaitIdle(dev);
    cleanupSwapchain();
    createOrResizeSwapchain();
    swapchainGen++;
}

void VulkanContext::createOrResizeSwapchain()
{
    createSwapchain();
    createDepthResources();
    if (!useDynamicRendering) {
        createRenderPass();
        createFramebuffers();
    }
}

void VulkanContext::createSurface()
{
    vkCheck((VkResult)glfwCreateWindowSurface(inst, win, nullptr, &surf), "glfwCreateWindowSurface");
}

void VulkanContext::shutdown()
{
    if (dev)
        vkDeviceWaitIdle(dev);

    for (uint32_t i = 0; i < MAX_FRAMES; i++)
        frameDeletion[i].flush();
    deviceDeletion.flush();

    for (auto& img : transientImagesFree) {
        if (img.view)
            vkDestroyImageView(dev, img.view, nullptr);
        if (img.image)
            vkDestroyImage(dev, img.image, nullptr);
        if (img.memory)
            vkFreeMemory(dev, img.memory, nullptr);
    }
    transientImagesFree.clear();
    for (uint32_t i = 0; i < MAX_FRAMES; ++i) {
        for (auto& img : transientImagesInFlight[i]) {
            if (img.view)
                vkDestroyImageView(dev, img.view, nullptr);
            if (img.image)
                vkDestroyImage(dev, img.image, nullptr);
            if (img.memory)
                vkFreeMemory(dev, img.memory, nullptr);
        }
        transientImagesInFlight[i].clear();
    }

    for (auto& b : transientBuffersFree) {
        if (b.buffer)
            vkDestroyBuffer(dev, b.buffer, nullptr);
        if (b.memory)
            vkFreeMemory(dev, b.memory, nullptr);
    }
    transientBuffersFree.clear();
    for (uint32_t i = 0; i < MAX_FRAMES; ++i) {
        for (auto& b : transientBuffersInFlight[i]) {
            if (b.buffer)
                vkDestroyBuffer(dev, b.buffer, nullptr);
            if (b.memory)
                vkFreeMemory(dev, b.memory, nullptr);
        }
        transientBuffersInFlight[i].clear();
    }

    cleanupSwapchain();

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        if (imageAvailable[i])
            vkDestroySemaphore(dev, imageAvailable[i], nullptr);
        if (renderFinished[i])
            vkDestroySemaphore(dev, renderFinished[i], nullptr);
        if (inFlight[i])
            vkDestroyFence(dev, inFlight[i], nullptr);
    }

    if (cmdPool)
        vkDestroyCommandPool(dev, cmdPool, nullptr);
    if (dev)
        vkDestroyDevice(dev, nullptr);
    if (surf)
        vkDestroySurfaceKHR(inst, surf, nullptr);
    if (debugMessenger && pfnDestroyDebugUtilsMessenger)
        pfnDestroyDebugUtilsMessenger(inst, debugMessenger, nullptr);
    if (inst)
        vkDestroyInstance(inst, nullptr);

    if (win) {
        glfwDestroyWindow(win);
        win = nullptr;
        glfwTerminate();
    }
}

void VulkanContext::cleanupSwapchain()
{
    for (auto fb : framebuffers)
        vkDestroyFramebuffer(dev, fb, nullptr);
    framebuffers.clear();

    if (rp) {
        vkDestroyRenderPass(dev, rp, nullptr);
        rp = {};
    }

    if (depthIv)
        vkDestroyImageView(dev, depthIv, nullptr);
    if (depthImg)
        vkDestroyImage(dev, depthImg, nullptr);
    if (depthMem)
        vkFreeMemory(dev, depthMem, nullptr);
    depthIv = {};
    depthImg = {};
    depthMem = {};
    depthFmt = VK_FORMAT_UNDEFINED;
    depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    for (auto v : swapViews)
        vkDestroyImageView(dev, v, nullptr);
    swapViews.clear();
    swapImages.clear();
    swapImageLayouts.clear();

    if (swap)
        vkDestroySwapchainKHR(dev, swap, nullptr);
    swap = {};
}

void VulkanContext::createInstance()
{
    validationEnabled = false;
#if !defined(NDEBUG)
    validationEnabled = true;
#endif

    auto hasInstanceLayer = [](const char* name) -> bool {
        uint32_t count = 0;
        if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0)
            return false;
        std::vector<VkLayerProperties> props(count);
        if (vkEnumerateInstanceLayerProperties(&count, props.data()) != VK_SUCCESS)
            return false;
        for (const auto& p : props)
            if (std::strcmp(p.layerName, name) == 0)
                return true;
        return false;
    };

    auto hasInstanceExtension = [](const char* name) -> bool {
        uint32_t count = 0;
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS || count == 0)
            return false;
        std::vector<VkExtensionProperties> props(count);
        if (vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data()) != VK_SUCCESS)
            return false;
        for (const auto& p : props)
            if (std::strcmp(p.extensionName, name) == 0)
                return true;
        return false;
    };

    uint32_t instanceApi = VK_API_VERSION_1_0;
#if defined(VK_VERSION_1_1)
    vkEnumerateInstanceVersion(&instanceApi);
#endif
    const uint32_t wantedApi = VK_API_VERSION_1_3;

    CFGC_LOGF("Vulkan loader API: %u.%u.%u (requesting %u.%u.%u)", VK_VERSION_MAJOR(instanceApi), VK_VERSION_MINOR(instanceApi),
              VK_VERSION_PATCH(instanceApi), VK_VERSION_MAJOR(wantedApi), VK_VERSION_MINOR(wantedApi), VK_VERSION_PATCH(wantedApi));
    const uint32_t apiVersion = std::min(instanceApi, wantedApi);

    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "CSLike";
    ai.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    ai.pEngineName = "CSLike";
    ai.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    ai.apiVersion = apiVersion;

    uint32_t extCount = 0;
    const char** exts = glfwGetRequiredInstanceExtensions(&extCount);
    std::vector<const char*> extensions(exts, exts + extCount);

    std::vector<const char*> layers;
    if (validationEnabled) {
        const bool hasLayer = hasInstanceLayer("VK_LAYER_KHRONOS_validation");
        const bool hasDebugUtils = hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        if (hasLayer && hasDebugUtils) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        } else {
            validationEnabled = false;
        }
    }

    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledLayerCount = (uint32_t)layers.size();
    ici.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
    ici.enabledExtensionCount = (uint32_t)extensions.size();
    ici.ppEnabledExtensionNames = extensions.data();

    vkCheck(vkCreateInstance(&ici, nullptr, &inst), "vkCreateInstance");

    pfnCreateDebugUtilsMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst, "vkCreateDebugUtilsMessengerEXT");
    pfnDestroyDebugUtilsMessenger = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(inst, "vkDestroyDebugUtilsMessengerEXT");
    pfnCmdBeginLabel = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(inst, "vkCmdBeginDebugUtilsLabelEXT");
    pfnCmdEndLabel = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(inst, "vkCmdEndDebugUtilsLabelEXT");
    pfnSetObjectName = (PFN_vkSetDebugUtilsObjectNameEXT)vkGetInstanceProcAddr(inst, "vkSetDebugUtilsObjectNameEXT");

    if (validationEnabled && pfnCreateDebugUtilsMessenger && pfnDestroyDebugUtilsMessenger) {
        VkDebugUtilsMessengerCreateInfoEXT mci{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
        mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        mci.pfnUserCallback = [](VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                 const VkDebugUtilsMessengerCallbackDataEXT* cb, void*) -> VkBool32 {
            const char* pfx = (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "[VK ERROR]" : "[VK WARN]";
            CFGC_LOGF("%s %s", pfx, cb && cb->pMessage ? cb->pMessage : "(no message)");
            return VK_FALSE;
        };
        vkCheck(pfnCreateDebugUtilsMessenger(inst, &mci, nullptr, &debugMessenger), "vkCreateDebugUtilsMessengerEXT");
    }
}

void VulkanContext::pickPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(inst, &count, nullptr);
    if (!count)
        throw std::runtime_error("No Vulkan devices");
    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(inst, &count, devs.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    uint32_t bestFamily = 0;
    int bestScore = -1;

    for (auto d : devs) {
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extCount, exts.data());
        bool hasSwapchain = false;
        for (auto& e : exts)
            if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                hasSwapchain = true;
        if (!hasSwapchain)
            continue;

        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qcount, qprops.data());

        for (uint32_t i = 0; i < qcount; i++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surf, &present);
            if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                VkPhysicalDeviceProperties props{};
                vkGetPhysicalDeviceProperties(d, &props);

                int score = 0;
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                    score += 1000;
                score += int(VK_VERSION_MAJOR(props.apiVersion) * 100 + VK_VERSION_MINOR(props.apiVersion) * 10);

                score += int(props.limits.maxImageDimension2D / 4096);

                if (score > bestScore) {
                    bestScore = score;
                    best = d;
                    bestFamily = i;
                }
                break;
            }
        }
    }

    if (!best)
        throw std::runtime_error("No suitable physical device found");
    phys = best;
    gfxFamily = bestFamily;
}

void VulkanContext::createDevice()
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys, &props);

    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mem);
    VkPhysicalDeviceFeatures feats{};
    vkGetPhysicalDeviceFeatures(phys, &feats);

    auto vMajor = VK_VERSION_MAJOR(props.apiVersion);
    auto vMinor = VK_VERSION_MINOR(props.apiVersion);
    auto vPatch = VK_VERSION_PATCH(props.apiVersion);

    uint64_t deviceLocalBytes = 0;
    for (uint32_t i = 0; i < mem.memoryHeapCount; ++i) {
        if (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            if (mem.memoryHeaps[i].size > deviceLocalBytes)
                deviceLocalBytes = mem.memoryHeaps[i].size;
    }

    CFGC_LOGF("GPU: %s", props.deviceName);
    CFGC_LOGF("Vulkan API: %u.%u.%u  vendor:0x%04x device:0x%04x", vMajor, vMinor, vPatch, props.vendorID, props.deviceID);
    CFGC_LOGF("VRAM (device-local heap): %.0f MiB", deviceLocalBytes / 1048576.0);
    CFGC_LOGF("Features: geometryShader=%u samplerAnisotropy=%u fillModeNonSolid=%u wideLines=%u", feats.geometryShader,
              feats.samplerAnisotropy, feats.fillModeNonSolid, feats.wideLines);

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> extProps(extCount);
    vkEnumerateDeviceExtensionProperties(phys, nullptr, &extCount, extProps.data());
    auto hasExt = [&](const char* name) {
        for (auto& e : extProps)
            if (std::strcmp(e.extensionName, name) == 0)
                return true;
        return false;
    };

    useDynamicRendering = (props.apiVersion >= VK_API_VERSION_1_3) || hasExt(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    useSync2 = (props.apiVersion >= VK_API_VERSION_1_3) || hasExt(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);

    CFGC_LOGF("Dynamic Rendering: %s", useDynamicRendering ? "enabled" : "disabled");
    CFGC_LOGF("Synchronization2: %s", useSync2 ? "enabled" : "disabled");

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = gfxFamily;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    std::vector<const char*> exts;
    exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    if (props.apiVersion < VK_API_VERSION_1_2)
        exts.push_back(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME);

    if (useDynamicRendering && props.apiVersion < VK_API_VERSION_1_3)
        exts.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    if (useSync2 && props.apiVersion < VK_API_VERSION_1_3)
        exts.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);

    VkPhysicalDeviceFeatures features{};

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynFeat{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR };
    dynFeat.dynamicRendering = useDynamicRendering ? VK_TRUE : VK_FALSE;

    VkPhysicalDeviceSynchronization2FeaturesKHR sync2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR };
    sync2.synchronization2 = useSync2 ? VK_TRUE : VK_FALSE;
    sync2.pNext = useDynamicRendering ? &dynFeat : nullptr;

    VkPhysicalDeviceShaderDrawParametersFeatures shaderParams{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES };
    shaderParams.shaderDrawParameters = VK_TRUE;
    shaderParams.pNext = &sync2;

    VkPhysicalDeviceFeatures2 feats2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    feats2.features = features;
    feats2.pNext = &shaderParams;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.pNext = &feats2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.pEnabledFeatures = nullptr;
    dci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    dci.ppEnabledExtensionNames = exts.data();

    vkCheck(vkCreateDevice(phys, &dci, nullptr, &dev), "vkCreateDevice");
    vkGetDeviceQueue(dev, gfxFamily, 0, &gfxQ);
}

void VulkanContext::loadDeviceFunctionPointers()
{
    if (useDynamicRendering) {
        pfnCmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetDeviceProcAddr(dev, "vkCmdBeginRendering"));
        if (!pfnCmdBeginRendering)
            pfnCmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetDeviceProcAddr(dev, "vkCmdBeginRenderingKHR"));

        pfnCmdEndRendering = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetDeviceProcAddr(dev, "vkCmdEndRendering"));
        if (!pfnCmdEndRendering)
            pfnCmdEndRendering = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetDeviceProcAddr(dev, "vkCmdEndRenderingKHR"));
    }

    pfnCmdDrawIndexedIndirectCount =
        reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCount>(vkGetDeviceProcAddr(dev, "vkCmdDrawIndexedIndirectCount"));
    pfnCmdDrawIndexedIndirectCountKHR =
        reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCountKHR>(vkGetDeviceProcAddr(dev, "vkCmdDrawIndexedIndirectCountKHR"));

    if (useSync2) {
        pfnCmdPipelineBarrier2 = reinterpret_cast<PFN_vkCmdPipelineBarrier2>(vkGetDeviceProcAddr(dev, "vkCmdPipelineBarrier2"));
        pfnCmdPipelineBarrier2KHR = reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(vkGetDeviceProcAddr(dev, "vkCmdPipelineBarrier2KHR"));
    }
}

void VulkanContext::createSwapchain()
{
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surf, &caps);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surf, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surf, &fmtCount, fmts.data());

    VkSurfaceFormatKHR chosen = fmts[0];

    for (auto& f : fmts) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }

    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surf, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> pms(pmCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surf, &pmCount, pms.data());
    VkPresentModeKHR pm = VK_PRESENT_MODE_IMMEDIATE_KHR;
    for (auto m : pms)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR)
            pm = m;

    swapExtent = caps.currentExtent;
    if (swapExtent.width == 0xFFFFFFFF) {
        int w, h;
        glfwGetFramebufferSize(win, &w, &h);
        swapExtent = { (uint32_t)w, (uint32_t)h };
        swapExtent.width = std::clamp(swapExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        swapExtent.height = std::clamp(swapExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surf;
    sci.minImageCount = imageCount;
    sci.imageFormat = chosen.format;
    sci.imageColorSpace = chosen.colorSpace;
    sci.imageExtent = swapExtent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = pm;
    sci.clipped = VK_TRUE;

    vkCheck(vkCreateSwapchainKHR(dev, &sci, nullptr, &swap), "vkCreateSwapchainKHR");
    swapFormat = chosen.format;

    uint32_t scCount = 0;
    vkGetSwapchainImagesKHR(dev, swap, &scCount, nullptr);
    swapImages.resize(scCount);
    vkGetSwapchainImagesKHR(dev, swap, &scCount, swapImages.data());

    swapViews.resize(scCount);
    for (uint32_t i = 0; i < scCount; i++) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = swapImages[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = swapFormat;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.layerCount = 1;
        vkCheck(vkCreateImageView(dev, &vci, nullptr, &swapViews[i]), "vkCreateImageView");
    }

    swapImageLayouts.assign(scCount, VK_IMAGE_LAYOUT_UNDEFINED);
}

void VulkanContext::createDepthResources()
{
    const VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    depthFmt = VK_FORMAT_UNDEFINED;
    for (VkFormat f : candidates) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(phys, f, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthFmt = f;
            break;
        }
    }
    if (depthFmt == VK_FORMAT_UNDEFINED)
        throw std::runtime_error("No supported depth format");

    VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = depthFmt;
    ici.extent = { swapExtent.width, swapExtent.height, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCheck(vkCreateImage(dev, &ici, nullptr, &depthImg), "vkCreateImage(depth)");

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(dev, depthImg, &req);
    VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = findMemoryType(phys, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkCheck(vkAllocateMemory(dev, &mai, nullptr, &depthMem), "vkAllocateMemory(depth)");
    vkCheck(vkBindImageMemory(dev, depthImg, depthMem, 0), "vkBindImageMemory(depth)");

    VkImageViewCreateInfo vci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    vci.image = depthImg;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = depthFmt;
    vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vci.subresourceRange.levelCount = 1;
    vci.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(dev, &vci, nullptr, &depthIv), "vkCreateImageView(depth)");

    depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
}

void VulkanContext::createRenderPass()
{
    VkAttachmentDescription attachments[2]{};

    attachments[0].format = swapFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    attachments[1].format = depthFmt;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = 2;
    rpci.pAttachments = attachments;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    vkCheck(vkCreateRenderPass(dev, &rpci, nullptr, &rp), "vkCreateRenderPass");
}

void VulkanContext::createFramebuffers()
{
    framebuffers.resize(swapViews.size());
    for (size_t i = 0; i < swapViews.size(); i++) {
        VkImageView att[] = { swapViews[i], depthIv };
        VkFramebufferCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fci.renderPass = rp;
        fci.attachmentCount = 2;
        fci.pAttachments = att;
        fci.width = swapExtent.width;
        fci.height = swapExtent.height;
        fci.layers = 1;
        vkCheck(vkCreateFramebuffer(dev, &fci, nullptr, &framebuffers[i]), "vkCreateFramebuffer");
    }
}

void VulkanContext::createCommands()
{
    VkCommandPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = gfxFamily;
    vkCheck(vkCreateCommandPool(dev, &pci, nullptr, &cmdPool), "vkCreateCommandPool");

    cmdBuffers.resize(MAX_FRAMES);
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = (uint32_t)cmdBuffers.size();
    vkCheck(vkAllocateCommandBuffers(dev, &ai, cmdBuffers.data()), "vkAllocateCommandBuffers");
}

void VulkanContext::createSync()
{
    VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < MAX_FRAMES; i++) {
        vkCheck(vkCreateSemaphore(dev, &sci, nullptr, &imageAvailable[i]), "vkCreateSemaphore");
        vkCheck(vkCreateSemaphore(dev, &sci, nullptr, &renderFinished[i]), "vkCreateSemaphore");
        vkCheck(vkCreateFence(dev, &fci, nullptr, &inFlight[i]), "vkCreateFence");
    }
}

void VulkanContext::cmdDrawIndexedIndirectCount(VkCommandBuffer cmd,
                                                VkBuffer indirectBuffer,
                                                VkDeviceSize indirectOffset,
                                                VkBuffer countBuffer,
                                                VkDeviceSize countOffset,
                                                uint32_t maxDrawCount,
                                                uint32_t stride) const
{
    if (pfnCmdDrawIndexedIndirectCount) {
        pfnCmdDrawIndexedIndirectCount(cmd, indirectBuffer, indirectOffset, countBuffer, countOffset, maxDrawCount, stride);
        return;
    }
    if (pfnCmdDrawIndexedIndirectCountKHR) {
        pfnCmdDrawIndexedIndirectCountKHR(cmd, indirectBuffer, indirectOffset, countBuffer, countOffset, maxDrawCount, stride);
        return;
    }
    vkCmdDrawIndexedIndirect(cmd, indirectBuffer, indirectOffset, maxDrawCount, stride);
}

VkCommandBuffer VulkanContext::beginFrame()
{
    if (framebufferResized) {
        return VK_NULL_HANDLE;
    }

    VkFence fence = inFlight[frameIndex];
    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);

    if (!transientImagesInFlight[frameIndex].empty()) {
        transientImagesFree.insert(transientImagesFree.end(), transientImagesInFlight[frameIndex].begin(),
                                   transientImagesInFlight[frameIndex].end());
        transientImagesInFlight[frameIndex].clear();
    }
    if (!transientBuffersInFlight[frameIndex].empty()) {
        transientBuffersFree.insert(transientBuffersFree.end(), transientBuffersInFlight[frameIndex].begin(),
                                    transientBuffersInFlight[frameIndex].end());
        transientBuffersInFlight[frameIndex].clear();
    }

    frameDeletion[frameIndex].flush();
    vkResetFences(dev, 1, &fence);

    VkResult r = vkAcquireNextImageKHR(dev, swap, UINT64_MAX, imageAvailable[frameIndex], VK_NULL_HANDLE, &acquiredImage);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_ERROR_SURFACE_LOST_KHR) {
        framebufferResized = true;
        return VK_NULL_HANDLE;
    }
    if (r == VK_SUBOPTIMAL_KHR) {
        framebufferResized = true;
    } else if (r != VK_SUCCESS) {
        vkFail("vkAcquireNextImageKHR", r);
    }

    VkCommandBuffer cmd = cmdBuffers[frameIndex];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd, &bi), "vkBeginCommandBuffer");
    return cmd;
}

void VulkanContext::beginMainPass(VkCommandBuffer cmd)
{
    if (useDynamicRendering) {
        VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        barrier.oldLayout = swapImageLayouts.empty() ? VK_IMAGE_LAYOUT_UNDEFINED : swapImageLayouts[acquiredImage];
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = swapImages[acquiredImage];
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                             nullptr, 1, &barrier);

        if (!swapImageLayouts.empty())
            swapImageLayouts[acquiredImage] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkImageMemoryBarrier dbar{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        dbar.oldLayout = depthLayout;
        dbar.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        dbar.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dbar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dbar.image = depthImg;
        dbar.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        dbar.subresourceRange.levelCount = 1;
        dbar.subresourceRange.layerCount = 1;
        dbar.srcAccessMask = 0;
        dbar.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr,
                             1, &dbar);

        depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkClearValue cclear{};
        cclear.color = { { 0.05f, 0.06f, 0.08f, 1.0f } };

        VkClearValue dclear{};
        dclear.depthStencil = { 1.0f, 0 };

        VkRenderingAttachmentInfoKHR colorAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
        colorAtt.imageView = swapViews[acquiredImage];
        colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.clearValue = cclear;

        VkRenderingAttachmentInfoKHR depthAtt{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
        depthAtt.imageView = depthIv;
        depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.clearValue = dclear;

        VkRenderingInfoKHR ri{ VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
        ri.renderArea = VkRect2D{ { 0, 0 }, swapExtent };
        ri.layerCount = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments = &colorAtt;
        ri.pDepthAttachment = &depthAtt;

        pfnCmdBeginRendering(cmd, &ri);
        return;
    }

    VkClearValue clears[2]{};
    clears[0].color = { { 0.05f, 0.06f, 0.08f, 1.0f } };
    clears[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rbi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rbi.renderPass = rp;
    rbi.framebuffer = framebuffers[acquiredImage];
    rbi.renderArea.extent = swapExtent;
    rbi.clearValueCount = 2;
    rbi.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
}

void VulkanContext::endMainPass(VkCommandBuffer cmd)
{
    if (useDynamicRendering) {
        pfnCmdEndRendering(cmd);
        return;
    }
    vkCmdEndRenderPass(cmd);
}

void VulkanContext::endFrame()
{
    VkCommandBuffer cmd = cmdBuffers[frameIndex];

    vkCheck(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");

    VkSemaphore waitSem = imageAvailable[frameIndex];
    VkSemaphore sigSem = renderFinished[frameIndex];
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &waitSem;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &sigSem;

    vkCheck(vkQueueSubmit(gfxQ, 1, &si, inFlight[frameIndex]), "vkQueueSubmit");

    VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &sigSem;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swap;
    pi.pImageIndices = &acquiredImage;
    VkResult pr = vkQueuePresentKHR(gfxQ, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR || pr == VK_ERROR_SURFACE_LOST_KHR) {
        framebufferResized = true;
    } else {
        vkCheck(pr, "vkQueuePresentKHR");
    }

    frameIndex = (frameIndex + 1) % MAX_FRAMES;
}

void VulkanContext::cmdBeginLabel(VkCommandBuffer cmd, const char* name) const
{
    if (!pfnCmdBeginLabel || !name)
        return;
    VkDebugUtilsLabelEXT label{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
    label.pLabelName = name;
    pfnCmdBeginLabel(cmd, &label);
}

void VulkanContext::cmdEndLabel(VkCommandBuffer cmd) const
{
    if (!pfnCmdEndLabel)
        return;
    pfnCmdEndLabel(cmd);
}

void VulkanContext::setObjectName(VkObjectType type, uint64_t handle, const char* name) const
{
    if (!pfnSetObjectName || !name || handle == 0)
        return;
    VkDebugUtilsObjectNameInfoEXT ni{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
    ni.objectType = type;
    ni.objectHandle = handle;
    ni.pObjectName = name;
    pfnSetObjectName(dev, &ni);
}

static VkPipelineStageFlags stage2ToStage1(VkPipelineStageFlags2 s)
{
    return static_cast<VkPipelineStageFlags>(s & 0xFFFFFFFFull);
}

static VkAccessFlags access2ToAccess1(VkAccessFlags2 a)
{
    return static_cast<VkAccessFlags>(a & 0xFFFFFFFFull);
}

void VulkanContext::cmdPipelineBarrier2(VkCommandBuffer cmd, const VkDependencyInfo& dep) const
{
    if (pfnCmdPipelineBarrier2) {
        pfnCmdPipelineBarrier2(cmd, &dep);
        return;
    }
    if (pfnCmdPipelineBarrier2KHR) {
        pfnCmdPipelineBarrier2KHR(cmd, &dep);
        return;
    }

    VkPipelineStageFlags srcStages = 0;
    VkPipelineStageFlags dstStages = 0;

    std::vector<VkMemoryBarrier> mem;
    mem.reserve(dep.memoryBarrierCount);
    for (uint32_t i = 0; i < dep.memoryBarrierCount; ++i) {
        const auto& b2 = dep.pMemoryBarriers[i];
        VkMemoryBarrier b{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        b.srcAccessMask = access2ToAccess1(b2.srcAccessMask);
        b.dstAccessMask = access2ToAccess1(b2.dstAccessMask);
        mem.push_back(b);
        srcStages |= stage2ToStage1(b2.srcStageMask);
        dstStages |= stage2ToStage1(b2.dstStageMask);
    }

    std::vector<VkBufferMemoryBarrier> buf;
    buf.reserve(dep.bufferMemoryBarrierCount);
    for (uint32_t i = 0; i < dep.bufferMemoryBarrierCount; ++i) {
        const auto& b2 = dep.pBufferMemoryBarriers[i];
        VkBufferMemoryBarrier b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
        b.srcAccessMask = access2ToAccess1(b2.srcAccessMask);
        b.dstAccessMask = access2ToAccess1(b2.dstAccessMask);
        b.srcQueueFamilyIndex = b2.srcQueueFamilyIndex;
        b.dstQueueFamilyIndex = b2.dstQueueFamilyIndex;
        b.buffer = b2.buffer;
        b.offset = b2.offset;
        b.size = b2.size;
        buf.push_back(b);
        srcStages |= stage2ToStage1(b2.srcStageMask);
        dstStages |= stage2ToStage1(b2.dstStageMask);
    }

    std::vector<VkImageMemoryBarrier> img;
    img.reserve(dep.imageMemoryBarrierCount);
    for (uint32_t i = 0; i < dep.imageMemoryBarrierCount; ++i) {
        const auto& b2 = dep.pImageMemoryBarriers[i];
        VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.srcAccessMask = access2ToAccess1(b2.srcAccessMask);
        b.dstAccessMask = access2ToAccess1(b2.dstAccessMask);
        b.oldLayout = b2.oldLayout;
        b.newLayout = b2.newLayout;
        b.srcQueueFamilyIndex = b2.srcQueueFamilyIndex;
        b.dstQueueFamilyIndex = b2.dstQueueFamilyIndex;
        b.image = b2.image;
        b.subresourceRange = b2.subresourceRange;
        img.push_back(b);
        srcStages |= stage2ToStage1(b2.srcStageMask);
        dstStages |= stage2ToStage1(b2.dstStageMask);
    }

    if (srcStages == 0)
        srcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (dstStages == 0)
        dstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    vkCmdPipelineBarrier(cmd, srcStages, dstStages, 0, (uint32_t)mem.size(), mem.data(), (uint32_t)buf.size(), buf.data(),
                         (uint32_t)img.size(), img.data());
}

static bool transientImageKeyMatch(const VulkanContext::TransientImage2D& a,
                                   VkFormat format,
                                   VkExtent2D extent,
                                   VkImageUsageFlags usage,
                                   VkImageAspectFlags aspect)
{
    return a.format == format && a.extent.width == extent.width && a.extent.height == extent.height && a.usage == usage &&
           a.aspect == aspect;
}

VulkanContext::TransientImage2D VulkanContext::acquireTransientImage2D(std::string_view debugName,
                                                                       VkFormat format,
                                                                       VkExtent2D extent,
                                                                       VkImageUsageFlags usage,
                                                                       VkImageAspectFlags aspect)
{
    for (size_t i = 0; i < transientImagesFree.size(); ++i) {
        if (!transientImageKeyMatch(transientImagesFree[i], format, extent, usage, aspect))
            continue;
        auto r = transientImagesFree[i];
        transientImagesFree[i] = transientImagesFree.back();
        transientImagesFree.pop_back();
        if (!debugName.empty()) {
            setObjectName(VK_OBJECT_TYPE_IMAGE, (uint64_t)r.image, std::string(debugName).c_str());
            setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)r.view, std::string(debugName).c_str());
        }
        return r;
    }

    TransientImage2D r{};
    r.format = format;
    r.extent = extent;
    r.usage = usage;
    r.aspect = aspect;

    VkImageCreateInfo ici{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = VkExtent3D{ extent.width, extent.height, 1 };
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    createImage2D(dev, phys, ici, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, r.image, r.memory, "acquireTransientImage2D");
    r.view = createImageView2D(dev, r.image, format, aspect, 1);
    if (!debugName.empty()) {
        setObjectName(VK_OBJECT_TYPE_IMAGE, (uint64_t)r.image, std::string(debugName).c_str());
        setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, (uint64_t)r.view, std::string(debugName).c_str());
    }
    return r;
}

void VulkanContext::retireTransientImage2D(TransientImage2D img)
{
    if (!img.image)
        return;
    transientImagesInFlight[frameIndex].push_back(img);
}

VulkanContext::TransientBuffer VulkanContext::acquireTransientBuffer(std::string_view debugName,
                                                                     VkDeviceSize size,
                                                                     VkBufferUsageFlags usage,
                                                                     VkMemoryPropertyFlags memFlags)
{
    for (size_t i = 0; i < transientBuffersFree.size(); ++i) {
        auto& f = transientBuffersFree[i];
        if (f.size == size && f.usage == usage && f.memFlags == memFlags) {
            auto r = f;
            transientBuffersFree[i] = transientBuffersFree.back();
            transientBuffersFree.pop_back();
            if (!debugName.empty())
                setObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)r.buffer, std::string(debugName).c_str());
            return r;
        }
    }

    TransientBuffer r{};
    r.size = size;
    r.usage = usage;
    r.memFlags = memFlags;
    createBuffer(dev, phys, size, usage, memFlags, r.buffer, r.memory, "acquireTransientBuffer");
    if (!debugName.empty())
        setObjectName(VK_OBJECT_TYPE_BUFFER, (uint64_t)r.buffer, std::string(debugName).c_str());
    return r;
}

void VulkanContext::retireTransientBuffer(TransientBuffer buf)
{
    if (!buf.buffer)
        return;
    transientBuffersInFlight[frameIndex].push_back(buf);
}
