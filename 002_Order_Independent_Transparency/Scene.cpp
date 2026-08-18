// ============================================================================
// 002_Order_Independent_Transparency - Scene.cpp
// ============================================================================
#include "Scene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

    // TitusMath 没有提供仿射变换构造，这里就地手搓。注意不能用 Mat4(s) 当缩放：
    // 那个构造把 m[3][3] 也设成 s，齐次坐标整体缩放会被透视除法约掉，缩放实际
    // 无效，而 Scene_VS 里 v2f_PosVS 取的是未除 w 的视空间坐标，会被放大 s 倍，
    // 进而喂错 WBOIT 的深度权重。缩放矩阵必须保持 m[3][3] = 1。
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

    // 绕 Y 轴旋转。Mat4 是列主序 m[col][row]，与 glm 一致。
    TitusMath::Mat4 MakeRotationY(float radians)
    {
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        TitusMath::Mat4 m{};
        m[0][0] = c;
        m[0][2] = -s;
        m[2][0] = s;
        m[2][2] = c;
        return m;
    }

    TitusMath::Vec3 TransformPoint(const TitusMath::Mat4& m, const TitusMath::Vec3& p)
    {
        const TitusMath::Vec4 r = m * TitusMath::Vec4(p, 1.0f);
        const float invW = (r.w != 0.0f) ? (1.0f / r.w) : 1.0f;
        return TitusMath::Vec3(r.x * invW, r.y * invW, r.z * invW);
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
    , m_hasLayoutBounds(o.m_hasLayoutBounds)
    , m_dragonLocalMin(o.m_dragonLocalMin)
    , m_dragonLocalMax(o.m_dragonLocalMax)
    , m_boxMin(o.m_boxMin)
    , m_boxMax(o.m_boxMax)
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
        m_hasLayoutBounds = o.m_hasLayoutBounds;
        m_dragonLocalMin = o.m_dragonLocalMin;
        m_dragonLocalMax = o.m_dragonLocalMax;
        m_boxMin = o.m_boxMin;
        m_boxMax = o.m_boxMax;
        o.m_cornellHandle = {};
        o.m_dragonHandle = {};
    }
    return *this;
}

void Scene::SetLayoutBounds(const TitusMath::Vec3& dragonLocalMin,
                            const TitusMath::Vec3& dragonLocalMax,
                            const TitusMath::Vec3& boxMin,
                            const TitusMath::Vec3& boxMax)
{
    m_dragonLocalMin = dragonLocalMin;
    m_dragonLocalMax = dragonLocalMax;
    m_boxMin = boxMin;
    m_boxMax = boxMax;
    m_hasLayoutBounds = true;
}

void Scene::ApplyDragonLayout(const DragonLayoutParams& params)
{
    if (!m_hasLayoutBounds || m_dragons.empty())
        return;

    const int count = static_cast<int>(m_dragons.size());
    const std::vector<TitusMath::Mat4> matrices =
        (params.layout == DragonLayout::Pinwheel)
            ? MakeDragonPinwheelTransforms(m_dragonLocalMin, m_dragonLocalMax,
                                           m_boxMin, m_boxMax, count,
                                           params.heightFill, params.interlock,
                                           params.bladeDeg, params.phaseDeg)
            : MakeDragonRowTransforms(m_dragonLocalMin, m_dragonLocalMax,
                                      m_boxMin, m_boxMax, count, params.heightFill);

    const TitusMath::Vec3 localCenter = (m_dragonLocalMin + m_dragonLocalMax) * 0.5f;
    for (size_t i = 0; i < m_dragons.size() && i < matrices.size(); ++i)
    {
        m_dragons[i].modelMatrix = matrices[i];
        m_dragons[i].worldCenter = TransformPoint(matrices[i], localCenter);
    }
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
        const TitusMath::Vec3 t{
            firstCenterX + static_cast<float>(i) * (scaledW + gap) - s * localCenter.x,
            targetMin.y - s * localMin.y,
            boxCenterZ - s * localCenter.z};
        out.push_back(MakeTranslation(t) * MakeScale(s));
    }
    return out;
}

std::vector<TitusMath::Mat4> MakeDragonPinwheelTransforms(const TitusMath::Vec3& localMin,
                                                          const TitusMath::Vec3& localMax,
                                                          const TitusMath::Vec3& targetMin,
                                                          const TitusMath::Vec3& targetMax,
                                                          int count,
                                                          float heightFill,
                                                          float interlock,
                                                          float bladeDeg,
                                                          float phaseDeg)
{
    count = std::max(count, 1);
    interlock = std::min(std::max(interlock, 0.0f), 1.0f);

    const TitusMath::Vec3 localSize = localMax - localMin;
    const TitusMath::Vec3 localCenter = (localMin + localMax) * 0.5f;
    const TitusMath::Vec3 targetSize = targetMax - targetMin;

    const float halfZ = localSize.z * 0.5f;
    const float boxRadius = std::min(targetSize.x, targetSize.z) * 0.5f * 0.90f;

    // 高度约束与"单只龙本身不能占满整个水平半径"约束取小者；后者给摆放半径留余量。
    const float sH = localSize.y > 1e-6f
        ? (targetSize.y * std::max(heightFill, 0.01f)) / localSize.y : 1.0f;
    const float sR = (halfZ > 1e-6f) ? (boxRadius * 0.80f) / halfZ : 1.0f;
    const float s = std::min(sH, sR);

    const float rMax = std::max(boxRadius - s * halfZ, 0.0f);
    const float r = rMax * (1.0f - interlock);

    const TitusMath::Vec3 boxCenter{
        (targetMin.x + targetMax.x) * 0.5f,
        targetMin.y + targetSize.y * 0.5f,
        (targetMin.z + targetMax.z) * 0.5f};

    constexpr float kPi = 3.14159265358979323846f;
    const float kDegToRad = kPi / 180.0f;
    const float phase = phaseDeg * kDegToRad;
    const float blade = bladeDeg * kDegToRad;
    const float step = 2.0f * kPi / static_cast<float>(count);

    std::vector<TitusMath::Mat4> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        // 局部中心挪到原点 → 统一缩放 → 自转 blade 成斜置叶片 → 沿 +Z 外推 r →
        // 绕 Y 转到各自方位 → 平移到盒心。
        const float theta = phase + static_cast<float>(i) * step;
        const TitusMath::Mat4 m =
            MakeTranslation(boxCenter)
            * MakeRotationY(theta)
            * MakeTranslation(TitusMath::Vec3{0.0f, 0.0f, r})
            * MakeRotationY(blade)
            * MakeScale(s)
            * MakeTranslation(-localCenter);
        out.push_back(m);
    }
    return out;
}
