#pragma once
#include <cstdint>
#include <iostream>
#include <ostream>
#include <utility>

#include "common/fixed_string.h"

enum ELogOutputDetailsMask
{
    no_detail = 0,
    file = 0x1,
    line = 0x2,
    function = 0x4,
};

inline void print_error(const char* message)
{
    std::cerr << message << std::endl;
}
inline void print_text(const char* message)
{
    std::cout << message << std::endl;
}

using LogPrinter = void(*)(const char*);


struct LogVerbosity
{
    ELogOutputDetailsMask details;
    uint8_t verbosity_level;
    LogPrinter log_printer;
    
    constexpr LogVerbosity(uint8_t level, ELogOutputDetailsMask details, LogPrinter in_printer) noexcept
        : details(details)
        , verbosity_level(level)
    {
        log_printer = in_printer;
    }
    
    void output(const char* message) const
    {
        log_printer(message);
    }
};

template<size_t verbosity_level>
struct LogVerbosityLevelTraits
{
    
};


#define DEFINE_VERBOSITY(name, level, details, printer) \
    static inline constexpr auto name = LogVerbosity(level, static_cast<ELogOutputDetailsMask>(details), &printer); \
    template<> \
    struct LogVerbosityLevelTraits<level> \
    { \
        static constexpr inline LogVerbosity verbosity = name; \
    }

DEFINE_VERBOSITY(Critical, 0, file | line | function, print_error);
DEFINE_VERBOSITY(Error, 1, file | line | function, print_error);
DEFINE_VERBOSITY(Warning, 2, no_detail, print_error);
DEFINE_VERBOSITY(DisplayFn, 3, function, print_text);
DEFINE_VERBOSITY(Log, 4, no_detail, print_text);

#define DEFAULT_GLOBAL_VERBOSITY DisplayFn


template<size_t LogNameSize>
struct Logger
{
    constexpr Logger(FixedString<LogNameSize> in_name, LogVerbosity in_default_verbosity) noexcept
        : name(in_name)
        , default_verbosity_level(in_default_verbosity.verbosity_level)
    {
        
    }
    
    template<LogVerbosity verbosity = DEFAULT_GLOBAL_VERBOSITY, typename Fmt, typename... Args>
    void Log(Fmt&& fmt, Args&&... args) const
    {
        if (verbosity.verbosity_level <= default_verbosity_level)
        {
            char const buffer[512] = {};
            sprintf_s(
                (char* const)buffer, 
                512, 
                (const char*)fmt, 
                std::forward<Args>(args)...);
            
            
            char const buffer2[512] = {};
            sprintf_s(
                (char* const)buffer2, 
                512, 
                "%s: %s", 
                name.Buffer, buffer);
            
            verbosity.output(buffer2);
        }
    }
    
    FixedString<LogNameSize> name;
    size_t default_verbosity_level;
};


template<size_t Num>
Logger(char const (&)[Num], LogVerbosity DefaultVerbosity) -> Logger<Num - 1>;

#define DEFINE_LOGGER(LoggerName, DefaultVerbosity) \
    constexpr Logger LoggerName(#LoggerName, DefaultVerbosity)

DEFINE_LOGGER(LogTemp, DisplayFn);
