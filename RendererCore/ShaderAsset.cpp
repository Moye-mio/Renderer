// ============================================================================
// RendererCore - ShaderAsset.cpp
// ============================================================================
#include "ShaderAsset.h"
#include "IGDevice.h"
#include "ShaderReflector.h"
#include "FileSystem.h"

#include "Logger.h"
#include <string>

namespace TitusRHI
{
    bool ShaderAsset::LoadAndCreate(IGDevice& device, const ShaderAssetDesc& desc)
    {
        m_desc = desc;
        const GBackend backend = device.GetBackend();

        const std::string& vsPath = (backend == GBackend::Vulkan)
                                  ? desc.vkVertexSpvPath
                                  : desc.glVertexPath;
        const std::string& fsPath = (backend == GBackend::Vulkan)
                                  ? desc.vkFragmentSpvPath
                                  : desc.glFragmentPath;
        if (vsPath.empty() || fsPath.empty())
        {
            LOG_STREAM_ERROR("ShaderAsset") << "missing source for backend "
                      << static_cast<int>(backend);
            return false;
        }

        std::vector<uint8_t> vsBytes, fsBytes;
        if (!TitusAsset::ReadAllBytes(vsPath, vsBytes) ||
            !TitusAsset::ReadAllBytes(fsPath, fsBytes))
        {
            LOG_STREAM_ERROR("ShaderAsset") << "failed to open: " << vsPath
                      << " or " << fsPath;
            return false;
        }

        // 缓存下来供 AutoReflect 使用
        m_vsBytes = vsBytes;
        m_fsBytes = fsBytes;

        ShaderDesc vsd{};
        vsd.stage      = ShaderStage::Vertex;
        vsd.code       = vsBytes.data();
        vsd.bytes      = vsBytes.size();
        vsd.entryPoint = "main";
        vsd.debugName  = desc.debugName.empty() ? "ShaderAsset.vs" : desc.debugName.c_str();
        m_vs = device.CreateShader(vsd);
        if (!m_vs.IsValid()) return false;

        ShaderDesc fsd{};
        fsd.stage      = ShaderStage::Fragment;
        fsd.code       = fsBytes.data();
        fsd.bytes      = fsBytes.size();
        fsd.entryPoint = "main";
        fsd.debugName  = desc.debugName.empty() ? "ShaderAsset.fs" : desc.debugName.c_str();
        m_fs = device.CreateShader(fsd);
        if (!m_fs.IsValid())
        {
            device.Destroy(m_vs);
            m_vs = {};
            return false;
        }
        return true;
    }

    void ShaderAsset::Destroy(IGDevice& device)
    {
        if (m_vs.IsValid()) device.Destroy(m_vs);
        if (m_fs.IsValid()) device.Destroy(m_fs);
        m_vs = {};
        m_fs = {};
        m_vsBytes.clear();
        m_fsBytes.clear();
        m_reflection = {};
    }

    // ------------------------------------------------------------------------
    // AutoReflect
    //  - VK 后端：会拿到 SPIR-V 字节流，调 ShaderReflector::ReflectFromSPIRV。
    //    如果未定义 TITUS_ENABLE_SPIRV_CROSS 则返回 false。
    //  - GL 后端：会拿到 GLSL 文本，调 ShaderReflector::ReflectFromGLSLSource。
    // 反射结果合并进入 m_reflection（VS 与 FS 同 set/binding 项会按位或 stages）。
    // ------------------------------------------------------------------------
    bool ShaderAsset::AutoReflect(IGDevice& device)
    {
        if (m_vsBytes.empty() || m_fsBytes.empty())
        {
            LOG_STREAM_ERROR("ShaderAsset") << "AutoReflect: shader bytes empty (call LoadAndCreate first)";
            return false;
        }
        const GBackend backend = device.GetBackend();
        bool ok = false;
        if (backend == GBackend::Vulkan)
        {
            // SPIR-V 是 32-bit word 流，需重解释
            const auto* vsSpv = reinterpret_cast<const uint32_t*>(m_vsBytes.data());
            const auto* fsSpv = reinterpret_cast<const uint32_t*>(m_fsBytes.data());
            const size_t vsWords = m_vsBytes.size() / sizeof(uint32_t);
            const size_t fsWords = m_fsBytes.size() / sizeof(uint32_t);
            const bool okVS = ShaderReflector::ReflectFromSPIRV(vsSpv, vsWords, ShaderStage::Vertex,   m_reflection);
            const bool okFS = ShaderReflector::ReflectFromSPIRV(fsSpv, fsWords, ShaderStage::Fragment, m_reflection);
            ok = okVS && okFS;
        }
        else
        {
            // GL 文本路径
            const std::string vsSrc(m_vsBytes.begin(), m_vsBytes.end());
            const std::string fsSrc(m_fsBytes.begin(), m_fsBytes.end());
            const bool okVS = ShaderReflector::ReflectFromGLSLSource(vsSrc, ShaderStage::Vertex,   m_reflection);
            const bool okFS = ShaderReflector::ReflectFromGLSLSource(fsSrc, ShaderStage::Fragment, m_reflection);
            ok = okVS && okFS;
        }
        return ok;
    }
}
