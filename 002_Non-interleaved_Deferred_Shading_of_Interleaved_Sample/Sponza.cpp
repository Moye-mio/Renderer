#include "Sponza.h"
#include "Interface.h"
#include "ResourceManager.h"

Sponza::Sponza(const std::string& gameObjectName, int executionOrder) : IGameObject(gameObjectName, executionOrder)
{
}

void Sponza::InitV()
{
    SetModel(ResourceManager::GetOrCreateInstance()->GetOrCreateModel(MODEL_PATH("sponza/sponza.obj")));
}

void Sponza::UpdateV()
{
}
