#pragma once
// ============================================================================
// 006_Dynamic_Diffuse_GI - Sponza
// 业务侧轻量包装：只持 UploadGpuModel 得到的句柄和 model 矩阵。
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
