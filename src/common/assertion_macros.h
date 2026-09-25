#pragma once

// Breaks into the debugger right at the call site. __builtin_debugtrap needs no
// declaration, unlike __debugbreak, whose implicit declarations clash across modules.
#ifndef RH_DEBUGBREAK
#if defined(__clang__)
#define RH_DEBUGBREAK() __builtin_debugtrap()
#else
#define RH_DEBUGBREAK() __debugbreak()
#endif
#endif


#define ENSURE_IMPL(v) \
    [](decltype(v) value, std::source_location sl = std::source_location::current()) -> decltype(v) \
    { \
        if (!value) \
        { \
            print_error_no_text(sl); \
            RH_DEBUGBREAK(); \
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
            RH_DEBUGBREAK(); \
            std::terminate(); \
        }\
    } while (false)

#define check(assertion) \
    do \
    { \
        if (!(assertion)) \
        { \
            print_error(std::source_location::current(), "assertion failed"); \
            RH_DEBUGBREAK(); \
            std::terminate(); \
        }\
    } while (false)


#define checked(assertion, ...) \
    [&] () \
    { \
        auto result = assertion; \
        check(result); \
        return result; \
    }()


#define unreachable(text, ...) \
    {\
        print_error(std::source_location::current(), text, ##__VA_ARGS__); \
        RH_DEBUGBREAK(); \
        std::terminate(); \
    }

#define todo(...) \
    {\
        print_error(std::source_location::current(), "todo: " __VA_ARGS__); \
        RH_DEBUGBREAK(); \
        std::terminate(); \
    }

#define pure \
    { \
        unreachable("Pure virtual function call "); \
    }