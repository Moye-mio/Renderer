// ============================================================================
// 003_Toon_Shading - Scene.cpp
// ============================================================================
#include "Scene.h"

#include <algorithm>
#include <limits>

namespace
{
    TitusMath::Mat4 MakeScale(float s)
    {
        TitusMath::Mat4 m{};
        m[0][0] = s;
        m[1][1] = s;
        m[2][2] = s;
        return m;
    }

    TitusMath::Mat4 MakeTranslation(const TitusMath::Vec3& t)
    {
        TitusMath::Mat4 m{};
        m[3][0] = t.x;
        m[3][1] = t.y;
        m[3][2] = t.z;
        return m;
    }
}

Scene::Scene(TitusRHI::GpuModelHandle modelHandle, const TitusMath::Mat4& modelMatrix)
    : m_modelHandle(modelHandle)
    , m_modelMatrix(modelMatrix)
{
}

Scene::Scene(Scene&& o) noexcept
    : m_modelHandle(o.m_modelHandle)
    , m_modelMatrix(o.m_modelMatrix)
{
    o.m_modelHandle = {};
}

Scene& Scene::operator=(Scene&& o) noexcept
{
    if (this != &o)
    {
        DestroyHandles();
        m_modelHandle = o.m_modelHandle;
        m_modelMatrix = o.m_modelMatrix;
        o.m_modelHandle = {};
    }
    return *this;
}

Scene::~Scene()
{
    DestroyHandles();
}

void Scene::DestroyHandles()
{
    if (m_modelHandle.IsValid())
    {
        TitusRHI::APP::DestroyGpuModel(m_modelHandle);
        m_modelHandle = {};
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

TitusMath::Mat4 MakeFitGroundMatrix(const TitusMath::Vec3& localMin,
                                    const TitusMath::Vec3& localMax,
                                    float targetHeight)
{
    const TitusMath::Vec3 size = localMax - localMin;
    const float height = std::max(size.y, 1e-4f);
    const float s = targetHeight / height;
    const TitusMath::Vec3 feetCenter{
        (localMin.x + localMax.x) * 0.5f,
        localMin.y,
        (localMin.z + localMax.z) * 0.5f};
    // 先把脚底中心挪到原点，再统一缩放。Mat4 列主序，右乘先作用。
    return MakeScale(s) * MakeTranslation(-feetCenter);
}
