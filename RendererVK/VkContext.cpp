#include "VkContext.h"
#include "VkWindow.h"
#include <iostream>
#include "Logger.h"
#include <set>
#include <cstring>
#include <algorithm>

// ============================================================================
// Debug Callback
// ============================================================================
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        LOG_STREAM_ERROR("VkValidation") << data->pMessage;
    return VK_FALSE;
}

static VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pMessenger)
{
    if (const auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT")))
        return fn(instance, pCreateInfo, pAllocator, pMessenger);
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks* pAllocator)
{
    if (const auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT")))
        fn(instance, messenger, pAllocator);
}

// ============================================================================
void VkContext::Init(const VkWindow& window)
{
    CreateInstance();
    SetupDebugMessenger();
    m_surface = window.CreateSurface(m_instance);
    PickPhysicalDevice();
    CreateLogicalDevice();
}

void VkContext::Destroy()
{
    if (m_device)
    {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_surface)
    {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_debugMessenger)
    {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_instance)
    {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

// ============================================================================
// Instance
// ============================================================================
void VkContext::CreateInstance()
{
    if (TitusVkGraphics::COMPONENT_CONFIG::ENABLE_VALIDATION_LAYER && !CheckValidationLayerSupport())
    {
            LOG_STREAM_ERROR("VkContext") << "Validation layer requested but not available";
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "TitusVkApp";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "TitusVkRenderer";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &appInfo;

    auto extensions = GetRequiredInstanceExtensions();
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT dbgCi{};
    if (TitusVkGraphics::COMPONENT_CONFIG::ENABLE_VALIDATION_LAYER)
    {
        ci.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        ci.ppEnabledLayerNames = m_validationLayers.data();

        dbgCi.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dbgCi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dbgCi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dbgCi.pfnUserCallback = DebugCallback;
        ci.pNext = &dbgCi;
    }

    VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance));
}

void VkContext::SetupDebugMessenger()
{
    if (!TitusVkGraphics::COMPONENT_CONFIG::ENABLE_VALIDATION_LAYER) return;

    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = DebugCallback;

    CreateDebugUtilsMessengerEXT(m_instance, &ci, nullptr, &m_debugMessenger);
}

bool VkContext::CheckValidationLayerSupport() const
{
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> available(count);
    vkEnumerateInstanceLayerProperties(&count, available.data());

    for (auto* needed : m_validationLayers)
    {
        bool found = false;
        for (const auto& a : available)
        {
            if (std::strcmp(needed, a.layerName) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

std::vector<const char*> VkContext::GetRequiredInstanceExtensions() const
{
    uint32_t glfwCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwCount);
    if (TitusVkGraphics::COMPONENT_CONFIG::ENABLE_VALIDATION_LAYER)
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    return extensions;
}

// ============================================================================
// Physical / Logical Device
// ============================================================================
void VkContext::PickPhysicalDevice()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0)
    {
            LOG_STREAM_ERROR("VkContext") << "No Vulkan-capable GPU found";
        std::abort();
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    for (auto d : devices)
    {
        if (IsDeviceSuitable(d))
        {
            m_physicalDevice = d;
            m_queueFamilyIndices = FindQueueFamilies(d);
            break;
        }
    }
    if (m_physicalDevice == VK_NULL_HANDLE)
    {
            LOG_STREAM_ERROR("VkContext") << "No suitable GPU found";
        std::abort();
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
        LOG_STREAM_INFO("VkContext") << "Selected GPU: " << props.deviceName;

#if defined(RENDERER_ENABLE_RAY_TRACING)
    // 光追探测：不影响设备选择结果，仅记录能力。缺失时安全降级为不支持。
    m_supportsRayTracing = CheckRayTracingSupport(m_physicalDevice);
    m_supportsRayQuery   = m_supportsRayTracing; // 当前路线 A 以 ray_query 为准
    if (m_supportsRayTracing)
    {
        // 探测 RT 管线（路线 B）能力，与 ray query 独立。
        m_supportsRayTracingPipeline = CheckRayTracingPipelineSupport(m_physicalDevice);

        // 查询 AS/scratch 对齐等属性（通过 Properties2 链）；支持 RT 管线时
        // 一并查询 SBT handle 大小/对齐（VkPhysicalDeviceRayTracingPipelinePropertiesKHR）。
        m_accelStructProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        VkPhysicalDeviceProperties2 props2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
        props2.pNext = &m_accelStructProps;
        if (m_supportsRayTracingPipeline)
        {
            m_rtPipelineProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
            m_accelStructProps.pNext = &m_rtPipelineProps;
        }
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &props2);
        m_accelStructProps.pNext = nullptr; // 复位，避免悬挂指针
        LOG_STREAM_INFO("VkContext") << "Ray tracing (ray query) supported on this GPU"
            << (m_supportsRayTracingPipeline ? "; ray tracing pipeline supported" : "");
    }
    else
    {
        LOG_STREAM_INFO("VkContext") << "Ray tracing not supported on this GPU; running raster-only";
    }
#endif
}

bool VkContext::IsDeviceSuitable(VkPhysicalDevice device) const
{
    auto indices = FindQueueFamilies(device);
    if (!indices.IsComplete()) return false;
    if (!CheckDeviceExtensionSupport(device)) return false;

    // 检查 Swapchain 是否可用
    uint32_t fmtCount = 0, pmCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &fmtCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &pmCount, nullptr);
    return fmtCount > 0 && pmCount > 0;
}

bool VkContext::CheckDeviceExtensionSupport(VkPhysicalDevice device) const
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

    std::set<std::string> required(m_deviceExtensions.begin(), m_deviceExtensions.end());
    for (const auto& e : available) required.erase(e.extensionName);
    return required.empty();
}

TitusVkGraphics::QueueFamilyIndices VkContext::FindQueueFamilies(VkPhysicalDevice device) const
{
    TitusVkGraphics::QueueFamilyIndices indices{};

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (uint32_t i = 0; i < count; ++i)
    {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            indices.graphicsFamily = i;
        if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
            indices.computeFamily = i;
        if (families[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
            indices.transferFamily = i;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
        if (presentSupport) indices.presentFamily = i;

        if (indices.IsComplete()) break;
    }
    return indices;
}

void VkContext::CreateLogicalDevice()
{
    std::set<uint32_t> uniqueFamilies = {
        m_queueFamilyIndices.graphicsFamily,
        m_queueFamilyIndices.presentFamily
    };

    float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t fam : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = fam;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }

    // 现有光栅化特性（迁移自旧的 VkPhysicalDeviceFeatures，不得丢失）
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;
    features.fillModeNonSolid  = VK_TRUE;

    // 待启用的设备扩展列表：默认与改造前一致（仅 Swapchain）
    std::vector<const char*> enabledExtensions = m_deviceExtensions;

    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    ci.pQueueCreateInfos = queueInfos.data();

#if defined(RENDERER_ENABLE_RAY_TRACING)
    // ------------------------------------------------------------------
    // 光追路径：改用 VkPhysicalDeviceFeatures2 + pNext 链启用特性。
    //   现有 features 迁入 features2.features（不再使用 ci.pEnabledFeatures）。
    //   仅在探测到支持时追加光追扩展与特性，否则退回与改造前一致的行为。
    // ------------------------------------------------------------------
    VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    features2.features = features;

    VkPhysicalDeviceBufferDeviceAddressFeatures      bdaFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    VkPhysicalDeviceRayQueryFeaturesKHR              rqFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR    rtpFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };

    if (m_supportsRayTracing)
    {
        bdaFeatures.bufferDeviceAddress = VK_TRUE;
        asFeatures.accelerationStructure = VK_TRUE;
        rqFeatures.rayQuery = VK_TRUE;

        // 串联 pNext：features2 -> bda -> as -> rq
        bdaFeatures.pNext = &asFeatures;
        asFeatures.pNext  = &rqFeatures;
        features2.pNext   = &bdaFeatures;

        // 追加光追扩展
        enabledExtensions.insert(enabledExtensions.end(),
                                 m_rayTracingExtensions.begin(),
                                 m_rayTracingExtensions.end());

        // 路线 B：额外启用 RT 管线特性与扩展（仅在探测支持时）。
        if (m_supportsRayTracingPipeline)
        {
            rtpFeatures.rayTracingPipeline = VK_TRUE;
            rqFeatures.pNext = &rtpFeatures; // 挂到链尾
            enabledExtensions.insert(enabledExtensions.end(),
                                     m_rayTracingPipelineExtensions.begin(),
                                     m_rayTracingPipelineExtensions.end());
        }
    }

    ci.pNext            = &features2;
    ci.pEnabledFeatures = nullptr; // 使用 features2 时此字段必须为空
#else
    ci.pEnabledFeatures = &features;
#endif

    ci.enabledExtensionCount   = static_cast<uint32_t>(enabledExtensions.size());
    ci.ppEnabledExtensionNames = enabledExtensions.data();

    if (TitusVkGraphics::COMPONENT_CONFIG::ENABLE_VALIDATION_LAYER)
    {
        ci.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
        ci.ppEnabledLayerNames = m_validationLayers.data();
    }

    VK_CHECK(vkCreateDevice(m_physicalDevice, &ci, nullptr, &m_device));
    vkGetDeviceQueue(m_device, m_queueFamilyIndices.graphicsFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_queueFamilyIndices.presentFamily, 0, &m_presentQueue);

#if defined(RENDERER_ENABLE_RAY_TRACING)
    if (m_supportsRayTracing)
        LoadRayTracingFunctions();
#endif
}

// ============================================================================
// Queries & Helpers
// ============================================================================
TitusVkGraphics::SwapchainSupportDetails VkContext::QuerySwapchainSupport() const
{
    TitusVkGraphics::SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &details.capabilities);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, nullptr);
    if (fmtCount)
    {
        details.formats.resize(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &fmtCount, details.formats.data());
    }

    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmCount, nullptr);
    if (pmCount)
    {
        details.presentModes.resize(pmCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &pmCount, details.presentModes.data());
    }
    return details;
}

uint32_t VkContext::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const
{
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
            LOG_STREAM_ERROR("VkContext") << "FindMemoryType failed";
    return UINT32_MAX;
}

// ============================================================================
// 光追
// ============================================================================
#if defined(RENDERER_ENABLE_RAY_TRACING)
bool VkContext::CheckRayTracingSupport(VkPhysicalDevice device) const
{
    // 1) 全部光追扩展均可用
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

    std::set<std::string> required(m_rayTracingExtensions.begin(), m_rayTracingExtensions.end());
    for (const auto& e : available) required.erase(e.extensionName);
    if (!required.empty()) return false;

    // 2) 相关特性确实开启（通过 Features2 链查询）
    VkPhysicalDeviceRayQueryFeaturesKHR rqFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
    VkPhysicalDeviceBufferDeviceAddressFeatures bdaFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
    asFeatures.pNext  = &rqFeatures;
    bdaFeatures.pNext = &asFeatures;
    VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    features2.pNext = &bdaFeatures;
    vkGetPhysicalDeviceFeatures2(device, &features2);

    return bdaFeatures.bufferDeviceAddress == VK_TRUE
        && asFeatures.accelerationStructure == VK_TRUE
        && rqFeatures.rayQuery == VK_TRUE;
}

void VkContext::LoadRayTracingFunctions()
{
    auto load = [this](const char* name) {
        return vkGetDeviceProcAddr(m_device, name);
    };

    m_rtFunctions.vkGetAccelerationStructureBuildSizesKHR =
        reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(load("vkGetAccelerationStructureBuildSizesKHR"));
    m_rtFunctions.vkCreateAccelerationStructureKHR =
        reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(load("vkCreateAccelerationStructureKHR"));
    m_rtFunctions.vkDestroyAccelerationStructureKHR =
        reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(load("vkDestroyAccelerationStructureKHR"));
    m_rtFunctions.vkCmdBuildAccelerationStructuresKHR =
        reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(load("vkCmdBuildAccelerationStructuresKHR"));
    m_rtFunctions.vkGetAccelerationStructureDeviceAddressKHR =
        reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(load("vkGetAccelerationStructureDeviceAddressKHR"));
    m_rtFunctions.vkGetBufferDeviceAddressKHR =
        reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(load("vkGetBufferDeviceAddressKHR"));

    // 任一函数缺失则视为不支持，回退到光栅路径（安全降级）
    if (!m_rtFunctions.vkGetAccelerationStructureBuildSizesKHR
        || !m_rtFunctions.vkCreateAccelerationStructureKHR
        || !m_rtFunctions.vkDestroyAccelerationStructureKHR
        || !m_rtFunctions.vkCmdBuildAccelerationStructuresKHR
        || !m_rtFunctions.vkGetAccelerationStructureDeviceAddressKHR
        || !m_rtFunctions.vkGetBufferDeviceAddressKHR)
    {
        LOG_STREAM_ERROR("VkContext") << "Failed to load ray tracing device functions; disabling ray tracing";
        m_rtFunctions = RayTracingFunctions{};
        m_supportsRayTracing = false;
        m_supportsRayQuery   = false;
        m_supportsRayTracingPipeline = false;
        return;
    }

    // 路线 B：加载 RT 管线函数（仅在探测支持时）；缺失则仅关闭管线能力，
    // 不影响 ray query 路径。
    if (m_supportsRayTracingPipeline)
    {
        m_rtFunctions.vkCreateRayTracingPipelinesKHR =
            reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(load("vkCreateRayTracingPipelinesKHR"));
        m_rtFunctions.vkGetRayTracingShaderGroupHandlesKHR =
            reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(load("vkGetRayTracingShaderGroupHandlesKHR"));
        m_rtFunctions.vkCmdTraceRaysKHR =
            reinterpret_cast<PFN_vkCmdTraceRaysKHR>(load("vkCmdTraceRaysKHR"));

        if (!m_rtFunctions.vkCreateRayTracingPipelinesKHR
            || !m_rtFunctions.vkGetRayTracingShaderGroupHandlesKHR
            || !m_rtFunctions.vkCmdTraceRaysKHR)
        {
            LOG_STREAM_ERROR("VkContext") << "Failed to load ray tracing pipeline functions; disabling RT pipeline";
            m_supportsRayTracingPipeline = false;
        }
    }
}

// 探测 RT 管线扩展与特性（路线 B）。与 ray query 解耦：不满足时仅
// m_supportsRayTracingPipeline=false，ray query 路径不受影响。
bool VkContext::CheckRayTracingPipelineSupport(VkPhysicalDevice device) const
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

    std::set<std::string> required(m_rayTracingPipelineExtensions.begin(), m_rayTracingPipelineExtensions.end());
    for (const auto& e : available) required.erase(e.extensionName);
    if (!required.empty()) return false;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtpFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
    VkPhysicalDeviceFeatures2 features2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    features2.pNext = &rtpFeatures;
    vkGetPhysicalDeviceFeatures2(device, &features2);

    return rtpFeatures.rayTracingPipeline == VK_TRUE;
}
#endif
