#pragma once
#include "IRenderPass.h"
#include <vector>

class EdgeDetectionPass : public IRenderPass
{
public:
    EdgeDetectionPass(const std::string& name, TitusGraphics::ERenderPassEvent event);
    ~EdgeDetectionPass() override = default;

    void InitV() override;
    void UpdateV() override;

private:
    std::vector<int> m_globalGroupSize;
    float m_normalThreshold = 0.0f;
    float m_depthThreshold = 1.15f;
    int m_oldKeyAddNormalThreshold = -1;
    int m_oldKeyAddDepthThreshold = -1;
    int m_oldKeyDecreaseNormalThreshold = -1;
    int m_oldKeyDecreaseDepthThreshold = -1;
};
