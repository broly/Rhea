module game:engine;

import :renderer;
import <memory>;

void GameEngine::engine_init()
{
    auto game_renderer = std::make_shared<GameRenderer>();
    game_renderer->set_engine(std::static_pointer_cast<GameEngine>(shared_from_this()));
    renderer = game_renderer;
    Engine::engine_init();
}
