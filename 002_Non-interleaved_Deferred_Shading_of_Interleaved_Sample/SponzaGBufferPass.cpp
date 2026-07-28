#include "SponzaGBufferPass.h"

#include <glm/gtc/type_ptr.hpp>

#include "GLUtils.h"
#include "Interface.h"
#include "ShaderProgram.h"
#include "Sponza.h"

SponzaGBufferPass::SponzaGBufferPass(const std::string& renderPassName, int executionOrder)
    : IRenderPass(renderPassName, executionOrder)
{
}

void SponzaGBufferPass::InitV()
{
    m_shader = std::make_shared<ShaderProgram>(
        SHADER_PATH("002_Non-interleaved_Deferred_Shading_of_Interleaved_Sample", "Sponza_VS.glsl"),
        SHADER_PATH("002_Non-interleaved_Deferred_Shading_of_Interleaved_Sample", "Sponza_FS.glsl"));

    m_sponza = std::dynamic_pointer_cast<Sponza>(TitusGraphics::RESOURCE_MANAGER::GetGameObjectByName("Sponza"));

    auto textureCfg4Pos = std::make_shared<TitusGraphics::Texture>();
    auto textureCfg4Normal = std::make_shared<TitusGraphics::Texture>();
    auto textureCfg4Albedo = std::make_shared<TitusGraphics::Texture>();

    textureCfg4Albedo->InternalFormat = textureCfg4Normal->InternalFormat = textureCfg4Pos->InternalFormat = GL_RGBA32F;
    textureCfg4Albedo->ExternalFormat = textureCfg4Normal->ExternalFormat = textureCfg4Pos->ExternalFormat = GL_RGBA;
    textureCfg4Albedo->DataType = textureCfg4Normal->DataType = textureCfg4Pos->DataType = GL_FLOAT;

    GLUtils::GenTexture(textureCfg4Albedo);
    GLUtils::GenTexture(textureCfg4Normal);
    GLUtils::GenTexture(textureCfg4Pos);

    auto textureCfg4Depth = std::make_shared<TitusGraphics::Texture>();

    textureCfg4Depth->InternalFormat = GL_DEPTH_COMPONENT32F;
    textureCfg4Depth->ExternalFormat = GL_DEPTH_COMPONENT;
    textureCfg4Depth->DataType = GL_FLOAT;
    textureCfg4Depth->Type4MinFilter = GL_NEAREST;
    textureCfg4Depth->Type4MagFilter = GL_NEAREST;
    textureCfg4Depth->TextureAttachmentType = TitusGraphics::Texture::ETextureAttachmentType::DEPTH_TEXTURE;
    GLUtils::GenTexture(textureCfg4Depth);

    m_FBO = GLUtils::GenFBO({textureCfg4Albedo, textureCfg4Normal, textureCfg4Pos, textureCfg4Depth});

    TitusGraphics::RESOURCE_MANAGER::RegisterSharedData("AlbedoTexture", textureCfg4Albedo);
    TitusGraphics::RESOURCE_MANAGER::RegisterSharedData("NormalTexture", textureCfg4Normal);
    TitusGraphics::RESOURCE_MANAGER::RegisterSharedData("PositionTexture", textureCfg4Pos);
    TitusGraphics::RESOURCE_MANAGER::RegisterSharedData("DepthTexture", textureCfg4Depth);

    m_shader->ActiveShader();
    m_shader->SetMat4UniformValue("u_ModelMatrix", glm::value_ptr(m_sponza->GetModelMatrix()));
    m_sponza->InitModel(*m_shader);
}

void SponzaGBufferPass::UpdateV()
{
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    m_shader->ActiveShader();
    m_sponza->UpdateModel(*m_shader);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
