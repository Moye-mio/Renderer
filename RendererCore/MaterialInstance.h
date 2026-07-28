#pragma once
// ============================================================================
// RendererCore - MaterialInstance
//
// "导入态"的材质实例：从 AssetLoader 的 MaterialAssetData 上传 GPU 后产出的
// 后端无关数据包（句柄聚合 + CPU 端可调标量）。
//
// 与 RendererCore::Material（运行期材质对象，持 ShaderAsset+PropertySheet）
// 的关系：
//   ┌───────────────────────┐    ┌─────────────────────┐
//   │ TitusAsset::Material  │ ─► │ TitusRHI::Material  │  ← 业务运行期使用
//   │  Asset Data (CPU)     │    │   Instance（本类）  │     可被进一步包装为
//   │                       │    │                     │     运行期 Material
//   └───────────────────────┘    └─────────────────────┘
//
// 任务 12 / M5-B：requirements.md 17.7 / 17.10(M-B) / 17.11 / 18.4。
// ============================================================================
#include <array>
#include <cstdint>
#include <string>

#include "TitusMath.h"

#include "GHandle.h"

namespace TitusRHI
{
    // 与 AssetLoader 的 TitusAsset::TextureSlot 一一对齐（顺序、个数）。
    // 这里复制一份"GPU 侧"枚举，避免 RendererCore 反向依赖 AssetLoader。
    enum class MaterialTextureSlot : uint8_t
    {
        Diffuse   = 0,
        Specular  = 1,
        Normal    = 2,
        Roughness = 3,
        Metallic  = 4,
        Ambient   = 5,
        Height    = 6,
        Emissive  = 7,
        SlotCount
    };

    // 单个槽位的纹理 + 采样器绑定
    struct MaterialTextureBinding
    {
        TextureHandle texture;
        SamplerHandle sampler;
        bool          isSRGB = false;
    };

    // 材质标量参数（对应原 MeshMatProperties + Shininess 等）
    struct MaterialParameters
    {
        TitusMath::Vec3 ambientColor   { 0.1f, 0.1f, 0.1f };
        TitusMath::Vec3 diffuseColor   { 1.0f, 1.0f, 1.0f };
        TitusMath::Vec3 specularColor  { 1.0f, 1.0f, 1.0f };
        float     shininess        = 32.0f;
        float     refractiveIndex  = 1.0f;
        // 保留 4 个泛用 float4 给上层 Shader 自由使用
        TitusMath::Vec4 userVector0      { 0.0f };
        TitusMath::Vec4 userVector1      { 0.0f };
    };

    // ------------------------------------------------------------------------
    // MaterialInstance —— 纯数据 + 句柄；不持有任何后端原生类型。
    // 由 AssetGpuUploader 或上层手动构造；可以被 Material（运行期对象）二次封装：
    //   Material mat;
    //   mat.SetShader(&shaderAsset);
    //   mat.SetVector("u_DiffuseColor", inst.params.diffuseColor);
    //   mat.SetTexture("u_DiffuseTex", inst.TextureAt(Diffuse).texture, ...);
    // ------------------------------------------------------------------------
    struct MaterialInstance
    {
        std::string         name;
        // 该材质期望使用的图形管线（可为非法句柄，由上层后续指定）
        PipelineHandle      pipeline;
        MaterialParameters  params;

        // 8 个固定槽位，按 MaterialTextureSlot 索引；未绑定的槽 texture.IsValid()==false
        std::array<MaterialTextureBinding,
                   static_cast<size_t>(MaterialTextureSlot::SlotCount)> textures{};

        // ---- 便捷接口 ----
        constexpr MaterialTextureBinding&
        TextureAt(MaterialTextureSlot slot) noexcept
        {
            return textures[static_cast<size_t>(slot)];
        }

        constexpr const MaterialTextureBinding&
        TextureAt(MaterialTextureSlot slot) const noexcept
        {
            return textures[static_cast<size_t>(slot)];
        }

        constexpr bool HasTexture(MaterialTextureSlot slot) const noexcept
        {
            return TextureAt(slot).texture.IsValid();
        }
    };
} // namespace TitusRHI
