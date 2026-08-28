// ============================================================================
// AssetLoader - MeshUtils（暂存于 003_Toon_Shading，见 SmoothNormal.h 文件头）
// ============================================================================
#include "SmoothNormal.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace TitusAsset
{
    namespace
    {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        // 累加结果短于此值视为邻面互相抵消。
        constexpr float kMinAccumLength = 1e-8f;
        // cos(1°)：判定平滑法线是否真的偏离了原法线。
        constexpr float kCosOneDeg = 0.99984769f;

        struct PosKey
        {
            int32_t x = 0;
            int32_t y = 0;
            int32_t z = 0;

            bool operator==(const PosKey& o) const noexcept
            {
                return x == o.x && y == o.y && z == o.z;
            }
        };

        struct PosKeyHash
        {
            size_t operator()(const PosKey& k) const noexcept
            {
                uint64_t h = static_cast<uint32_t>(k.x);
                h = h * 0x9E3779B97F4A7C15ull + static_cast<uint32_t>(k.y);
                h = h * 0x9E3779B97F4A7C15ull + static_cast<uint32_t>(k.z);
                return static_cast<size_t>(h ^ (h >> 32));
            }
        };

        // 桶里存单位面法线与权重，避免第二遍再逐个归一化。
        struct FaceRef
        {
            TitusMath::Vec3 unitNormal{0.0f};
            float weight = 1.0f;
        };

        inline PosKey MakeKey(const TitusMath::Vec3& p, float invEps)
        {
            return PosKey{
                static_cast<int32_t>(std::lround(p.x * invEps)),
                static_cast<int32_t>(std::lround(p.y * invEps)),
                static_cast<int32_t>(std::lround(p.z * invEps))
            };
        }
    } // namespace

    bool ComputeSmoothNormals(const ModelAssetData& model,
                              std::vector<std::vector<TitusMath::Vec3>>& outNormals,
                              const SmoothNormalOptions& opts,
                              SmoothNormalStats* outStats)
    {
        using TitusMath::Vec3;

        outNormals.clear();
        if (outStats) *outStats = SmoothNormalStats{};
        if (model.meshes.empty()) return false;

        const float eps = opts.positionEpsilon > 0.0f ? opts.positionEpsilon : 1e-4f;
        const float invEps = 1.0f / eps;
        const float cosLimit = std::cos(opts.maxSmoothAngleDeg * kDegToRad);

        size_t vertexTotal = 0;
        for (const auto& mesh : model.meshes)
            vertexTotal += mesh.vertices.size();
        if (vertexTotal == 0) return false;

        // 1) 按位置分桶，收集落在同一位置上的所有邻面。
        std::unordered_map<PosKey, uint32_t, PosKeyHash> bucketOf;
        std::vector<std::vector<FaceRef>> buckets;
        bucketOf.reserve(vertexTotal);
        buckets.reserve(vertexTotal);

        auto bucketIndex = [&](const Vec3& p) -> uint32_t
        {
            const PosKey key = MakeKey(p, invEps);
            const auto it = bucketOf.find(key);
            if (it != bucketOf.end()) return it->second;
            const uint32_t idx = static_cast<uint32_t>(buckets.size());
            bucketOf.emplace(key, idx);
            buckets.emplace_back();
            return idx;
        };

        for (const auto& mesh : model.meshes)
        {
            const size_t vertexCount = mesh.vertices.size();
            const bool useIndices = !mesh.indices.empty();
            const size_t triCount = useIndices ? mesh.indices.size() / 3
                                               : vertexCount / 3;
            for (size_t t = 0; t < triCount; ++t)
            {
                const size_t base = t * 3;
                const size_t i0 = useIndices ? mesh.indices[base + 0] : base + 0;
                const size_t i1 = useIndices ? mesh.indices[base + 1] : base + 1;
                const size_t i2 = useIndices ? mesh.indices[base + 2] : base + 2;
                if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
                    continue;

                const Vec3& p0 = mesh.vertices[i0].position;
                const Vec3& p1 = mesh.vertices[i1].position;
                const Vec3& p2 = mesh.vertices[i2].position;

                const Vec3 faceNormal = TitusMath::cross(p1 - p0, p2 - p0);
                const float doubleArea = TitusMath::length(faceNormal);
                if (doubleArea <= kMinAccumLength) continue; // 退化三角形

                FaceRef ref{};
                ref.unitNormal = faceNormal * (1.0f / doubleArea);
                ref.weight = opts.areaWeighted ? doubleArea : 1.0f;

                buckets[bucketIndex(p0)].push_back(ref);
                buckets[bucketIndex(p1)].push_back(ref);
                buckets[bucketIndex(p2)].push_back(ref);
            }
        }

        // 2) 每个顶点只累加与自身法线夹角在阈值内的邻面。
        size_t merged = 0;
        size_t fallbacks = 0;

        outNormals.resize(model.meshes.size());
        for (size_t m = 0; m < model.meshes.size(); ++m)
        {
            const auto& verts = model.meshes[m].vertices;
            auto& out = outNormals[m];
            out.resize(verts.size());

            for (size_t v = 0; v < verts.size(); ++v)
            {
                const Vec3 ownNormal = TitusMath::normalize(verts[v].normal);
                // 原法线退化时无从做角度判定，退回接纳全部邻面。
                const bool hasOwnNormal = TitusMath::length(ownNormal) > 0.5f;

                Vec3 acc{0.0f};
                const auto it = bucketOf.find(MakeKey(verts[v].position, invEps));
                if (it != bucketOf.end())
                {
                    for (const FaceRef& ref : buckets[it->second])
                    {
                        if (hasOwnNormal &&
                            TitusMath::dot(ref.unitNormal, ownNormal) < cosLimit)
                            continue;
                        acc += ref.unitNormal * ref.weight;
                    }
                }

                const float accLength = TitusMath::length(acc);
                if (accLength <= kMinAccumLength)
                {
                    out[v] = ownNormal;
                    ++fallbacks;
                    continue;
                }

                out[v] = acc * (1.0f / accLength);
                if (hasOwnNormal && TitusMath::dot(out[v], ownNormal) < kCosOneDeg)
                    ++merged;
            }
        }

        if (outStats)
        {
            outStats->uniquePositions = buckets.size();
            outStats->mergedVertices = merged;
            outStats->degenerateFallbacks = fallbacks;
        }
        return true;
    }
} // namespace TitusAsset
