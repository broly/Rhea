export module fixed_string;

export template<size_t Num>
struct FixedString
{
    char buffer[Num + 1]{};
    
    constexpr FixedString(char const* s)
    {
        for (size_t i = 0; i != Num; ++i)
            buffer[i] = s[i];
    }
    constexpr operator char const*() const
    {
        return buffer;
    }
    
    template<size_t OtherNum>
    constexpr bool operator==(const FixedString<OtherNum>& other) const
    {
        if (Num != OtherNum)
            return false;
        
        for (size_t i = 0; i < Num; ++i)
            if (buffer[i] != other.Buffer[i])
                return false;
        return true;
    }
    
    constexpr bool operator!=(const FixedString& other) const
    {
        return !(*this == other);
    }
    
    template<size_t OtherNum>
    constexpr bool operator==(const char (&other)[OtherNum]) const
    {
        if (Num != OtherNum - 1)
            return false;
        
        for (size_t i = 0; i < Num; ++i)
            if (buffer[i] != other[i])
                return false;
        return true;
    }
    
    template<size_t OtherNum>
    constexpr bool operator!=(const char (&other)[OtherNum]) const
    {
        if (Num == OtherNum - 1)
            return false;
        
        for (size_t i = 0; i < Num; ++i)
            if (buffer[i] == other[i])
                return true;
        return false;
    }
};
template<size_t Num>
FixedString(char const (&)[Num]) -> FixedString<Num - 1>;

export template<FixedString FS>
constexpr auto operator"" _fixed ()
{
    return FS;
}

#define CONCAT_TWO(a, b) a##b
#define FIXED_STRING(s) CONCAT_TWO(#s,_fixed)


static_assert(FixedString("qwe") == "qwe");
static_assert(FixedString("qwe") != "qwe2");