// ============================================================================
// 003_Toon_Shading - NilouMaterials.cpp
// ============================================================================
#include "NilouMaterials.h"

#include "FileSystem.h"
#include "ImageLoader.h"
#include "Logger.h"

#include <unordered_map>
#include <vector>

namespace NilouMaterials
{
    namespace
    {
        bool Contains(const std::string& s, const char* token)
        {
            return s.find(token) != std::string::npos;
        }

        const char* DiffuseFileFor(const TitusAsset::MeshAssetData& mesh)
        {
            const std::string& mat = mesh.material.name;
            const std::string& meshName = mesh.name;
            if (Contains(mat, "Mat_Hair") || meshName == "Hair")
                return "Avatar_Girl_Sword_Nilou_Tex_Hair_Diffuse.png";
            if (Contains(mat, "Mat_Face") || meshName == "Face")
                return "Avatar_Girl_Sword_Nilou_Tex_Face_Diffuse.png";
            if (Contains(mat, "Mat_Body") || Contains(mat, "Mat_Dress")
                || meshName == "Body" || meshName == "Dress")
                return "Avatar_Girl_Sword_Nilou_Tex_Body_Diffuse.png";
            return nullptr;
        }
    }

    bool KeepMesh(const TitusAsset::MeshAssetData& mesh)
    {
        const std::string& name = mesh.name;
        const std::string& mat = mesh.material.name;
        if (Contains(mat, "Mat_Body") || Contains(mat, "Mat_Dress")
            || Contains(mat, "Mat_Hair") || Contains(mat, "Mat_Face"))
            return true;

        return name == "Body" || name == "Dress" || name == "Hair" || name == "Face";
    }

    bool FilterAndBindDiffuse(TitusAsset::ModelAssetData& model,
                              const std::string& textureDir)
    {
        std::vector<TitusAsset::MeshAssetData> kept;
        kept.reserve(model.meshes.size());
        for (auto& mesh : model.meshes)
        {
            if (!KeepMesh(mesh))
            {
                LOG_STREAM_INFO("NilouMaterials")
                    << "skip mesh '" << mesh.name << "' mat='" << mesh.material.name << "'";
                continue;
            }
            kept.push_back(std::move(mesh));
        }
        model.meshes = std::move(kept);
        model.sharedImages.clear();

        if (model.meshes.empty())
        {
            LOG_STREAM_ERROR("NilouMaterials") << "no Body/Dress/Hair/Face meshes left";
            return false;
        }

        TitusAsset::ImageLoadOptions imgOpts{};
        imgOpts.flipVerticallyOnLoad = true;
        imgOpts.isSRGBHint = true;

        std::unordered_map<std::string, std::shared_ptr<TitusAsset::ImageAssetData>> cache;
        bool allOk = true;
        for (auto& mesh : model.meshes)
        {
            const char* file = DiffuseFileFor(mesh);
            if (!file)
            {
                LOG_STREAM_ERROR("NilouMaterials")
                    << "no Diffuse mapping for mesh '" << mesh.name
                    << "' mat='" << mesh.material.name << "'";
                allOk = false;
                continue;
            }

            const std::string path = TitusAsset::JoinPath(textureDir, file);
            auto it = cache.find(path);
            if (it == cache.end())
            {
                auto img = TitusAsset::LoadImage2D(path, imgOpts);
                if (!img)
                {
                    LOG_STREAM_ERROR("NilouMaterials") << "missing texture: " << path;
                    allOk = false;
                    cache.emplace(path, nullptr);
                    continue;
                }
                it = cache.emplace(path, std::move(img)).first;
            }
            if (!it->second)
            {
                allOk = false;
                continue;
            }

            mesh.material.textures.clear();
            TitusAsset::MaterialTextureRef ref{};
            ref.slot = TitusAsset::TextureSlot::Diffuse;
            ref.path = path;
            ref.isSRGB = true;
            mesh.material.textures.push_back(std::move(ref));

            LOG_STREAM_INFO("NilouMaterials")
                << "mesh '" << mesh.name << "' mat='" << mesh.material.name
                << "' diffuse=" << file
                << " verts=" << mesh.vertices.size();
        }

        model.sharedImages.reserve(cache.size());
        for (auto& kv : cache)
        {
            if (kv.second)
                model.sharedImages.push_back(std::move(kv.second));
        }
        return allOk && !model.sharedImages.empty();
    }
}
