#pragma once

import assertions;
import <source_location>;

#define ENSURE_IMPL(v) \
    [](decltype(v) value, std::source_location sl = std::source_location::current()) -> decltype(v) \
    { \
        if (!value) \
        { \
            print_error_no_text(sl); \
            __debugbreak(); \
        }\
        return value; \
    }(v)

#define ensure(v) (ENSURE_IMPL(v))


#define checkf(assertion, text, ...) \
    do \
    { \
        if (!(assertion)) \
        { \
            print_error(std::source_location::current(), text, ##__VA_ARGS__); \
            __debugbreak(); \
            std::terminate(); \
        }\
    } while (false)

#define unreachable(text, ...) \
    {\
        print_error(std::source_location::current(), text, ##__VA_ARGS__); \
        __debugbreak(); \
        std::terminate(); \
    }