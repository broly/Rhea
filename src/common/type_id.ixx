export module type_id;

import std.compat;

import name;

// Qualified type name via C++26 reflection, e.g. "CameraUBO", "vk::DescriptorSetLayoutData".
// Replaces parsing __FUNCSIG__ / source_location, whose format is compiler specific.
template<typename T>
consteval std::string_view get_unique_id()
{
	constexpr std::meta::info type = std::meta::dealias(^^T);
	std::string name(std::meta::display_string_of(type));

	if constexpr (std::meta::is_class_type(type) || std::meta::is_enum_type(type))
	{
		for (std::meta::info scope = std::meta::parent_of(type);
			 scope != ^^::;
			 scope = std::meta::parent_of(scope))
		{
			if (std::meta::has_identifier(scope))
				name = std::string(std::meta::identifier_of(scope)) + "::" + name;
		}
	}
	return std::define_static_string(name);
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
	
	bool operator<(const TypeId& other) const
	{
		return name < other.name;
	}

	bool operator==(const TypeId& other) const
    {
        return name == other.name;
    }
	bool operator!=(const TypeId& other) const
	{
		return !operator==(other);
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



