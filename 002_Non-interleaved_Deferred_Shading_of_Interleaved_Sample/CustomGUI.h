#pragma once
#include "IGUI.h"

class CustomGUI : public IGUI
{
public:
    CustomGUI(const std::string& name, int executionOrder);
    ~CustomGUI() override = default;

    void InitV() override;
    void UpdateV() override;
};
