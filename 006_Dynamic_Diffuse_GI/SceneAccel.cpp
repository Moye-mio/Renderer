// ============================================================================
// 006_Dynamic_Diffuse_GI - SceneAccel.cpp
// ============================================================================
#include "SceneAccel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "AssetLoader/AssetTypes.h"
#include "Logger.h"

namespace
{
    struct GpuVertex
    {
        float px, py, pz, albedoR;
        float nx, ny, nz, albedoG;
        float ux, uy, albedoB, pad;
    };

    TitusMath::Vec3 SampleImage(const TitusAsset::ImageAssetData& img, float u, float v)
    {
        if (img.width == 0 || img.height == 0 || img.mips.empty())
            return TitusMath::Vec3(1.0f);
        const auto& mip = img.mips[0];
        if (mip.pixels.empty())
            return TitusMath::Vec3(1.0f);

        u = u - std::floor(u);
        v = v - std::floor(v);
        if (u < 0.0f) u += 1.0f;
        if (v < 0.0f) v += 1.0f;

        const int w = static_cast<int>(img.width);
        const int h = static_cast<int>(img.height);
        const int x = std::clamp(static_cast<int>(u * static_cast<float>(w)), 0, w - 1);
        const int y = std::clamp(static_cast<int>(v * static_cast<float>(h)), 0, h - 1);
        const uint32_t ch = img.channels == 0 ? 4u : img.channels;
        const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * ch;
        if (idx + (ch > 0 ? ch - 1 : 0) >= mip.pixels.size())
            return TitusMath::Vec3(1.0f);

        if (img.pixelType == TitusAsset::ImagePixelType::UNorm8)
        {
            const uint8_t* p = mip.pixels.data() + idx;
            const float r = p[0] / 255.0f;
            const float g = ch > 1 ? p[1] / 255.0f : r;
            const float b = ch > 2 ? p[2] / 255.0f : r;
            return TitusMath::Vec3(r, g, b);
        }
        if (img.pixelType == TitusAsset::ImagePixelType::Float32 && ch >= 3)
        {
            const float* p = reinterpret_cast<const float*>(mip.pixels.data() + idx * sizeof(float));
            return TitusMath::Vec3(p[0], p[1], p[2]);
        }
        return TitusMath::Vec3(1.0f);
    }

    const TitusAsset::ImageAssetData* FindDiffuseImage(
        const TitusAsset::MeshAssetData& mesh,
        const std::unordered_map<std::string, const TitusAsset::ImageAssetData*>& byPath)
    {
        for (const auto& tref : mesh.material.textures)
        {
            if (tref.slot != TitusAsset::TextureSlot::Diffuse)
                continue;
            auto it = byPath.find(tref.path);
            if (it != byPath.end())
                return it->second;
            const auto slash = tref.path.find_last_of("/\\");
            const std::string file = (slash == std::string::npos) ? tref.path : tref.path.substr(slash + 1);
            for (const auto& kv : byPath)
            {
                const auto s = kv.first.find_last_of("/\\");
                const std::string other = (s == std::string::npos) ? kv.first : kv.first.substr(s + 1);
                if (other == file)
                    return kv.second;
            }
        }
        return nullptr;
    }
}

bool SceneAccel::Build(TitusRHI::IGDevice& device, const TitusAsset::ModelAssetData& model)
{
    using namespace TitusRHI;

    m_aabbMin = TitusMath::Vec3(std::numeric_limits<float>::max());
    m_aabbMax = TitusMath::Vec3(-std::numeric_limits<float>::max());

    std::unordered_map<std::string, const TitusAsset::ImageAssetData*> byPath;
    byPath.reserve(model.sharedImages.size());
    for (const auto& img : model.sharedImages)
    {
        if (img)
            byPath[img->sourcePath] = img.get();
    }

    std::vector<float> positions;
    std::vector<uint32_t> indices;
    std::vector<GpuVertex> vertices;
    std::vector<uint32_t> meshRanges; // 4 uints per mesh

    positions.reserve(200000 * 3);
    indices.reserve(400000);
    vertices.reserve(200000);

    for (const auto& mesh : model.meshes)
    {
        const uint32_t triIndices = mesh.indices.empty()
            ? static_cast<uint32_t>(mesh.vertices.size())
            : static_cast<uint32_t>(mesh.indices.size());
        if (mesh.vertices.empty() || triIndices < 3)
            continue;

        m_aabbMin = TitusMath::min(m_aabbMin, mesh.aabbMin);
        m_aabbMax = TitusMath::max(m_aabbMax, mesh.aabbMax);

        const uint32_t firstVertex = static_cast<uint32_t>(vertices.size());
        const uint32_t firstIndex = static_cast<uint32_t>(indices.size());
        const TitusAsset::ImageAssetData* diffImg = FindDiffuseImage(mesh, byPath);
        const TitusMath::Vec3 kd = mesh.material.diffuseColor;

        for (const auto& v : mesh.vertices)
        {
            TitusMath::Vec3 albedo = kd;
            if (diffImg)
            {
                const TitusMath::Vec3 texel = SampleImage(*diffImg, v.uv.x, v.uv.y);
                albedo = TitusMath::Vec3(kd.x * texel.x, kd.y * texel.y, kd.z * texel.z);
            }

            GpuVertex gv{};
            gv.px = v.position.x;
            gv.py = v.position.y;
            gv.pz = v.position.z;
            gv.albedoR = albedo.x;
            gv.nx = v.normal.x;
            gv.ny = v.normal.y;
            gv.nz = v.normal.z;
            gv.albedoG = albedo.y;
            gv.ux = v.uv.x;
            gv.uy = v.uv.y;
            gv.albedoB = albedo.z;
            gv.pad = 0.0f;
            vertices.push_back(gv);
            positions.push_back(v.position.x);
            positions.push_back(v.position.y);
            positions.push_back(v.position.z);
        }

        if (!mesh.indices.empty())
        {
            indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
        }
        else
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(mesh.vertices.size()); ++i)
                indices.push_back(i);
        }

        meshRanges.push_back(firstIndex);
        meshRanges.push_back(static_cast<uint32_t>(indices.size()) - firstIndex);
        meshRanges.push_back(firstVertex);
        meshRanges.push_back(static_cast<uint32_t>(vertices.size()) - firstVertex);
    }

    if (!(m_aabbMin.x < m_aabbMax.x))
    {
        m_aabbMin = TitusMath::Vec3(-4.0f, -3.0f, -7.0f);
        m_aabbMax = TitusMath::Vec3(4.0f, 3.0f, 7.0f);
    }

    m_meshCount = static_cast<uint32_t>(meshRanges.size() / 4);
    m_vertexCount = static_cast<uint32_t>(vertices.size());
    m_indexCount = static_cast<uint32_t>(indices.size());

    if (m_vertexCount == 0 || m_indexCount < 3)
    {
        LOG_STREAM_ERROR("SceneAccel") << "model has no usable triangles";
        return false;
    }

    auto createBuf = [&](size_t bytes, BufferUsage usage, const void* data, const char* name) -> BufferHandle
    {
        BufferDesc bd{};
        bd.size = bytes;
        bd.usage = usage;
        bd.memory = MemoryUsage::GpuOnly;
        bd.initialData = data;
        bd.debugName = name;
        return device.CreateBuffer(bd);
    };

    m_positionBuffer = createBuf(
        positions.size() * sizeof(float),
        BufferUsage::ShaderDeviceAddress | BufferUsage::AccelerationStructureBuildInput | BufferUsage::TransferDst,
        positions.data(),
        "DDGI.AS.Positions");
    m_indexBuffer = createBuf(
        indices.size() * sizeof(uint32_t),
        BufferUsage::IndexBuffer | BufferUsage::StorageBuffer | BufferUsage::ShaderDeviceAddress
            | BufferUsage::AccelerationStructureBuildInput | BufferUsage::TransferDst,
        indices.data(),
        "DDGI.AS.Indices");
    m_vertexSSBO = createBuf(
        vertices.size() * sizeof(GpuVertex),
        BufferUsage::StorageBuffer | BufferUsage::TransferDst,
        vertices.data(),
        "DDGI.Hit.Vertices");
    m_meshRangeBuffer = createBuf(
        meshRanges.size() * sizeof(uint32_t),
        BufferUsage::StorageBuffer | BufferUsage::TransferDst,
        meshRanges.data(),
        "DDGI.Hit.MeshRanges");

    if (!m_positionBuffer.IsValid() || !m_indexBuffer.IsValid()
        || !m_vertexSSBO.IsValid() || !m_meshRangeBuffer.IsValid())
    {
        LOG_STREAM_ERROR("SceneAccel") << "failed to create geometry buffers";
        return false;
    }

    AccelerationStructureDesc blasDesc{};
    blasDesc.type = AccelerationStructureType::BottomLevel;
    blasDesc.buildFlags = ASBuildFlags::PreferFastTrace;
    blasDesc.debugName = "DDGI.Sponza.BLAS";
    blasDesc.geometries.reserve(model.meshes.size());

    for (uint32_t i = 0; i < m_meshCount; ++i)
    {
        const uint32_t firstIndex = meshRanges[i * 4 + 0];
        const uint32_t indexCount = meshRanges[i * 4 + 1];
        const uint32_t firstVertex = meshRanges[i * 4 + 2];
        const uint32_t vertexCount = meshRanges[i * 4 + 3];
        if (vertexCount == 0 || indexCount < 3)
            continue;

        BLASGeometryDesc geo{};
        geo.vertexBuffer = m_positionBuffer;
        geo.vertexFormat = Format::R32G32B32_SFLOAT;
        geo.vertexStride = sizeof(float) * 3;
        geo.vertexCount = vertexCount;
        geo.vertexOffset = static_cast<uint64_t>(firstVertex) * sizeof(float) * 3;
        geo.indexBuffer = m_indexBuffer;
        geo.indexType = IndexType::UInt32;
        geo.indexCount = indexCount;
        geo.indexOffset = static_cast<uint64_t>(firstIndex) * sizeof(uint32_t);
        geo.opaque = true;
        blasDesc.geometries.push_back(geo);
    }

    if (blasDesc.geometries.empty())
    {
        LOG_STREAM_ERROR("SceneAccel") << "no BLAS geometries";
        return false;
    }

    LOG_STREAM_INFO("SceneAccel")
        << "building Sponza BLAS: meshes=" << blasDesc.geometries.size()
        << " vertices=" << m_vertexCount << " indices=" << m_indexCount;
    m_blas = device.CreateAccelerationStructure(blasDesc);
    if (!m_blas.IsValid())
    {
        LOG_STREAM_ERROR("SceneAccel") << "BLAS create failed";
        return false;
    }

    AccelerationStructureDesc tlasDesc{};
    tlasDesc.type = AccelerationStructureType::TopLevel;
    tlasDesc.buildFlags = ASBuildFlags::PreferFastTrace;
    tlasDesc.debugName = "DDGI.Sponza.TLAS";
    TLASInstanceDesc inst{};
    inst.blas = m_blas;
    inst.mask = 0xFF;
    tlasDesc.instances.push_back(inst);
    m_tlas = device.CreateAccelerationStructure(tlasDesc);
    if (!m_tlas.IsValid())
    {
        LOG_STREAM_ERROR("SceneAccel") << "TLAS create failed";
        return false;
    }

    LOG_STREAM_INFO("SceneAccel")
        << "Sponza AS ready, AABB min=(" << m_aabbMin.x << "," << m_aabbMin.y << "," << m_aabbMin.z
        << ") max=(" << m_aabbMax.x << "," << m_aabbMax.y << "," << m_aabbMax.z << ")";
    return true;
}

void SceneAccel::Destroy(TitusRHI::IGDevice& device)
{
    if (m_tlas.IsValid()) device.Destroy(m_tlas);
    if (m_blas.IsValid()) device.Destroy(m_blas);
    if (m_meshRangeBuffer.IsValid()) device.Destroy(m_meshRangeBuffer);
    if (m_vertexSSBO.IsValid()) device.Destroy(m_vertexSSBO);
    if (m_indexBuffer.IsValid()) device.Destroy(m_indexBuffer);
    if (m_positionBuffer.IsValid()) device.Destroy(m_positionBuffer);
    m_tlas = {};
    m_blas = {};
    m_meshRangeBuffer = {};
    m_vertexSSBO = {};
    m_indexBuffer = {};
    m_positionBuffer = {};
    m_meshCount = 0;
    m_vertexCount = 0;
    m_indexCount = 0;
}
