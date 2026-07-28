#include "Common.h"

namespace TitusVkGraphics
{
    namespace WINDOW_KEYWORD
    {
        int WINDOW_WIDTH = 1280;
        int WINDOW_HEIGHT = 720;
        int VIEWPORT_LEFTBOTTOM_X = 0;
        int VIEWPORT_LEFTBOTTOM_Y = 0;
        bool CURSOR_DISABLE = false;
        std::string WINDOW_TITLE = "TitusVkRenderer";
    }

    namespace COMPONENT_CONFIG
    {
        bool IS_ENABLE_GUI = false;
#ifdef _DEBUG
        bool ENABLE_VALIDATION_LAYER = true;
#else
        bool ENABLE_VALIDATION_LAYER = false;
#endif
        uint32_t MAX_FRAMES_IN_FLIGHT = 2; // frames of rendering instructions can the CPU record and submit in advance to the GPU
    }
}
