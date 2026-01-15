export module type_id;

#include <source_location>
import <string_view>;
import name;


#ifndef _MSC_VER
consteval std::string_view ExtractTypeNameFromSourceLocation(std::string_view input) 
{
    auto start = input.find("[T = ");
    if (start == -1) 
        return {};

    start += 5;

    auto end = input.find("]");
    if (end == -1) 
        return {};

    return input.substr(start, end - start);
}
#else
consteval std::string_view extract_type(std::string_view input) 
{
	int32_t start_index = input.find("<");
    if (start_index == -1) 
        return {};

    start_index += 1; 

	int32_t end_index = input.find(">", start_index);
    if (end_index == -1) 
        return {}; 

    return input.substr(start_index, end_index - start_index);
}
#endif

template<typename>
consteval static std::string_view get_unique_id(std::source_location location = std::source_location::current())
{
	return extract_type(location.function_name());
}

export struct TypeId
{
	TypeId(const TypeId&) = default;

	TypeId(Name InTypeName)
		: name(InTypeName)
	{}
	
	TypeId(nullptr_t)
		: name(NAME_None)
	{}
	
	operator bool() const
	{
		return !name.is_none();
	}
	
	Name name;

	bool operator==(const TypeId& Other) const
    {
        return name == Other.name;
    }
	bool operator!=(const TypeId& Other) const
	{
		return !operator==(Other);
	}

	~TypeId() = default;
};


export template<typename T>
TypeId get_type_id()
{
	static Name TypeName = get_unique_id<T>();
	return TypeName;
}


export template<>
struct std::hash<TypeId>
{
	size_t operator()(const TypeId& h) const noexcept
	{
		return h.name.get_id();
	}
};



