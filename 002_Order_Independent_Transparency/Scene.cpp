// ============================================================================
// 002_Order_Independent_Transparency - Scene.cpp
// ============================================================================
#include "Scene.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

namespace
{
    std::string ToLower(std::string s)
    {
        for (char& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    TitusMath::Vec3 CornellAlbedoForMesh(const TitusAsset::MeshAssetData& mesh)
    {
        // 顶灯在 MTL 里是 Ke 17 12 4 的自发光，Kd 只是 0.78 灰；MaterialAssetData
        // 不带 emissive 字段，这里直接给个亮暖色，免得灯板和天花板一个颜色。
        if (ToLower(mesh.material.name).find("light") != std::string::npos)
            return {1.00f, 0.93f, 0.72f};

        return mesh.material.diffuseColor;
    }
}

Scene::Scene(TitusRHI::GpuModelHandle cornellHandle,
             const TitusMath::Mat4& cornellMatrix,
             std::vector<TitusMath::Vec3> cornellAlbedo,
             TitusRHI::GpuModelHandle dragonHandle,
             std::vector<DragonInstance> dragons)
    : m_cornellHandle(cornellHandle)
    , m_cornellMatrix(cornellMatrix)
    , m_cornellAlbedo(std::move(cornellAlbedo))
    , m_dragonHandle(dragonHandle)
    , m_dragons(std::move(dragons))
{
}

Scene::Scene(Scene&& o) noexcept
    : m_cornellHandle(o.m_cornellHandle)
    , m_cornellMatrix(o.m_cornellMatrix)
    , m_cornellAlbedo(std::move(o.m_cornellAlbedo))
    , m_dragonHandle(o.m_dragonHandle)
    , m_dragons(std::move(o.m_dragons))
{
    o.m_cornellHandle = {};
    o.m_dragonHandle = {};
}

Scene& Scene::operator=(Scene&& o) noexcept
{
    if (this != &o)
    {
        DestroyHandles();
        m_cornellHandle = o.m_cornellHandle;
        m_cornellMatrix = o.m_cornellMatrix;
        m_cornellAlbedo = std::move(o.m_cornellAlbedo);
        m_dragonHandle = o.m_dragonHandle;
        m_dragons = std::move(o.m_dragons);
        o.m_cornellHandle = {};
        o.m_dragonHandle = {};
    }
    return *this;
}

Scene::~Scene()
{
    DestroyHandles();
}

void Scene::DestroyHandles()
{
    if (m_cornellHandle.IsValid())
    {
        TitusRHI::APP::DestroyGpuModel(m_cornellHandle);
        m_cornellHandle = {};
    }
    if (m_dragonHandle.IsValid())
    {
        TitusRHI::APP::DestroyGpuModel(m_dragonHandle);
        m_dragonHandle = {};
    }
}

bool ComputeModelAabb(const TitusAsset::ModelAssetData& asset,
                      TitusMath::Vec3& outMin,
                      TitusMath::Vec3& outMax)
{
    outMin = TitusMath::Vec3(std::numeric_limits<float>::max());
    outMax = TitusMath::Vec3(-std::numeric_limits<float>::max());
    for (const auto& mesh : asset.meshes)
    {
        outMin = TitusMath::min(outMin, mesh.aabbMin);
        outMax = TitusMath::max(outMax, mesh.aabbMax);
    }
    return outMin.x < outMax.x && outMin.y < outMax.y && outMin.z < outMax.z;
}

std::vector<TitusMath::Vec3> MakeCornellAlbedo(const TitusAsset::ModelAssetData& asset)
{
    std::vector<TitusMath::Vec3> colors;
    colors.reserve(asset.meshes.size());
    for (const auto& mesh : asset.meshes)
        colors.push_back(CornellAlbedoForMesh(mesh));
    return colors;
}

TitusMath::Vec3 DefaultDragonAlbedo(int index)
{
    static const TitusMath::Vec3 kPalette[] = {
        {0.18f, 0.68f, 0.95f}, // 青
        {0.90f, 0.22f, 0.55f}, // 品红
        {0.95f, 0.72f, 0.12f}, // 金
    };
    constexpr int n = static_cast<int>(sizeof(kPalette) / sizeof(kPalette[0]));
    const int i = index < 0 ? 0 : index;
    return kPalette[i % n];
}

std::vector<TitusMath::Mat4> MakeDragonRowTransforms(const TitusMath::Vec3& localMin,
                                                     const TitusMath::Vec3& localMax,
                                                     const TitusMath::Vec3& targetMin,
                                                     const TitusMath::Vec3& targetMax,
                                                     int count,
                                                     float heightFill)
{
    count = std::max(count, 1);
    const TitusMath::Vec3 localSize = localMax - localMin;
    const TitusMath::Vec3 targetSize = targetMax - targetMin;
    const TitusMath::Vec3 localCenter = (localMin + localMax) * 0.5f;

    const float gap = targetSize.x * 0.04f;
    const float usableX = targetSize.x * 0.90f;
    const float slotW = (usableX - gap * static_cast<float>(count - 1)) / static_cast<float>(count);

    const float sH = localSize.y > 1e-6f
        ? (targetSize.y * std::max(heightFill, 0.01f)) / localSize.y : 1.0f;
    const float sX = localSize.x > 1e-6f ? slotW / localSize.x : 1.0f;
    // 龙在 z 向最长（局部 AABB 约 0.45 x 0.71 x 1.0），进深是真正的瓶颈；
    // 这里允许它吃掉盒子 60% 的进深，否则在 1.99 高的盒子里会显得很小。
    const float sZ = localSize.z > 1e-6f ? (targetSize.z * 0.60f) / localSize.z : 1.0f;
    const float s = std::min(sH, std::min(sX, sZ));

    const float scaledW = localSize.x * s;
    const float totalW = scaledW * static_cast<float>(count) + gap * static_cast<float>(count - 1);
    const float boxCenterX = (targetMin.x + targetMax.x) * 0.5f;
    const float boxCenterZ = (targetMin.z + targetMax.z) * 0.5f;
    const float firstCenterX = boxCenterX - totalW * 0.5f + scaledW * 0.5f;

    std::vector<TitusMath::Mat4> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        TitusMath::Mat4 scale(s);
        TitusMath::Mat4 trans;
        trans[3][0] = firstCenterX + static_cast<float>(i) * (scaledW + gap) - s * localCenter.x;
        trans[3][1] = targetMin.y - s * localMin.y;
        trans[3][2] = boxCenterZ - s * localCenter.z;
        out.push_back(trans * scale);
    }
    return out;
}
