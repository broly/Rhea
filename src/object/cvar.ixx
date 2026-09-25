export module cvar;

import std.compat;
import reflect;
import properties;
import type_id;
import name;


// Console variables: named runtime settings from any module, listed in the debug UI catalog
// (grouped by the dotted name) and optionally saved to cache/cvars.json.
//
//     cvar::Var<float> cv_move_speed("camera.move_speed", 6.f, "Free camera speed, m/s",
//                                    {.has_range = true, .min = 0.1f, .max = 50.f});
//     cv_move_speed.get();
//
// Values are edited through their reflect::PropertyDesc, so any type the property editor
// supports works (bool, numbers, enums, strings, vectors, ...).
export namespace cvar
{
    enum Flags : uint32_t
    {
        none = 0,
        persistent = 1 << 0,   // saved / loaded with the catalog
        read_only = 1 << 1,
    };

    class Entry
    {
    public:
        virtual ~Entry() = default;

        const std::string& get_name() const { return name; }
        const std::string& get_description() const { return description; }
        uint32_t get_flags() const { return flags; }

        // property describing the value, offset 0 from value_ptr()
        const reflect::PropertyDesc& get_property() const { return property; }
        virtual void* value_ptr() = 0;

        // the value was modified in place through value_ptr()
        virtual void notify_changed() = 0;
        virtual void reset_to_default() = 0;
        virtual bool is_default() const = 0;

        // persistence, empty / false for types without a text form
        virtual std::string to_string() const = 0;
        virtual bool from_string(std::string_view text) = 0;

    protected:
        std::string name;
        std::string description;
        uint32_t flags = none;
        reflect::PropertyDesc property;
    };

    void register_entry(Entry* entry);
    void unregister_entry(Entry* entry);

    // sorted by name
    std::vector<Entry*> get_entries();
    Entry* find(std::string_view name);

    // cache/cvars.json by default
    bool save(const std::filesystem::path& path = {});
    bool load(const std::filesystem::path& path = {});

    namespace detail
    {
        template<typename T>
        std::string value_to_string(const T& value)
        {
            if constexpr (std::is_same_v<T, bool>)
                return value ? "true" : "false";
            else if constexpr (std::is_enum_v<T>)
                return reflect::enum_name(value).to_string();
            else if constexpr (std::is_arithmetic_v<T>)
                return std::to_string(value);
            else if constexpr (std::is_same_v<T, std::string>)
                return value;
            else if constexpr (std::is_same_v<T, Name>)
                return value.to_string();
            else
                return {};
        }

        template<typename T>
        bool value_from_string(T& value, std::string_view text)
        {
            if constexpr (std::is_same_v<T, bool>)
            {
                value = text == "true" || text == "1";
                return true;
            }
            else if constexpr (std::is_enum_v<T>)
            {
                if (!reflect::is_valid_enum_name<T>(Name(text)))
                    return false;
                value = reflect::name_to_enum<T>(Name(text));
                return true;
            }
            else if constexpr (std::is_arithmetic_v<T>)
            {
                return std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{};
            }
            else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, Name>)
            {
                value = std::string(text);
                return true;
            }
            else
                return false;
        }
    }

    template<typename T>
    class Var final : public Entry
    {
    public:
        using Callback = std::function<void(const T& value)>;

        Var(std::string_view in_name, T in_default, std::string_view in_description = {},
            const reflect::PropertyMeta& meta = {}, uint32_t in_flags = persistent)
            : value(in_default)
            , default_value(std::move(in_default))
        {
            name = in_name;
            description = in_description;
            flags = in_flags;
            reflect::PropertyMeta property_meta = meta;
            property_meta.read_only |= (flags & read_only) != 0;
            // the property name is the last segment of the dotted name
            const size_t dot = name.find_last_of('.');
            const std::string_view short_name = dot == std::string::npos
                ? std::string_view(name)
                : std::string_view(name).substr(dot + 1);
            property = reflect::make_property<T>(short_name, 0, property_meta);
            register_entry(this);
        }

        ~Var() override
        {
            unregister_entry(this);
        }

        Var(const Var&) = delete;
        Var& operator=(const Var&) = delete;

        const T& get() const { return value; }
        operator const T&() const { return value; }

        void set(const T& new_value)
        {
            if constexpr (std::equality_comparable<T>)
                if (value == new_value)
                    return;
            value = new_value;
            notify_changed();
        }

        Var& operator=(const T& new_value)
        {
            set(new_value);
            return *this;
        }

        // called after every change (from code, the UI or loading)
        void on_changed(Callback callback)
        {
            callbacks.push_back(std::move(callback));
        }

        void* value_ptr() override { return &value; }

        void notify_changed() override
        {
            for (const auto& callback : callbacks)
                callback(value);
        }

        void reset_to_default() override
        {
            set(default_value);
        }

        bool is_default() const override
        {
            if constexpr (std::equality_comparable<T>)
                return value == default_value;
            else
                return false;
        }

        std::string to_string() const override
        {
            return detail::value_to_string(value);
        }

        bool from_string(std::string_view text) override
        {
            T parsed = value;
            if (!detail::value_from_string(parsed, text))
                return false;
            set(parsed);
            return true;
        }

    private:
        T value;
        T default_value;
        std::vector<Callback> callbacks;
    };
}
