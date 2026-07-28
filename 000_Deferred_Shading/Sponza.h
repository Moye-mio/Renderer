#pragma once
// ============================================================================
// 000_Deferred_Shading - Sponza
//
// 业务侧轻量类：仅持有由 RendererInterface::APP::UploadGpuModel 上传得到的
// `GpuModelHandle` 与 `TitusMath::Mat4 modelMatrix`，不做任何文件 IO、不做任何 GPU
// 上传。资产解码（AssetLoader）-> 上传（gfx）-> 持 handle（业务）这条分层在
// main.cpp 中显式驱动。
// ============================================================================
#include "RendererInterface/TitusGfx.h"

class Sponza
{
public:
    // 唯一构造形式：业务侧先走 AssetLoader 解码 + APP::UploadGpuModel 拿到
    // handle 后再注入；Sponza 自身不接受 path，也不接收 IGDevice。
    Sponza(TitusRHI::GpuModelHandle handle, const TitusMath::Mat4& modelMatrix)
        : m_modelHandle(handle), m_modelMatrix(modelMatrix)
    {
    }

    // 不可拷贝（持有的 GpuModelHandle 视为独占语义）；可移动
    Sponza(const Sponza&) = delete;
    Sponza& operator=(const Sponza&) = delete;

    Sponza(Sponza&& o) noexcept
        : m_modelHandle(o.m_modelHandle), m_modelMatrix(o.m_modelMatrix)
    {
        o.m_modelHandle = TitusRHI::GpuModelHandle{};
    }

    Sponza& operator=(Sponza&& o) noexcept
    {
        if (this != &o)
        {
            if (m_modelHandle.IsValid())
            {
                TitusRHI::APP::DestroyGpuModel(m_modelHandle);
            }
            m_modelHandle = o.m_modelHandle;
            m_modelMatrix = o.m_modelMatrix;
            o.m_modelHandle = TitusRHI::GpuModelHandle{};
        }
        return *this;
    }

    ~Sponza()
    {
        if (m_modelHandle.IsValid())
        {
            TitusRHI::APP::DestroyGpuModel(m_modelHandle);
            m_modelHandle = TitusRHI::GpuModelHandle{};
        }
    }

    TitusRHI::GpuModelHandle GetModelHandle() const { return m_modelHandle; }
    const TitusMath::Mat4& GetModelMatrix() const { return m_modelMatrix; }

private:
    TitusRHI::GpuModelHandle m_modelHandle{};
    TitusMath::Mat4 m_modelMatrix{1.0f};
};
