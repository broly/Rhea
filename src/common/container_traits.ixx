export module container_traits;
import enum_helpers;
import <vector>;
import <optional>;
import <map>;
import <set>;
import <memory>;
import <variant>;

export
{
    
    /// std::vector

    template<typename T>
    struct is_vector
    {
        static bool constexpr value = false;
    };

    template<typename T>
    struct is_vector<std::vector<T> > 
    {
        static bool constexpr value = true;
    };

    template<typename T>
    constexpr bool is_vector_v = is_vector<T>::value;


    /// std::optional

    template<typename T>
    struct is_optional
    {
        static bool constexpr value = false;
    };

    template<typename T>
    struct is_optional<std::optional<T>> 
    {
        static bool constexpr value = true;
    };

    template<typename T>
    constexpr bool is_optional_v = is_optional<T>::value;



    /// std::map

    template<typename>
    struct is_map 
    {
        static bool constexpr value = false;
    };

    template<typename K, typename V>
    struct is_map<std::map<K, V> > 
    {
        static bool constexpr value = true;
    };

    template<typename T>
    constexpr bool is_map_v = is_map<T>::value;


    /// set::set

    template<typename>
    struct is_set
    {
        static bool constexpr value = false;
    };

    template<typename... Ts>
    struct is_set<std::set<Ts...> > 
    {
        static bool constexpr value = true;
    };

    template<typename T>
    constexpr bool is_set_v = is_set<T>::value;


    /// Mask

    template<typename>
    struct is_mask
    {
        static bool constexpr value = false;
    };

    template<typename... Ts>
    struct is_mask<Mask<Ts...> > 
    {
        static bool constexpr value = true;
    };

    template<typename T>
    constexpr bool is_mask_v = is_mask<T>::value;


    /// std::shared_ptr

    template<typename>
    struct is_shared_ptr 
    {
        static bool constexpr value = false;
    };

    template<typename T>
    struct is_shared_ptr<std::shared_ptr<T> > 
    {
        static bool constexpr value = true;
    };

    template<typename T>
    constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;


    /// std::variant

    template<typename>
    struct is_variant 
    {
        static bool constexpr value = false;
    };

    template<typename... Ts>
    struct is_variant<std::variant<Ts...>> 
    {
        static bool constexpr value = true;
    };

    template<typename T>
    constexpr bool is_variant_v = is_variant<T>::value;

}
