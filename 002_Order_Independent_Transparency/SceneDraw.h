#pragma once
// ============================================================================
// 002_Order_Independent_Transparency - SceneDraw
//
// ScenePass / WeightedBlendedOITPass 共用的几何绘制与着色 UBO。
// ============================================================================
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "RendererInterface/TitusGfxPass.h"
#include "Scene.h"
#include "TechniqueContext.h"

struct SceneShadingUBO
{
    TitusMath::Mat4 projection{1.0f};
    TitusMath::Mat4 view{1.0f};
    TitusMath::Vec4 lightDirVSAndAmbient{0.0f, 1.0f, 0.0f, 0.22f};
    TitusMath::Vec4 lightColor{1.0f, 0.96f, 0.88f, 0.0f};
    TitusMath::Vec4 weightedParams{2.0f, 20.0f, 1.5f, 3.0f}; // x=w1, y=w2, z=e1, w=e2
    TitusMath::Vec4 fourierParams{0.0f, 1.0f, 3.0f, 0.0f};   // x=zMin, y=1/(zMax-zMin), z=谐波阶数
};
static_assert(sizeof(SceneShadingUBO) == 192, "SceneShadingUBO std140 size");

inline void FillSceneShadingUBO(SceneShadingUBO& data, const TechniqueContext* ctx)
{
    data.projection = TitusRHI::CAMERA::GetMainCameraProjectionMatrix();
    data.view = TitusRHI::CAMERA::GetMainCameraViewMatrix();
    const TitusMath::Vec3 lightDirWS = TitusMath::normalize(TitusMath::Vec3(0.18f, 1.0f, 0.35f));
    const TitusMath::Vec4 lightDirVS = data.view * TitusMath::Vec4(lightDirWS, 0.0f);
    data.lightDirVSAndAmbient = TitusMath::Vec4(TitusMath::Vec3(lightDirVS), 0.22f);
    data.lightColor = TitusMath::Vec4(1.0f, 0.96f, 0.88f, 0.0f);
    if (ctx)
    {
        data.weightedParams = TitusMath::Vec4(
            ctx->weighted1, ctx->weighted2, ctx->weighted1Exp, ctx->weighted2Exp);
    }
}

// 按 order 生成龙的提交顺序（实例下标）。视空间朝 -Z 看，远处 z 更小（更负），
// 所以由远到近就是按视空间 z 升序。
inline std::vector<uint32_t> BuildDragonDrawOrder(const std::vector<DragonInstance>& dragons,
                                                 DragonDrawOrder order)
{
    std::vector<uint32_t> indices(dragons.size());
    for (size_t i = 0; i < indices.size(); ++i)
        indices[i] = static_cast<uint32_t>(i);
    if (order == DragonDrawOrder::SceneOrder || dragons.size() < 2)
        return indices;

    const TitusMath::Mat4 view = TitusRHI::CAMERA::GetMainCameraViewMatrix();
    std::vector<float> depth(dragons.size());
    for (size_t i = 0; i < dragons.size(); ++i)
        depth[i] = (view * TitusMath::Vec4(dragons[i].worldCenter, 1.0f)).z;

    const bool backToFront = (order == DragonDrawOrder::BackToFront);
    std::stable_sort(indices.begin(), indices.end(),
                     [&depth, backToFront](uint32_t a, uint32_t b)
                     {
                         return backToFront ? (depth[a] < depth[b]) : (depth[a] > depth[b]);
                     });
    return indices;
}

// 傅里叶基函数只在 t ∈ [0,1] 上正交，必须先把视空间线性深度归一化到这个区间。
// 直接拿世界单位的线性深度当 t 会让基函数周期退化成 1 个世界单位，深度相差整数
// 个单位的片元被映射到同一相位而无法区分。这里每帧按半透明几何自身的视空间深度
// 跨度建窗：把全部龙实例的局部 AABB 变换到视空间取包络，再按 pad 外扩一圈，
// 保证片元都落在窗口内部而不是 t=0/1 的相位边界上。
inline void ComputeDragonViewDepthWindow(const Scene& scene,
                                         float pad,
                                         float& outMin,
                                         float& outMax)
{
    outMin = 0.0f;
    outMax = 1.0f;

    const auto& dragons = scene.GetDragons();
    if (dragons.empty())
        return;

    const TitusMath::Vec3 lo = scene.GetDragonLocalMin();
    const TitusMath::Vec3 hi = scene.GetDragonLocalMax();
    if (!(lo.x < hi.x))
        return;

    const TitusMath::Mat4 view = TitusRHI::CAMERA::GetMainCameraViewMatrix();
    float zMin = std::numeric_limits<float>::max();
    float zMax = -std::numeric_limits<float>::max();
    for (const auto& dragon : dragons)
    {
        const TitusMath::Mat4 modelView = view * dragon.modelMatrix;
        for (int corner = 0; corner < 8; ++corner)
        {
            const TitusMath::Vec4 p{
                (corner & 1) ? hi.x : lo.x,
                (corner & 2) ? hi.y : lo.y,
                (corner & 4) ? hi.z : lo.z,
                1.0f};
            // 视空间朝 -Z 看，取负号换成朝前为正的线性深度。
            const float z = -(modelView * p).z;
            zMin = std::min(zMin, z);
            zMax = std::max(zMax, z);
        }
    }
    if (!(zMin < zMax))
        return;

    const float margin = std::max((zMax - zMin) * std::max(pad, 0.0f), 1e-3f);
    outMin = zMin - margin;
    outMax = zMax + margin;
}

inline void FillGeometryPipelineShared(TitusRHI::GraphicsPipelineDesc& pd,
                                       TitusRHI::ShaderHandle vs,
                                       TitusRHI::ShaderHandle fs,
                                       TitusRHI::GpuModelHandle layoutSource)
{
    using namespace TitusRHI;
    pd.vertexShader = vs;
    pd.fragmentShader = fs;
    pd.topology = PrimitiveTopology::TriangleList;
    pd.rasterizer.cullMode = CullMode::None;
    pd.rasterizer.frontFace = FrontFace::CounterClockwise;
    pd.depthStencil.depthTestEnable = true;
    pd.depthStencil.depthCompareOp = CompareOp::Less;
    pd.blend.attachments.resize(1);
    if (layoutSource.IsValid())
        pd.vertexLayout = GetMeshSharedLayout(layoutSource);

    PushConstantRange pcModel{};
    pcModel.stages = ShaderStage::Vertex;
    pcModel.offset = 0;
    pcModel.size = sizeof(TitusMath::Mat4);
    pcModel.glName = "u_ModelMatrix";
    pd.pushConstantRanges.push_back(pcModel);

    PushConstantRange pcAlbedo{};
    pcAlbedo.stages = ShaderStage::Fragment;
    pcAlbedo.offset = sizeof(TitusMath::Mat4);
    pcAlbedo.size = sizeof(TitusMath::Vec4);
    pcAlbedo.glName = "u_AlbedoOpacity";
    pd.pushConstantRanges.push_back(pcAlbedo);

    ResourceBinding rb{};
    rb.name = "u_SceneShading";
    rb.set = 0;
    rb.binding = 0;
    rb.type = ResourceBindingType::UniformBuffer;
    rb.stages = ShaderStage::Vertex | ShaderStage::Fragment;
    pd.resourceBindings.push_back(rb);
}

inline void DrawModelColored(TitusRHI::RenderCommandList& cmd,
                             TitusRHI::GpuModelHandle handle,
                             const TitusMath::Mat4& model,
                             const TitusMath::Vec3* albedos,
                             size_t albedoCount,
                             float opacity)
{
    using namespace TitusRHI;
    const void* p = APP::GetGpuModelInternal(handle);
    if (!p) return;
    const GpuModel* gpu = static_cast<const GpuModel*>(p);
    const auto& mesh = gpu->GetMesh();

    cmd.PushConstants(ShaderStage::Vertex, 0, sizeof(TitusMath::Mat4), &model);
    for (size_t i = 0; i < mesh.subMeshes.size(); ++i)
    {
        const TitusMath::Vec3 albedo = (!albedos || albedoCount == 0)
            ? TitusMath::Vec3(1.0f)
            : albedos[i < albedoCount ? i : albedoCount - 1];
        const TitusMath::Vec4 albedoOpacity(albedo, opacity);
        cmd.PushConstants(ShaderStage::Fragment, sizeof(TitusMath::Mat4),
                          sizeof(TitusMath::Vec4), &albedoOpacity);

        const auto& sub = mesh.subMeshes[i];
        if (sub.vertexBuffer.IsValid())
            cmd.BindVertexBuffer(0, sub.vertexBuffer, 0);
        if (sub.indexCount > 0 && sub.indexBuffer.IsValid())
        {
            cmd.BindIndexBuffer(sub.indexBuffer, sub.indexType, 0);
            cmd.DrawIndexed(sub.indexCount, 1, 0, 0, 0);
        }
        else
        {
            cmd.Draw(sub.vertexCount, 1, 0, 0);
        }
    }
}
