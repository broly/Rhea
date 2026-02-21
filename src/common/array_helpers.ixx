export module array_helpers;

export template <typename T, size_t N>
constexpr size_t array_size(T (&)[N]) noexcept
{
    return N;
}