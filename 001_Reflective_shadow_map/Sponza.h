#pragma once
// ============================================================================
// 001_Reflective_shadow_map - Sponza
//
// 业务侧轻量类：仅持有由 RendererInterface::APP::UploadGpuModel 上传得到
// 的 `GpuModelHandle` 与 `TitusMath::Mat4 modelMatrix`，不做任何文件 IO、不做
// 任何 GPU 上传。资产解码（AssetLoader）→ 上传（gfx）→ 持 handle（业务）
// 这条分层在 main.cpp 中显式驱动。
//
// 历史：曾经继承自旧 `IGameObject`、依赖 `ShaderProgram / ResourceManager /
// Common`。本期已彻底切除这些依赖。
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
            // 释放旧 handle，再接管 o 的
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

    // 析构：释放 GpuModel；不触发任何 IO
    ~Sponza()
    {
        if (m_modelHandle.IsValid())
        {
            TitusRHI::APP::DestroyGpuModel(m_modelHandle);
            m_modelHandle = TitusRHI::GpuModelHandle{};
        }
    }

    // -- 只读访问器 --
    TitusRHI::GpuModelHandle GetModelHandle() const { return m_modelHandle; }
    const TitusMath::Mat4& GetModelMatrix() const { return m_modelMatrix; }

private:
    TitusRHI::GpuModelHandle m_modelHandle{};
    TitusMath::Mat4 m_modelMatrix{1.0f};
};
