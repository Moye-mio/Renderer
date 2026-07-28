#include "EdgeDetectionPass.h"
#include "Interface.h"
#include "GLUtils.h"
#include "ResourceManager.h"
#include "Common.h"
#include "InputManager.h"
#include "ShaderProgram.h"
#include "Logger.h"

using namespace TitusGraphics;

EdgeDetectionPass::EdgeDetectionPass(const std::string& name, TitusGraphics::ERenderPassEvent event)
    : IRenderPass(name, event)
{
}

void EdgeDetectionPass::InitV()
{
    auto texCfg = std::make_shared<TitusGraphics::Texture>();
    texCfg->InternalFormat = GL_RGBA32F;
    texCfg->ExternalFormat = GL_RGBA;
    texCfg->DataType = GL_FLOAT;
    texCfg->Type4MinFilter = GL_NEAREST;
    texCfg->Type4MagFilter = GL_NEAREST;
    texCfg->ImageBindUnit = 0;
    GLUtils::GenTexture(texCfg);
    RESOURCE_MANAGER::RegisterSharedData("EdgeTexture", texCfg);

    m_shader = std::make_shared<ShaderProgram>(SHADER_PATH("002_Non-interleaved_Deferred_Shading_of_Interleaved_Sample", "EdgeDetection_CS.glsl"));
    m_shader->ActiveShader();
    m_shader->SetIntUniformValue("u_WindowWidth", WINDOW_KEYWORD::GetWindowWidth());
    m_shader->SetIntUniformValue("u_WindowHeight", WINDOW_KEYWORD::GetWindowHeight());
    auto p = RESOURCE_MANAGER::GetSharedDataByName<std::shared_ptr<Texture>>("PositionTexture");
    m_shader->SetTextureUniformValue("u_PositionTexture", p);
    m_shader->SetTextureUniformValue("u_NormalTexture", RESOURCE_MANAGER::GetSharedDataByName<std::shared_ptr<Texture>>("NormalTexture"));
    m_shader->SetFloatUniformValue("u_NormalThreshold", m_normalThreshold);
    m_shader->SetFloatUniformValue("u_DepthThreshold", m_depthThreshold);
    m_shader->SetImageUniformValue(texCfg);

    std::vector<int> localGroupSize;
    m_shader->InquireLocalGroupSize(localGroupSize);
    m_globalGroupSize.push_back((WINDOW_KEYWORD::GetWindowWidth() + localGroupSize[0] - 1) / localGroupSize[0]);
    m_globalGroupSize.push_back((WINDOW_KEYWORD::GetWindowHeight() + localGroupSize[1] - 1) / localGroupSize[1]);
    m_globalGroupSize.push_back(1);
}

void EdgeDetectionPass::UpdateV()
{
    m_shader->ActiveShader();
    if (INPUT_MANAGER::GetKeyStatus(GLFW_KEY_O) == GLFW_PRESS && m_oldKeyAddNormalThreshold != GLFW_PRESS)
    {
        m_oldKeyAddNormalThreshold = GLFW_PRESS;
        m_normalThreshold += 0.05f;
        m_shader->SetFloatUniformValue("u_NormalThreshold", m_normalThreshold);
        LOG_STREAM_INFO("EdgeDetectionPass") << "u_NormalThreshold " << m_normalThreshold;
    }
    else if (INPUT_MANAGER::GetKeyStatus(GLFW_KEY_O) == GLFW_RELEASE)
        m_oldKeyAddNormalThreshold = GLFW_RELEASE;

    if (INPUT_MANAGER::GetKeyStatus(GLFW_KEY_P) == GLFW_PRESS && m_oldKeyDecreaseNormalThreshold != GLFW_PRESS)
    {
        m_oldKeyDecreaseNormalThreshold = GLFW_PRESS;
        m_normalThreshold -= 0.05f;
        m_shader->SetFloatUniformValue("u_NormalThreshold", m_normalThreshold);
        LOG_STREAM_INFO("EdgeDetectionPass") << "u_NormalThreshold " << m_normalThreshold;
    }
    else if (INPUT_MANAGER::GetKeyStatus(GLFW_KEY_P) == GLFW_RELEASE)
        m_oldKeyDecreaseNormalThreshold = GLFW_RELEASE;

    if (INPUT_MANAGER::GetKeyStatus(GLFW_KEY_K) == GLFW_PRESS && m_oldKeyAddDepthThreshold != GLFW_PRESS)
    {
        m_oldKeyAddDepthThreshold = GLFW_PRESS;
        m_depthThreshold += 0.05f;
        m_shader->SetFloatUniformValue("u_DepthThreshold", m_depthThreshold);

        LOG_STREAM_INFO("EdgeDetectionPass") << "u_DepthThreshold " << m_depthThreshold;
    }
    else if (INPUT_MANAGER::GetKeyStatus(GLFW_KEY_K) == GLFW_RELEASE)
        m_oldKeyAddDepthThreshold = GLFW_RELEASE;

    if (INPUT_MANAGER::GetKeyStatus(GLFW_KEY_L) == GLFW_PRESS && m_oldKeyDecreaseDepthThreshold != GLFW_PRESS)
    {
        m_oldKeyDecreaseDepthThreshold = GLFW_PRESS;
        m_depthThreshold -= 0.05f;
        m_shader->SetFloatUniformValue("u_DepthThreshold", m_depthThreshold);

        LOG_STREAM_INFO("EdgeDetectionPass") << "u_DepthThreshold " << m_depthThreshold;
    }
    else if (INPUT_MANAGER::GetKeyStatus(GLFW_KEY_L) == GLFW_RELEASE)
        m_oldKeyDecreaseDepthThreshold = GLFW_RELEASE;

    glDispatchCompute(m_globalGroupSize[0], m_globalGroupSize[1], m_globalGroupSize[2]);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
