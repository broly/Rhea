#pragma once
#include "engine.h"

class GameEngine : public Engine
{
public:
    void init() override;
};
REFLECT_OBJECT(GameEngine, Engine);