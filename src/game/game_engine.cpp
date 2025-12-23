#include "game_engine.h"

#include "game_renderer.h"

void GameEngine::init()
{
    renderer = std::make_shared<GameRenderer>();
    Engine::init();
}
