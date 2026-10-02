module;

#include <json/value.h>

module gameplay;

import :player;

import std.compat;
import reflect;
import rhobject;
import ecs;
import framework;

#include "framework/scene_macros.h"

namespace
{
    SCENE_REGISTER_COMPONENTS(Player)
}
