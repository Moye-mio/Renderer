// ============================================================================
// AssetLoader - ModelLoader.cpp
// ============================================================================
#include "ModelLoader.h"

#include <Assimp/Importer.hpp>
#include <Assimp/postprocess.h>
#include <Assimp/scene.h>
#include <Assimp/config.h>

#include <cstdio>
#include "Logger.h"
#include <unordered_map>

#include "FileSystem.h"
#include "MeshLoader.h"
#include "ImageLoader.h"

namespace TitusAsset
{
    bool LoadModel(const std::string& path,
                   ModelAssetData& outModel,
                   const ModelLoadOptions& opts)
    {
        Assimp::Importer importer;
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS, opts.fbxReadAnimations);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS, opts.fbxReadCameras);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS, opts.fbxReadLights);
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, opts.fbxPreservePivots);

        unsigned int flags = 0;
        if (opts.triangulate)          flags |= aiProcess_Triangulate;
        if (opts.flipUVs)              flags |= aiProcess_FlipUVs;
        if (opts.generateNormals)      flags |= aiProcess_GenSmoothNormals;
        if (opts.calcTangentSpace)     flags |= aiProcess_CalcTangentSpace;
        if (opts.preTransformVertices) flags |= aiProcess_PreTransformVertices;

        const aiScene* scene = importer.ReadFile(path, flags);
        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            LOG_ERROR("AssetLoader::ModelLoader",
                     "Assimp failed: %s, error=%s",
                     path.c_str(), importer.GetErrorString());
            return false;
        }

        outModel.sourcePath = path;
        outModel.directory  = GetDirectory(path);
        outModel.meshes.clear();
        outModel.sharedImages.clear();

        // 1) 解析所有 mesh + material
        outModel.meshes.reserve(scene->mNumMeshes);
        for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        {
            MeshAssetData mesh;
            if (BuildMeshFromAi(scene->mMeshes[i], scene, outModel.directory, mesh))
                outModel.meshes.push_back(std::move(mesh));
        }

        // 2) （可选）加载所有引用纹理 → sharedImages（按 path 去重）
        if (opts.loadTextures)
        {
            ImageLoadOptions imgOpts;
            imgOpts.flipVerticallyOnLoad = opts.flipVerticallyOnLoad;

            std::unordered_map<std::string, std::shared_ptr<ImageAssetData>> cache;
            for (const auto& m : outModel.meshes)
            {
                for (const auto& tref : m.material.textures)
                {
                    if (cache.find(tref.path) != cache.end())
                        continue;
                    imgOpts.isSRGBHint = tref.isSRGB;
                    auto img = LoadImage2D(tref.path, imgOpts);
                    if (img)
                        cache.emplace(tref.path, img);
                    else
                        LOG_ERROR("AssetLoader::ModelLoader",
                                 "missing texture: %s",
                                 tref.path.c_str());
                }
            }
            outModel.sharedImages.reserve(cache.size());
            for (auto& kv : cache)
                outModel.sharedImages.push_back(std::move(kv.second));
        }
        return true;
    }
} // namespace TitusAsset
