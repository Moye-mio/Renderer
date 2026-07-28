#include "Interface.h"
#include "LightSources.h"
#include "Sponza.h"
#include "SponzaGBufferPass.h"

int main()
{
    TitusGraphics::WINDOW_KEYWORD::SetWindowSize(1920, 1080);
    TitusGraphics::WINDOW_KEYWORD::SetIsCursorDisable(true);
    TitusGraphics::COMPONENT_CONFIG::SetIsEnableGUI(false);

    TitusGraphics::RESOURCE_MANAGER::RegisterGameObject(std::make_shared<Sponza>("Sponza", 1));
    TitusGraphics::RESOURCE_MANAGER::RegisterGameObject(std::make_shared<LightSources>("LightSources", 2));

    TitusGraphics::RESOURCE_MANAGER::RegisterRenderPass(std::make_shared<SponzaGBufferPass>("SponzaGBufferPass", 1));

    return 0;
}
