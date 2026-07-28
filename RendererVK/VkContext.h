#pragma once
// ============================================================================
// VkContext —— Vulkan 的"全局上下文"
// 对标：OpenGL 中的 GL Context 是隐式的；Vulkan 必须显式管理
//   - VkInstance          : 驱动/加载器入口
//   - VkDebugUtilsMessenger: 验证层回调
//   - VkPhysicalDevice    : 物理 GPU
//   - VkDevice            : 逻辑设备
//   - VkQueue             : 图形/呈现队列
// ============================================================================
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include "Common.h"
#include "RENDERER_VK_EXPORTS.h"

class VkWindow;

class RENDERER_VK_DLLEXPORTS VkContext
{
public:
    VkContext()  = default;
    ~VkContext() = default;

    // 不可拷贝
    VkContext(const VkContext&)            = delete;
    VkContext& operator=(const VkContext&) = delete;

    void Init(const VkWindow& window);
    void Destroy();

    // ---- Getters ----
    VkInstance                          GetInstance()        const { return m_instance; }
    VkPhysicalDevice                    GetPhysicalDevice()  const { return m_physicalDevice; }
    VkDevice                            GetDevice()          const { return m_device; }
    VkSurfaceKHR                        GetSurface()         const { return m_surface; }
    VkQueue                             GetGraphicsQueue()   const { return m_graphicsQueue; }
    VkQueue                             GetPresentQueue()    const { return m_presentQueue; }
    const TitusVkGraphics::QueueFamilyIndices& GetQueueFamilyIndices() const { return m_queueFamilyIndices; }

    TitusVkGraphics::SwapchainSupportDetails QuerySwapchainSupport() const;
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) const;

#if defined(RENDERER_ENABLE_RAY_TRACING)
    // ------------------------------------------------------------------------
    // 光追能力查询（任务 1 / 需求 1、2）
    //   - 探测结果在 PickPhysicalDevice 阶段填充；不支持时二者恒为 false，
    //     设备照常创建（安全降级，绝不 abort）。
    //   - 扩展函数指针在 CreateLogicalDevice 末尾通过 vkGetDeviceProcAddr 加载。
    // ------------------------------------------------------------------------
    bool SupportsRayTracing() const { return m_supportsRayTracing; }
    bool SupportsRayQuery()   const { return m_supportsRayQuery; }

    const VkPhysicalDeviceAccelerationStructurePropertiesKHR& GetAccelStructProps() const
    {
        return m_accelStructProps;
    }

    // 光追管线（路线 B / P1，任务 12）：作为独立可选能力，与 ray query 解耦。
    bool SupportsRayTracingPipeline() const { return m_supportsRayTracingPipeline; }
    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& GetRTPipelineProps() const
    {
        return m_rtPipelineProps;
    }

    // 光追扩展函数指针（不支持时为 nullptr）
    struct RayTracingFunctions
    {
        PFN_vkGetAccelerationStructureBuildSizesKHR    vkGetAccelerationStructureBuildSizesKHR    = nullptr;
        PFN_vkCreateAccelerationStructureKHR           vkCreateAccelerationStructureKHR           = nullptr;
        PFN_vkDestroyAccelerationStructureKHR          vkDestroyAccelerationStructureKHR          = nullptr;
        PFN_vkCmdBuildAccelerationStructuresKHR        vkCmdBuildAccelerationStructuresKHR        = nullptr;
        PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
        PFN_vkGetBufferDeviceAddressKHR                vkGetBufferDeviceAddressKHR                = nullptr;
        // 光追管线（P1，任务 12/14/15）
        PFN_vkCreateRayTracingPipelinesKHR             vkCreateRayTracingPipelinesKHR             = nullptr;
        PFN_vkGetRayTracingShaderGroupHandlesKHR       vkGetRayTracingShaderGroupHandlesKHR       = nullptr;
        PFN_vkCmdTraceRaysKHR                          vkCmdTraceRaysKHR                          = nullptr;
    };
    const RayTracingFunctions& RT() const { return m_rtFunctions; }
#else
    bool SupportsRayTracing() const { return false; }
    bool SupportsRayQuery()   const { return false; }
    bool SupportsRayTracingPipeline() const { return false; }
#endif

private:
    // ---- Init sub-steps ----
    void CreateInstance();
    void SetupDebugMessenger();
    void PickPhysicalDevice();
    void CreateLogicalDevice();

    // ---- Helpers ----
    bool CheckValidationLayerSupport() const;
    std::vector<const char*> GetRequiredInstanceExtensions() const;
    bool IsDeviceSuitable(VkPhysicalDevice device) const;
    TitusVkGraphics::QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device) const;
    bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;

#if defined(RENDERER_ENABLE_RAY_TRACING)
    // 探测指定设备是否具备全部光追扩展（不影响 IsDeviceSuitable 的通过与否）
    bool CheckRayTracingSupport(VkPhysicalDevice device) const;
    // 探测 RT 管线（路线 B / P1）扩展与特性；与 ray query 解耦、单独可选。
    bool CheckRayTracingPipelineSupport(VkPhysicalDevice device) const;
    // 加载光追扩展函数指针（在 CreateLogicalDevice 成功且支持光追后调用）
    void LoadRayTracingFunctions();
#endif

    // ---- Members ----
    VkInstance               m_instance        = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger  = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface         = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physicalDevice  = VK_NULL_HANDLE;
    VkDevice                 m_device          = VK_NULL_HANDLE;
    VkQueue                  m_graphicsQueue   = VK_NULL_HANDLE;
    VkQueue                  m_presentQueue    = VK_NULL_HANDLE;

    TitusVkGraphics::QueueFamilyIndices m_queueFamilyIndices{};

    const std::vector<const char*> m_validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };
    const std::vector<const char*> m_deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

#if defined(RENDERER_ENABLE_RAY_TRACING)
    // ---- 光追相关（任务 1 / 需求 1、2）----
    // 光追所需的设备扩展；仅在 CheckRayTracingSupport 通过时追加进 CreateLogicalDevice。
    const std::vector<const char*> m_rayTracingExtensions = {
        VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME,
        VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
    };

    bool m_supportsRayTracing = false;
    bool m_supportsRayQuery   = false;
    VkPhysicalDeviceAccelerationStructurePropertiesKHR m_accelStructProps{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR
    };

    // 光追管线（路线 B / P1，任务 12）：独立于 ray query 的可选扩展与属性。
    // 注：VK_KHR_spirv_1_4 / shader_float_controls 在实例 API 1.2 下已为 core，
    // 无需单独作为设备扩展启用。
    const std::vector<const char*> m_rayTracingPipelineExtensions = {
        VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    };
    bool m_supportsRayTracingPipeline = false;
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtPipelineProps{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
    };

    RayTracingFunctions m_rtFunctions{};
#endif
};
