#pragma once
#include <memory>

#include "framework/assets/asset_manager.h"
#include "platform/window.h"

class Engine
{
public:
    void run();

    std::shared_ptr<AssetManager> asset_manager;
    platform::window::Window window;
};
