export module guard_value;

export template<typename T>
struct Guard
{
    Guard(T& in_ref)
        : ref(in_ref)
        , old_value(in_ref)
    {}
    
    Guard(T& in_ref, const T& new_value)
        : ref(in_ref)
        , old_value(in_ref)
    {
        ref = new_value;
    }
    
    ~Guard()
    {
        ref = old_value;
    }
    
    T& ref;
    T old_value;
};

template<typename T>
Guard(T& ref) -> Guard<T>;