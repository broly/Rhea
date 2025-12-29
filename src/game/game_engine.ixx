export module game:engine;

import engine;

#include "object/object_reflection_macro.h"

class GameEngine : public Engine
{
public:
    void init() override;
};
REFLECT_OBJECT(GameEngine, Engine);