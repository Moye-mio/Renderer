// ============================================================================
// AssetLoader - MeshLoader.cpp
// ============================================================================
#include "MeshLoader.h"

#include <Assimp/scene.h>
#include <Assimp/material.h>

#include <algorithm>
#include <limits>

#include "FileSystem.h"

namespace TitusAsset
{
    namespace
    {
        struct AiTextureKind
        {
            unsigned int aiType;
            TextureSlot  slot;
            bool         isSRGB;
        };

        // 与 Renderer/Model.cpp::ProcessTextures 的语义对齐
        constexpr AiTextureKind kKinds[] = {
            { aiTextureType_DIFFUSE,   TextureSlot::Diffuse,   true  },
            { aiTextureType_SPECULAR,  TextureSlot::Specular,  false },
            { aiTextureType_NORMALS,   TextureSlot::Normal,    false },
            { aiTextureType_HEIGHT,    TextureSlot::Height,    false },
            { aiTextureType_AMBIENT,   TextureSlot::Ambient,   true  },
            { aiTextureType_EMISSIVE,  TextureSlot::Emissive,  true  },
            { aiTextureType_SHININESS, TextureSlot::Roughness, false },
            { aiTextureType_OPACITY,   TextureSlot::Metallic,  false }
        };
    } // anonymous

    bool BuildMeshFromAi(const aiMesh* mesh,
                         const aiScene* scene,
                         const std::string& directory,
                         MeshAssetData& outMesh)
    {
        if (!mesh) return false;

        outMesh.name = mesh->mName.length > 0
            ? std::string(mesh->mName.C_Str())
            : std::string("UnnamedMesh");

        // ---- 顶点 ----
        outMesh.vertices.clear();
        outMesh.vertices.reserve(mesh->mNumVertices);

        TitusMath::Vec3 mn( std::numeric_limits<float>::max());
        TitusMath::Vec3 mx(-std::numeric_limits<float>::max());

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
        {
            MeshVertex v{};
            const auto& p = mesh->mVertices[i];
            v.position = { p.x, p.y, p.z };

            mn = TitusMath::min(mn, v.position);
            mx = TitusMath::max(mx, v.position);

            if (mesh->HasNormals())
            {
                const auto& n = mesh->mNormals[i];
                v.normal = { n.x, n.y, n.z };
            }
            if (mesh->mTextureCoords[0])
            {
                const auto& uv = mesh->mTextureCoords[0][i];
                v.uv = { uv.x, uv.y };
            }
            if (mesh->HasTangentsAndBitangents())
            {
                const auto& t  = mesh->mTangents[i];
                const auto& bt = mesh->mBitangents[i];
                v.tangent   = { t.x,  t.y,  t.z  };
                v.bitangent = { bt.x, bt.y, bt.z };
            }
            outMesh.vertices.push_back(v);
        }
        outMesh.aabbMin = (mesh->mNumVertices > 0) ? mn : TitusMath::Vec3(0.0f);
        outMesh.aabbMax = (mesh->mNumVertices > 0) ? mx : TitusMath::Vec3(0.0f);

        // ---- 索引 ----
        outMesh.indices.clear();
        outMesh.indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3);
        for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
        {
            const aiFace& f = mesh->mFaces[i];
            for (unsigned int j = 0; j < f.mNumIndices; ++j)
                outMesh.indices.push_back(static_cast<uint32_t>(f.mIndices[j]));
        }

        // ---- 材质 ----
        if (scene && mesh->mMaterialIndex < scene->mNumMaterials)
        {
            const aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
            BuildMaterialFromAi(mat, directory, outMesh.material);
        }
        return true;
    }

    bool BuildMaterialFromAi(const aiMaterial* mat,
                             const std::string& directory,
                             MaterialAssetData& outMaterial)
    {
        if (!mat) return false;

        // 颜色与标量属性
        aiColor3D color;
        if (AI_SUCCESS == mat->Get(AI_MATKEY_COLOR_AMBIENT, color))
            outMaterial.ambientColor = { color.r, color.g, color.b };
        if (AI_SUCCESS == mat->Get(AI_MATKEY_COLOR_DIFFUSE, color))
            outMaterial.diffuseColor = { color.r, color.g, color.b };
        if (AI_SUCCESS == mat->Get(AI_MATKEY_COLOR_SPECULAR, color))
            outMaterial.specularColor = { color.r, color.g, color.b };

        float fval = 0.0f;
        if (AI_SUCCESS == mat->Get(AI_MATKEY_SHININESS, fval))
            outMaterial.shininess = fval;
        if (AI_SUCCESS == mat->Get(AI_MATKEY_REFRACTI, fval))
            outMaterial.refractiveIndex = fval;

        aiString matName;
        if (AI_SUCCESS == mat->Get(AI_MATKEY_NAME, matName))
            outMaterial.name = matName.C_Str();

        // 贴图引用（不加载像素，仅记录路径）
        outMaterial.textures.clear();
        for (const auto& kind : kKinds)
        {
            const unsigned int count = mat->GetTextureCount(static_cast<aiTextureType>(kind.aiType));
            for (unsigned int i = 0; i < count; ++i)
            {
                aiString aipath;
                if (AI_SUCCESS != mat->GetTexture(static_cast<aiTextureType>(kind.aiType), i, &aipath))
                    continue;
                MaterialTextureRef ref;
                ref.slot   = kind.slot;
                ref.isSRGB = kind.isSRGB;
                ref.path   = JoinPath(directory, aipath.C_Str());
                outMaterial.textures.push_back(std::move(ref));
            }
        }
        return true;
    }
} // namespace TitusAsset
