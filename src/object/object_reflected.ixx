module;

#include <json/value.h>

export module rhobject:reflected;

import std.compat;
import reflect;

import :object;
import :reflection;
#include "object_reflection_macro.h"

RH_OBJECT(RhObject)
