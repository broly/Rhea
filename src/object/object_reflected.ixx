module;

#include <json/value.h>

export module rhobject:reflected;

import std.compat;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import :object;
import :reflection;
#include "object_reflection_macro.h"

export namespace reflect_inner 
{ \
        const auto RhObject_serialize_lambda = [] (const Json::Value& json_object, RhObject* ObjPtr, const SerializationContext& context) { 
        }; 
        const bool RhObject_registered = \
            reflect::register_object_class<RhObject>(\
                "RhObject", \
                std::nullopt \
            ); \
        REFL_OBJECT_TRAITS(RhObject, void) \
} 