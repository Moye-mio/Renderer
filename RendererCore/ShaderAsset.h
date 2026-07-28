#pragma once
// ============================================================================
// RendererCore - ShaderAsset
// 跨后端 Shader 资源抽象（任务 9 / M-A 最小可用版）：
//   - 业务侧填一份 ShaderAssetDesc：vertex/fragment 各自给 GLSL 路径 + SPIR-V 路径
//   - LoadAndCreate(device) 按 device.GetBackend() 自动选源、读字节、CreateShader
//   - 任务 13 (M-B) 接入 spirv-cross 后会在此处填充 ShaderParameterMap
//
// 设计参考：requirements.md 需求 18-MA / 9.x。
// 注意：本类仅是"工厂样板"——它本身**不**继承 RHIShader；RHIShader 的元数据
// 由 GDevice::FindShader(handle) 反查（任务 7 已落地）。
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

#include "GHandle.h"
#include "GDescs.h"

namespace TitusRHI
{
    class IGDevice;

    struct ShaderAssetDesc
    {
        // GL 后端读取的 GLSL 文件路径（文本）
        std::string glVertexPath;
        std::string glFragmentPath;
        // VK 后端读取的 SPIR-V 文件路径（字节码）
        std::string vkVertexSpvPath;
        std::string vkFragmentSpvPath;
        // 调试名（同时可作为材质实例的默认 debugName）
        std::string debugName;
    };

    class ShaderAsset
    {
    public:
        ShaderAsset() = default;
        ~ShaderAsset() = default;

        // 按 device.GetBackend() 选源、读字节、CreateShader；返回是否成功。
        bool LoadAndCreate(IGDevice& device, const ShaderAssetDesc& desc);

        // 释放（推入设备的延迟销毁队列）
        void Destroy(IGDevice& device);

        ShaderHandle           GetVS()   const { return m_vs; }
        ShaderHandle           GetFS()   const { return m_fs; }
        const ShaderAssetDesc& GetDesc() const { return m_desc; }
        bool                   IsValid() const { return m_vs.IsValid() && m_fs.IsValid(); }

        // -------------------- 任务 13 / M6：反射 --------------------
        // 可读反射信息（合并后的 VS+FS）
        const ReflectionInfo&  GetReflection() const { return m_reflection; }
        ReflectionInfo&        MutableReflection()   { return m_reflection; }
        // 上层手填或外部工具产出后赋值
        void                   SetReflection(ReflectionInfo refl)
        {
            m_reflection = std::move(refl);
        }
        // 调用 ShaderReflector 对已加载的 shader 源/字节码做一次反射。
        // 仅在 LoadAndCreate 成功后调用才有意义。返回是否成功。
        bool                   AutoReflect(IGDevice& device);

    private:
        ShaderAssetDesc m_desc;
        ShaderHandle    m_vs;
        ShaderHandle    m_fs;
        ReflectionInfo  m_reflection{};
        // 以下两份字节码/文本仅为 AutoReflect 服务；LoadAndCreate 后会一直保留
        std::vector<uint8_t> m_vsBytes;
        std::vector<uint8_t> m_fsBytes;
    };
}
