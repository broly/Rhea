#pragma once

import <set>;
import <optional>;
import <string_view>;
import <json/value.h>;
import dependency_collector;

#include "common/reflect_macros.h"

#define REFL_OBJECT_TRAITS(Name, Base) \
        export template<> \
        struct RhObjectTraits<Name> {\
            static constexpr std::string_view type_id = #Name; \
            static std::set<std::string_view> get_bases() {\
                return get_bases_impl<Name, Base>(#Name); \
            }\
            inline static std::set<std::string_view> bases = RhObjectTraits<Name>::get_bases(); \
        }; \

#define REFLECT_OBJECT(Name, Base, ...) \
    export namespace reflect_inner { \
        static_assert(std::is_base_of<RhObject, Name>::value, "Can't reflect non-RhObject types");\
        const bool Name##_registered = \
            reflect::register_actor_class<Name>(\
                #Name, \
                std::nullopt \
            ); \
        REFL_OBJECT_TRAITS(Name, Base) \
    } \
    



#define REFLECT_OBJECT_FIELDS(Name, Base, ...) \
    REFLECT_STRUCT(Name, __VA_ARGS__); \
    export namespace reflect_inner { \
        static_assert(std::is_base_of<RhObject, Name>::value, "Can't reflect non-RhObject types");\
        const bool Name##_registered = \
            reflect::register_actor_class<Name>(\
                #Name, \
                    [] (const Json::Value& json_object, RhObject* ObjPtr, bool is_loading, DependencyCollector* collector) -> bool { \
                        using Class = Name;\
                        auto CastedObjPtr = reinterpret_cast<Name*>(ObjPtr); \
                        reflect::json::visit_serialize(json_object, *CastedObjPtr, is_loading, collector); \
                        CastedObjPtr->on_serialize(collector); \
                        return true; \
                    }\
                ); \
        REFL_OBJECT_TRAITS(Name, Base) \
    } \


