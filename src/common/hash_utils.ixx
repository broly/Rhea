export module hash_utils;

export void hash_combine(size_t& seed, size_t v)
{
    seed ^= v + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}