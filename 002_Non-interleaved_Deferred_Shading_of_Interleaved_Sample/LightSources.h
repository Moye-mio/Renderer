#pragma once
#include "IGameObject.h"

#include <glm/glm.hpp>

struct PointLight
{
    glm::vec4 position;
    glm::vec4 colorAndRadius;
};

class LightSources : public IGameObject
{
public:
    LightSources(const std::string& gameObjectName, int executionOrder);
    ~LightSources() override;

    void InitV() override;
    void UpdateV() override;

private:
    std::vector<PointLight> m_lightSources;
    int m_cntLight = 10000;
};
