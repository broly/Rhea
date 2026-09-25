#pragma once

#include "common/reflect_macros.h"

// Registers an RhObject subclass: factory, default object and JSON serializer.
// Class name and bases come from C++26 reflection; serialized fields are the ones
// marked [[=rh::serialize]] in the class and its bases.
#define RH_OBJECT(cls) \
    namespace { \
        [[maybe_unused]] const bool RH_CONCAT(rh_object_registered_, __LINE__) = ::reflect::register_object_class<cls>(); \
    }
