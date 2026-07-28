#pragma once
#include "IRenderPass.h"

class Sponza;

class SponzaGBufferPass : public IRenderPass
{
public:
    SponzaGBufferPass(const std::string& renderPassName, int executionOrder);
    ~SponzaGBufferPass() override = default;

    void InitV() override;
    void UpdateV() override;

private:
    std::shared_ptr<Sponza> m_sponza;
    GLint m_FBO;
};
