#pragma once
// ============================================================================
// 002_Order_Independent_Transparency - Scene
//
// 业务侧轻量场景：Cornell Box（不透明）+ 若干 Stanford Dragon 实例（半透明）。
// 龙共用一份 GpuModelHandle，并排同朝向、每只用不同颜色；不做文件 IO、不做 GPU 上传。
// ============================================================================
#include <vector>

#include "AssetLoader/AssetTypes.h"
#include "RendererInterface/TitusGfx.h"

// 半透明龙实例：共用同一份 GpuModel，只换矩阵和颜色。
struct DragonInstance
{
    TitusMath::Mat4 modelMatrix{1.0f};
    TitusMath::Vec3 albedo{0.25f, 0.70f, 0.90f};
};

constexpr int kDragonInstanceCount = 3;

class Scene
{
public:
    Scene(TitusRHI::GpuModelHandle cornellHandle,
          const TitusMath::Mat4& cornellMatrix,
          std::vector<TitusMath::Vec3> cornellAlbedo,
          TitusRHI::GpuModelHandle dragonHandle,
          std::vector<DragonInstance> dragons);

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&& o) noexcept;
    Scene& operator=(Scene&& o) noexcept;
    ~Scene();

    TitusRHI::GpuModelHandle GetCornellHandle() const { return m_cornellHandle; }
    const TitusMath::Mat4& GetCornellMatrix() const { return m_cornellMatrix; }
    const std::vector<TitusMath::Vec3>& GetCornellAlbedo() const { return m_cornellAlbedo; }

    TitusRHI::GpuModelHandle GetDragonHandle() const { return m_dragonHandle; }
    const std::vector<DragonInstance>& GetDragons() const { return m_dragons; }
    std::vector<DragonInstance>& MutableDragons() { return m_dragons; }

private:
    void DestroyHandles();

    TitusRHI::GpuModelHandle m_cornellHandle{};
    TitusMath::Mat4 m_cornellMatrix{1.0f};
    std::vector<TitusMath::Vec3> m_cornellAlbedo;

    TitusRHI::GpuModelHandle m_dragonHandle{};
    std::vector<DragonInstance> m_dragons;
};

// 合并模型所有 SubMesh 的 AABB；无效时返回 false。
bool ComputeModelAabb(const TitusAsset::ModelAssetData& asset,
                      TitusMath::Vec3& outMin,
                      TitusMath::Vec3& outMax);

// 逐 SubMesh 取 Cornell 的反照率。McGuire 版 MTL 的 Kd 就是 Cornell 实测反射率
// （左墙红 0.63/0.065/0.05，右墙绿 0.14/0.45/0.091，其余纸白），直接用即可；
// 只有自发光顶灯要单独兜一个亮色。
std::vector<TitusMath::Vec3> MakeCornellAlbedo(const TitusAsset::ModelAssetData& asset);

// 沿盒子 X 轴并排放 count 只，朝向相同（只做均匀缩放 + 平移），贴地、z 居中。
std::vector<TitusMath::Mat4> MakeDragonRowTransforms(const TitusMath::Vec3& localMin,
                                                     const TitusMath::Vec3& localMax,
                                                     const TitusMath::Vec3& targetMin,
                                                     const TitusMath::Vec3& targetMax,
                                                     int count,
                                                     float heightFill);

// 第 i 只龙的默认颜色（青 / 品红 / 金，循环），和 Cornell 红绿墙错开。
TitusMath::Vec3 DefaultDragonAlbedo(int index);
