#pragma once

#define RH_CONCAT_INNER(a, b) a##b
#define RH_CONCAT(a, b) RH_CONCAT_INNER(a, b)

// Registers T in the runtime type registry (reflect::find_runtime_info by type name), used for
// UBOs / push constants / vertex types referenced by name from render schemas.
// Fields are taken from C++26 reflection (see reflect::fields_of), [[=rh::padding]] ones are skipped.
#define RH_REGISTER_TYPE(T) \
    namespace { \
        [[maybe_unused]] const bool RH_CONCAT(rh_type_registered_, __LINE__) = ::reflect::register_type_runtime_info<T>(); \
    }
