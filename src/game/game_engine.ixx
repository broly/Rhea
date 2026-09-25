module;

#include <json/value.h>

export module game:engine;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import engine;

#include "object/object_reflection_macro.h"

class GameEngine : public Engine
{
public:
    void engine_init() override;
};
RH_OBJECT(GameEngine)