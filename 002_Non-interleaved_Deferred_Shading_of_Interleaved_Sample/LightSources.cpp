#include "LightSources.h"
#include <random>
#include "GLUtils.h"
#include "AABB.h"
#include "Common.h"
#include "Interface.h"

LightSources::LightSources(const std::string& gameObjectName, int executionOrder)
    : IGameObject(gameObjectName, executionOrder)
{
}

LightSources::~LightSources()
{
}

void LightSources::InitV()
{
    auto sponza = TitusGraphics::RESOURCE_MANAGER::GetGameObjectByName("Sponza");
    auto sponzaAABB = sponza->GetAABB();
    auto& minCorner = sponzaAABB->GetMin();
    auto& maxCorner = sponzaAABB->GetMax();

    float sponzaVolume = sponzaAABB->GetVolume();
    std::default_random_engine randomEngine;
    std::uniform_real_distribution<float> uniformFloat01(0.0f, 1.0f);
    std::uniform_real_distribution<float> uniformFloat(0.4f, 0.7f);
    std::uniform_real_distribution<float> uniformFloatX(minCorner.x, maxCorner.x);
    std::uniform_real_distribution<float> uniformFloatY(minCorner.y, maxCorner.y);
    std::uniform_real_distribution<float> uniformFloatZ(minCorner.z, maxCorner.z);

    glm::vec4 colorAndRadius;
    for (int i = 0; i < m_cntLight; i++)
    {
        float lightRadius = 0.5f;
        glm::vec4 position = {uniformFloatX(randomEngine), uniformFloatY(randomEngine), uniformFloatZ(randomEngine), 1.0f};
        GLUtils::HueToRGB(uniformFloat01(randomEngine) * uniformFloat(randomEngine), colorAndRadius);
        colorAndRadius.w = lightRadius;
        m_lightSources.push_back({position, colorAndRadius});
    }

    TitusGraphics::RESOURCE_MANAGER::RegisterSharedData("LightSources", m_lightSources.data());
    TitusGraphics::RESOURCE_MANAGER::RegisterSharedData("LightLightSourcesByteSize", m_lightSources.size() * sizeof(PointLight));
    TitusGraphics::RESOURCE_MANAGER::RegisterSharedData("CntLight", m_lightSources.size());
}

void LightSources::UpdateV()
{
}
