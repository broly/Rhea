export module game:engine;

import engine;

#include "object/object_reflection_macro.h"

class GameEngine : public Engine
{
public:
    void engine_init() override;
};
REFLECT_OBJECT(GameEngine, Engine);