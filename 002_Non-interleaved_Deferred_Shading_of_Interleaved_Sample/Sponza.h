#pragma once
#include "IGameObject.h"

class Sponza : public IGameObject
{
public:
    Sponza(const std::string& gameObjectName, int executionOrder);

    void InitV() override;
    void UpdateV() override;
};
