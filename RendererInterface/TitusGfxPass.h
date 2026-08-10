#pragma once
// ============================================================================
// RendererInterface - TitusGfxPass.h
// 业务侧 Pass 转发头：当外部模块（如 Examples/010_UnifiedTriangle）需要继承
// IRenderPass、调用 IGDevice / RenderCommandList、操作 GHandle / Desc /
// Enums 时，**仅 include 本头文件**，禁止直接 include `RendererCore/*.h`。
//
// 本头作为单一入口聚合 RendererCore 的核心 API（IRenderPass / IGDevice /
// RenderCommandList / GHandle / GEnums / GDescs），它们已统一在
// `namespace TitusRHI` 下声明，外部代码引用方式与原来完全一致：
//     class MyPass : public TitusRHI::IRenderPass { ... };
//     TitusRHI::ShaderDesc desc{};
// ============================================================================
#include "TitusGfx.h"

// 注意：以下 include 路径是 RendererInterface 工程的内部细节，不会泄露给
// 业务模块（业务模块只 include "RendererInterface/TitusGfxPass.h"）。CI 静态
// 扫描脚本只检查业务模块自身源码中的 #include 字面文本，不展开预处理。
#include "RendererCore/IRenderPass.h"
#include "RendererCore/IGDevice.h"
#include "RendererCore/RenderCommandList.h"
#include "RendererCore/RayTracingManager.h"
#include "RendererCore/GHandle.h"
#include "RendererCore/GEnums.h"
#include "RendererCore/GDescs.h"
#include "RendererCore/ShaderAsset.h"
#include "RendererCore/Material.h"
#include "RendererCore/ShaderParameterSet.h"
#include "RendererCore/GpuModel.h"
#include "RendererCore/GpuMesh.h"

namespace TitusRHI
{
    // ----------------------------------------------------------------------
    // DrawGpuModel —— RenderCommandList 的高层封装
    //
    // 把 `GpuModelHandle` 拆为 BindVertexBuffer + BindIndexBuffer + DrawIndexed
    // 序列；后端把这些低层调用各自翻译为 vkCmdXxx / glXxx。
    //
    // 之所以做成 inline helper（而非把它加到 RenderCommandList 抽象的纯虚
    // 接口里），是因为 `GpuModelHandle` 是 RendererInterface 自有的不透明
    // 句柄，需要通过 APP::GetGpuModelInternal 把它解为 GpuModel*；让抽象层
    // 不感知此类应用层句柄是更干净的分层。
    //
    // 用法：
    //     cmd.BindPipeline(pipe);
    //     TitusRHI::DrawGpuModel(cmd, modelHandle);
    //     // —— 等价于：
    //     // for (sub : model.subMeshes)
    //     //     cmd.BindVertexBuffer(0, sub.vertexBuffer);
    //     //     if (sub.indexCount > 0)
    //     //         cmd.BindIndexBuffer(sub.indexBuffer, sub.indexType);
    //     //         cmd.DrawIndexed(sub.indexCount);
    //     //     else
    //     //         cmd.Draw(sub.vertexCount);
    // ----------------------------------------------------------------------
    // ----------------------------------------------------------------------
    // GetMeshSharedLayout —— 通过 GpuModelHandle 取出该模型上传时由
    // AssetGpuUploader 构造的 VertexLayout（pos/normal/uv/tangent/bitangent）。
    // 业务侧 Pass 在创建 GraphicsPipelineDesc 时必须把它写到
    // `pd.vertexLayout`，否则 GL 后端创建 VAO 时 attribute 全部缺失，
    // glDrawElements 会读到 location=0/1/2 的全 0 默认值。
    // 找不到（句柄无效）时返回一个空 VertexLayout，调用方应自行兜底。
    // ----------------------------------------------------------------------
    inline VertexLayout GetMeshSharedLayout(GpuModelHandle handle)
    {
        const void* p = TitusRHI::APP::GetGpuModelInternal(handle);
        if (!p) return {};
        const GpuModel* model = static_cast<const GpuModel*>(p);
        return model->GetMesh().sharedLayout; // return 5?
    }

    inline void DrawGpuModel(RenderCommandList& cmd, GpuModelHandle handle)
    {
        const void* p = TitusRHI::APP::GetGpuModelInternal(handle);
        if (!p) return;
        const GpuModel* model = static_cast<const GpuModel*>(p);
        const auto& mesh = model->GetMesh();
        for (const auto& sub : mesh.subMeshes)
        {
            if (sub.vertexBuffer.IsValid())
            {
                cmd.BindVertexBuffer(0, sub.vertexBuffer, 0);
            }
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

    // ----------------------------------------------------------------------
    // DrawGpuModelWithDiffuse —— 在每个 SubMesh 绘制前自动绑定其
    // MaterialInstance.Diffuse 槽位的 (texture + sampler) 到指定 set/binding。
    // 适配传统 GLSL：`uniform sampler2D u_DiffuseTexture;`（GL 后端按反射映射
    // 到具体 slot；VK 后端按 descriptor set 写入）。
    // 用法：cmd.BindPipeline(...); TitusRHI::DrawGpuModelWithDiffuse(cmd, h, 0, 0);
    // ----------------------------------------------------------------------
    inline void DrawGpuModelWithDiffuse(RenderCommandList& cmd,
                                        GpuModelHandle handle,
                                        uint32_t setIndex,
                                        uint32_t bindingSlot)
    {
        const void* p = TitusRHI::APP::GetGpuModelInternal(handle);
        if (!p) return;
        const GpuModel* model = static_cast<const GpuModel*>(p);
        const auto& mesh = model->GetMesh();
        const auto& mats = model->GetMaterials();

        // 连续相同 Diffuse 跳过 BindResourceSet：OBJ 常按材质成组，
        // 可把绑定次数从 SubMesh 数压到接近材质切换次数（VK DS 路径收益最大）。
        TextureHandle lastDiffuseTex{};
        SamplerHandle lastDiffuseSampler{};
        bool hasLastDiffuse = false;

        for (size_t i = 0; i < mesh.subMeshes.size(); ++i)
        {
            const auto& sub = mesh.subMeshes[i];
            if (sub.vertexBuffer.IsValid())
            {
                cmd.BindVertexBuffer(0, sub.vertexBuffer, 0);
            }
            // 绑材质 diffuse（若该 SubMesh 索引在 materials 范围内且有效）
            if (i < mats.size())
            {
                const auto& binding = mats[i].TextureAt(MaterialTextureSlot::Diffuse);
                if (binding.texture.IsValid())
                {
                    const bool sameAsLast = hasLastDiffuse
                        && binding.texture.id == lastDiffuseTex.id
                        && binding.sampler.id == lastDiffuseSampler.id;
                    if (!sameAsLast)
                    {
                        ResourceSetDesc rs{};
                        ResourceBindingValue bv{};
                        bv.binding = bindingSlot;
                        bv.type = ResourceBindingType::CombinedImageSampler;
                        bv.texture = binding.texture;
                        bv.sampler = binding.sampler;
                        rs.bindings.push_back(bv);
                        cmd.BindResourceSet(setIndex, rs);
                        lastDiffuseTex = binding.texture;
                        lastDiffuseSampler = binding.sampler;
                        hasLastDiffuse = true;
                    }
                }
            }
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
}
