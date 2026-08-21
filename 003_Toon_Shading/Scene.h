#pragma once
// ============================================================================
// 003_Toon_Shading - Scene
//
// 业务侧轻量场景：一份 Nilou GpuModel + 贴地缩放矩阵。不做文件 IO。
// ============================================================================
#include "AssetLoader/AssetTypes.h"
#include "RendererInterface/TitusGfx.h"

class Scene
{
public:
    Scene(TitusRHI::GpuModelHandle modelHandle, const TitusMath::Mat4& modelMatrix);
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&& o) noexcept;
    Scene& operator=(Scene&& o) noexcept;
    ~Scene();

    TitusRHI::GpuModelHandle GetModelHandle() const { return m_modelHandle; }
    const TitusMath::Mat4& GetModelMatrix() const { return m_modelMatrix; }

private:
    void DestroyHandles();

    TitusRHI::GpuModelHandle m_modelHandle{};
    TitusMath::Mat4 m_modelMatrix{1.0f};
};

bool ComputeModelAabb(const TitusAsset::ModelAssetData& asset,
                      TitusMath::Vec3& outMin,
                      TitusMath::Vec3& outMax);

// 把局部 AABB 缩放到 targetHeight、脚底贴 y=0、xz 居中。
TitusMath::Mat4 MakeFitGroundMatrix(const TitusMath::Vec3& localMin,
                                    const TitusMath::Vec3& localMax,
                                    float targetHeight);
