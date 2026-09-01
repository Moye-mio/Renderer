#pragma once
// ============================================================================
// 004_Anti_Aliasing - Sponza
//
// 业务侧轻量类：仅持有由 RendererInterface::APP::UploadGpuModel 上传得到的
// GpuModelHandle 与 modelMatrix。资产解码（AssetLoader）-> 上传（gfx）->
// 持 handle（业务）这条分层在 main.cpp 中显式驱动。
// ============================================================================
#include "RendererInterface/TitusGfx.h"

class Sponza
{
public:
    Sponza(TitusRHI::GpuModelHandle handle, const TitusMath::Mat4& modelMatrix)
        : m_modelHandle(handle), m_modelMatrix(modelMatrix)
    {
    }

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
                TitusRHI::APP::DestroyGpuModel(m_modelHandle);
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
