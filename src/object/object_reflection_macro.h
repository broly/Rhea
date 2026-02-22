#pragma once

import <set>;
import <optional>;
import <string_view>;
import <json/value.h>;
import dependency_collector;

#include "common/reflect_macros.h"

#define REFL_OBJECT_TRAITS(cls, base) \
        export template<> \
        struct RhObjectTraits<cls> {\
            static constexpr std::string_view type_name = #cls; \
            static std::set<std::string_view> get_bases() {\
                return get_bases_impl<cls, base>(#cls); \
            }\
            inline static std::set<std::string_view> bases = RhObjectTraits<cls>::get_bases(); \
        }; \

#define REFLECT_OBJECT(cls, base, ...) \
    export namespace reflect_inner { \
        static_assert(std::is_base_of<RhObject, cls>::value, "Can't reflect non-RhObject types");\
        const bool cls##_registered = \
            reflect::register_object_class<cls>(\
                #cls, \
                std::nullopt \
            ); \
        REFL_OBJECT_TRAITS(cls, base) \
    } \
    



#define REFLECT_OBJECT_FIELDS(cls, base, ...) \
    REFLECT_STRUCT(cls, __VA_ARGS__); \
    export namespace reflect_inner { \
        static_assert(std::is_base_of<RhObject, cls>::value, "Can't reflect non-RhObject types");\
        const bool cls##_registered = \
            reflect::register_object_class<cls>(\
                #cls, \
                    [] (const Json::Value& json_object, RhObject* ObjPtr, const SerializationContext& context) -> bool { \
                        using Class = cls;\
                        auto CastedObjPtr = reinterpret_cast<cls*>(ObjPtr); \
                        reflect::json::visit_serialize(json_object, *CastedObjPtr, context); \
                        CastedObjPtr->on_serialize(context); \
                        return true; \
                    }\
                ); \
        REFL_OBJECT_TRAITS(cls, base) \
    } \


