module voxel;

import std.compat;
import glm;

import :grid;
import :voxelize;

namespace
{
    using namespace voxel;

    float dot3(glm::vec3 a, glm::vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    glm::vec3 cross3(glm::vec3 a, glm::vec3 b)
    {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }
    float length3(glm::vec3 a) { return std::sqrt(dot3(a, a)); }

    // ---- colors ----

    struct SrgbTable
    {
        std::array<float, 256> to_linear{};
        SrgbTable()
        {
            for (int i = 0; i < 256; ++i)
            {
                const float c = float(i) / 255.0f;
                to_linear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
            }
        }
    };
    const SrgbTable& srgb_table()
    {
        static const SrgbTable table;
        return table;
    }

    uint8_t to_srgb8(float linear)
    {
        linear = std::clamp(linear, 0.0f, 1.0f);
        const float c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return uint8_t(std::lround(c * 255.0f));
    }

    glm::vec4 unpack_linear(uint32_t texel)
    {
        const auto& lut = srgb_table().to_linear;
        return { lut[texel & 0xff], lut[(texel >> 8) & 0xff], lut[(texel >> 16) & 0xff], float(texel >> 24) / 255.0f };
    }

    uint32_t pack_srgb(glm::vec4 linear)
    {
        const uint32_t a = uint32_t(std::lround(std::clamp(linear.w, 0.0f, 1.0f) * 255.0f));
        return uint32_t(to_srgb8(linear.x)) | uint32_t(to_srgb8(linear.y)) << 8 | uint32_t(to_srgb8(linear.z)) << 16 | a << 24;
    }

    // Box filtered levels of an image (linear light), level 0 = the image
    struct MipChain
    {
        struct Level
        {
            uint32_t width = 0;
            uint32_t height = 0;
            std::vector<glm::vec4> texels;
        };
        std::vector<Level> levels;

        explicit MipChain(const Image& image)
        {
            Level base{ image.width, image.height, {} };
            base.texels.resize(size_t(image.width) * image.height);
            for (size_t i = 0; i < base.texels.size(); ++i)
                base.texels[i] = unpack_linear(image.texels[i]);
            levels.push_back(std::move(base));
            while (levels.back().width > 1 || levels.back().height > 1)
            {
                const Level& from = levels.back();
                Level next{ std::max(from.width / 2, 1u), std::max(from.height / 2, 1u), {} };
                next.texels.resize(size_t(next.width) * next.height);
                for (uint32_t y = 0; y < next.height; ++y)
                {
                    for (uint32_t x = 0; x < next.width; ++x)
                    {
                        glm::vec4 sum(0.0f);
                        for (uint32_t dy = 0; dy < 2; ++dy)
                            for (uint32_t dx = 0; dx < 2; ++dx)
                            {
                                const uint32_t sx = std::min(x * 2 + dx, from.width - 1);
                                const uint32_t sy = std::min(y * 2 + dy, from.height - 1);
                                sum += from.texels[size_t(sy) * from.width + sx];
                            }
                        next.texels[size_t(y) * next.width + x] = sum * 0.25f;
                    }
                }
                levels.push_back(std::move(next));
            }
        }

        // bilinear, repeating
        glm::vec4 sample(glm::vec2 uv, uint32_t level_index) const
        {
            const Level& level = levels[std::min<size_t>(level_index, levels.size() - 1)];
            const float fx = uv.x * float(level.width) - 0.5f;
            const float fy = uv.y * float(level.height) - 0.5f;
            const float x0f = std::floor(fx), y0f = std::floor(fy);
            const float tx = fx - x0f, ty = fy - y0f;
            auto wrap = [] (int64_t v, uint32_t size) { const int64_t m = v % int64_t(size); return uint32_t(m < 0 ? m + size : m); };
            const uint32_t x0 = wrap(int64_t(x0f), level.width), x1 = wrap(int64_t(x0f) + 1, level.width);
            const uint32_t y0 = wrap(int64_t(y0f), level.height), y1 = wrap(int64_t(y0f) + 1, level.height);
            auto at = [&] (uint32_t x, uint32_t y) { return level.texels[size_t(y) * level.width + x]; };
            const glm::vec4 top = at(x0, y0) * (1.0f - tx) + at(x1, y0) * tx;
            const glm::vec4 bottom = at(x0, y1) * (1.0f - tx) + at(x1, y1) * tx;
            return top * (1.0f - ty) + bottom * ty;
        }
    };

    constexpr std::array<glm::ivec3, 6> neighbours{ glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0),
                                                    glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1) };
}

namespace voxel
{
    bool triangle_overlaps_box(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 box_center, glm::vec3 box_half)
    {
        const std::array<glm::vec3, 3> v{ a - box_center, b - box_center, c - box_center };

        // the box's faces
        for (int axis = 0; axis < 3; ++axis)
        {
            const float lo = std::min({ v[0][axis], v[1][axis], v[2][axis] });
            const float hi = std::max({ v[0][axis], v[1][axis], v[2][axis] });
            if (lo > box_half[axis] || hi < -box_half[axis])
                return false;
        }

        auto separated = [&] (glm::vec3 axis)
        {
            const float p0 = dot3(axis, v[0]), p1 = dot3(axis, v[1]), p2 = dot3(axis, v[2]);
            const float r = box_half.x * std::abs(axis.x) + box_half.y * std::abs(axis.y) + box_half.z * std::abs(axis.z);
            return std::min({ p0, p1, p2 }) > r || std::max({ p0, p1, p2 }) < -r;
        };

        // the triangle's plane
        const std::array<glm::vec3, 3> edges{ v[1] - v[0], v[2] - v[1], v[0] - v[2] };
        if (separated(cross3(edges[0], edges[1])))
            return false;

        // edge x box axis
        constexpr std::array<glm::vec3, 3> units{ glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1) };
        for (const glm::vec3& edge : edges)
            for (const glm::vec3& unit : units)
                if (separated(cross3(edge, unit)))
                    return false;
        return true;
    }

    // Ericson, Real-Time Collision Detection 5.1.5
    glm::vec3 closest_point_barycentric(glm::vec3 p, glm::vec3 a, glm::vec3 b, glm::vec3 c)
    {
        const glm::vec3 ab = b - a, ac = c - a, ap = p - a;
        const float d1 = dot3(ab, ap), d2 = dot3(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f)
            return { 1.0f, 0.0f, 0.0f };

        const glm::vec3 bp = p - b;
        const float d3 = dot3(ab, bp), d4 = dot3(ac, bp);
        if (d3 >= 0.0f && d4 <= d3)
            return { 0.0f, 1.0f, 0.0f };

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        {
            const float t = d1 / (d1 - d3);
            return { 1.0f - t, t, 0.0f };
        }

        const glm::vec3 cp = p - c;
        const float d5 = dot3(ab, cp), d6 = dot3(ac, cp);
        if (d6 >= 0.0f && d5 <= d6)
            return { 0.0f, 0.0f, 1.0f };

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        {
            const float t = d2 / (d2 - d6);
            return { 1.0f - t, 0.0f, t };
        }

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        {
            const float t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return { 0.0f, 1.0f - t, t };
        }

        const float denom = va + vb + vc;
        if (!(std::abs(denom) > 0.0f))
            return { 1.0f, 0.0f, 0.0f };        // degenerate triangle
        const float v = vb / denom, w = vc / denom;
        return { 1.0f - v - w, v, w };
    }

    std::optional<VoxelGrid> voxelize(std::span<const VoxelizePrimitive> primitives,
        std::span<const VoxelizeMaterial> materials, const VoxelizeSettings& settings, VoxelizeStats* stats,
        std::string* error)
    {
        const auto start = std::chrono::steady_clock::now();
        auto fail = [&] (std::string reason) -> std::optional<VoxelGrid>
        {
            if (error)
                *error = std::move(reason);
            return std::nullopt;
        };

        const float voxel_size = settings.voxel_size;
        if (!(voxel_size > 0.0f))
            return fail("voxel size must be > 0");

        auto material_of = [&] (const VoxelizePrimitive& primitive) -> const VoxelizeMaterial*
        {
            return primitive.material < materials.size() ? &materials[primitive.material] : nullptr;
        };

        // ---- the working box ----
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
        uint64_t triangle_count = 0;
        for (const VoxelizePrimitive& primitive : primitives)
        {
            const VoxelizeMaterial* material = material_of(primitive);
            if (material && material->skip)
                continue;
            for (const uint32_t index : primitive.indices)
            {
                if (index >= primitive.positions.size())
                    return fail(std::format("vertex index {} out of {} vertices", index, primitive.positions.size()));
                const glm::vec3 p = primitive.positions[index];
                for (int axis = 0; axis < 3; ++axis)
                {
                    lo[axis] = std::min(lo[axis], p[axis]);
                    hi[axis] = std::max(hi[axis], p[axis]);
                }
            }
            triangle_count += primitive.indices.size() / 3;
        }
        if (triangle_count == 0)
            return fail("no triangles to voxelize");

        glm::vec3 origin;
        glm::ivec3 dims;
        for (int axis = 0; axis < 3; ++axis)
        {
            // two voxels of margin: a face exactly on a voxel boundary touches the voxels on both sides
            origin[axis] = (std::floor(lo[axis] / voxel_size) - 2.0f) * voxel_size;
            dims[axis] = int32_t(std::floor((hi[axis] - origin[axis]) / voxel_size)) + 3;
        }
        const uint64_t voxel_count = uint64_t(dims.x) * uint64_t(dims.y) * uint64_t(dims.z);
        if (voxel_count > settings.max_voxels)
            return fail(std::format("{} x {} x {} voxels: more than {}", dims.x, dims.y, dims.z, settings.max_voxels));

        auto index_of = [&] (glm::ivec3 v) { return size_t(v.x) + size_t(dims.x) * (size_t(v.y) + size_t(dims.y) * size_t(v.z)); };
        auto inside = [&] (glm::ivec3 v) { return v.x >= 0 && v.y >= 0 && v.z >= 0 && v.x < dims.x && v.y < dims.y && v.z < dims.z; };

        // ---- surface ----
        std::vector<float> best(voxel_count, std::numeric_limits<float>::max());      // squared distance
        std::vector<uint32_t> colors(voxel_count, 0);

        std::map<const Image*, MipChain> mips;
        const glm::vec3 half(voxel_size * 0.5f * 1.0001f);
        const float eps = 1e-4f;

        for (const VoxelizePrimitive& primitive : primitives)
        {
            const VoxelizeMaterial* material = material_of(primitive);
            if (material && material->skip)
                continue;
            const glm::vec4 factor = material ? material->color : glm::vec4(1.0f);
            const Image* image = material && material->base_color && material->base_color->width > 0 &&
                material->base_color->texels.size() == size_t(material->base_color->width) * material->base_color->height &&
                primitive.uvs.size() == primitive.positions.size() ? material->base_color : nullptr;
            const MipChain* chain = nullptr;
            if (image)
                chain = &mips.try_emplace(image, *image).first->second;
            const glm::vec4 flat_color(srgb_table().to_linear[uint32_t(std::lround(std::clamp(factor.x, 0.0f, 1.0f) * 255.0f))],
                srgb_table().to_linear[uint32_t(std::lround(std::clamp(factor.y, 0.0f, 1.0f) * 255.0f))],
                srgb_table().to_linear[uint32_t(std::lround(std::clamp(factor.z, 0.0f, 1.0f) * 255.0f))], 1.0f);

            for (size_t t = 0; t + 2 < primitive.indices.size(); t += 3)
            {
                const uint32_t i0 = primitive.indices[t], i1 = primitive.indices[t + 1], i2 = primitive.indices[t + 2];
                const glm::vec3 a = primitive.positions[i0], b = primitive.positions[i1], c = primitive.positions[i2];

                // mip level with texels of about a voxel on this triangle
                uint32_t level = 0;
                if (chain)
                {
                    const glm::vec2 ta = primitive.uvs[i0], tb = primitive.uvs[i1], tc = primitive.uvs[i2];
                    const float uv_area = 0.5f * std::abs((tb.x - ta.x) * (tc.y - ta.y) - (tc.x - ta.x) * (tb.y - ta.y)) *
                        float(image->width) * float(image->height);
                    const float area = 0.5f * length3(cross3(b - a, c - a));
                    if (area > 1e-12f && uv_area > 0.0f)
                    {
                        const float footprint = voxel_size * std::sqrt(uv_area / area);
                        // rounded down: texels a bit smaller than the voxel keep the surface's contrast (a texel
                        // as large as the voxel or larger averages the planks into one flat color)
                        if (footprint > 1.0f)
                            level = uint32_t(std::floor(std::log2(footprint)));
                    }
                }

                glm::ivec3 first, last;
                for (int axis = 0; axis < 3; ++axis)
                {
                    const float tmin = std::min({ a[axis], b[axis], c[axis] });
                    const float tmax = std::max({ a[axis], b[axis], c[axis] });
                    first[axis] = std::clamp(int32_t(std::floor((tmin - origin[axis]) / voxel_size - eps)), 0, dims[axis] - 1);
                    last[axis] = std::clamp(int32_t(std::floor((tmax - origin[axis]) / voxel_size + eps)), 0, dims[axis] - 1);
                }

                for (int32_t z = first.z; z <= last.z; ++z)
                    for (int32_t y = first.y; y <= last.y; ++y)
                        for (int32_t x = first.x; x <= last.x; ++x)
                        {
                            const glm::vec3 center = origin + (glm::vec3(float(x), float(y), float(z)) + 0.5f) * voxel_size;
                            if (!triangle_overlaps_box(a, b, c, center, half))
                                continue;
                            const glm::vec3 w = closest_point_barycentric(center, a, b, c);
                            const glm::vec3 closest = a * w.x + b * w.y + c * w.z;
                            const glm::vec3 d = closest - center;
                            const float distance = dot3(d, d);
                            const size_t index = index_of({ x, y, z });
                            if (distance >= best[index])
                                continue;
                            best[index] = distance;
                            glm::vec4 color = flat_color;
                            if (chain)
                            {
                                const glm::vec2 uv = primitive.uvs[i0] * w.x + primitive.uvs[i1] * w.y + primitive.uvs[i2] * w.z;
                                const glm::vec4 texel = chain->sample(uv, level);
                                color = glm::vec4(texel.x * flat_color.x, texel.y * flat_color.y, texel.z * flat_color.z, 1.0f);
                            }
                            colors[index] = pack_srgb(color);
                        }
            }
        }

        // ---- fill ----
        // state: 0 empty (enclosed until the flood reaches it), 1 solid, 2 outside
        std::vector<uint8_t> state(voxel_count, 0);
        uint64_t surface_count = 0;
        for (size_t i = 0; i < voxel_count; ++i)
            if (best[i] != std::numeric_limits<float>::max())
            {
                state[i] = 1;
                ++surface_count;
            }
        best = {};

        std::vector<glm::ivec3> queue;
        queue.reserve(1 << 16);
        auto flood = [&] (glm::ivec3 seed, uint8_t from, uint8_t to, auto&& visit)
        {
            queue.clear();
            state[index_of(seed)] = to;
            queue.push_back(seed);
            for (size_t head = 0; head < queue.size(); ++head)
            {
                const glm::ivec3 v = queue[head];
                visit(v);
                for (const glm::ivec3& n : neighbours)
                {
                    const glm::ivec3 next = v + n;
                    if (!inside(next) || state[index_of(next)] != from)
                        continue;
                    state[index_of(next)] = to;
                    queue.push_back(next);
                }
            }
        };
        // the outside: from every empty voxel of the border layer (the margin keeps it empty)
        for (int32_t z = 0; z < dims.z; ++z)
            for (int32_t y = 0; y < dims.y; ++y)
                for (int32_t x = 0; x < dims.x; ++x)
                {
                    const bool border = x == 0 || y == 0 || z == 0 || x == dims.x - 1 || y == dims.y - 1 || z == dims.z - 1;
                    if (border && state[index_of({ x, y, z })] == 0)
                        flood({ x, y, z }, 0, 2, [] (glm::ivec3) {});
                }

        uint64_t filled_count = 0;
        if (settings.fill_thickness > 0)
        {
            // distance (voxels, 6-connected) of every enclosed voxel to the surface, and the color of the surface
            // voxel it came from
            constexpr uint16_t far = 0xffff;
            std::vector<uint16_t> distance(voxel_count, far);
            std::vector<glm::ivec3> wave;
            for (int32_t z = 0; z < dims.z; ++z)
                for (int32_t y = 0; y < dims.y; ++y)
                    for (int32_t x = 0; x < dims.x; ++x)
                        if (state[index_of({ x, y, z })] == 1)
                        {
                            distance[index_of({ x, y, z })] = 0;
                            wave.push_back({ x, y, z });
                        }
            for (size_t head = 0; head < wave.size(); ++head)
            {
                const glm::ivec3 v = wave[head];
                const size_t index = index_of(v);
                for (const glm::ivec3& n : neighbours)
                {
                    const glm::ivec3 next = v + n;
                    if (!inside(next))
                        continue;
                    const size_t next_index = index_of(next);
                    if (state[next_index] != 0 || distance[next_index] != far)
                        continue;
                    distance[next_index] = uint16_t(std::min<uint32_t>(distance[index] + 1u, far - 1u));
                    colors[next_index] = colors[index];
                    wave.push_back(next);
                }
            }

            // enclosed regions: filled when thin (3 marks a region being visited, 4 a region kept empty)
            std::vector<glm::ivec3> region;
            for (int32_t z = 0; z < dims.z; ++z)
                for (int32_t y = 0; y < dims.y; ++y)
                    for (int32_t x = 0; x < dims.x; ++x)
                    {
                        if (state[index_of({ x, y, z })] != 0)
                            continue;
                        region.clear();
                        uint16_t thickest = 0;
                        flood({ x, y, z }, 0, 3, [&] (glm::ivec3 v)
                        {
                            region.push_back(v);
                            thickest = std::max(thickest, distance[index_of(v)]);
                        });
                        const bool fill = thickest <= settings.fill_thickness;
                        for (const glm::ivec3& v : region)
                            state[index_of(v)] = fill ? 1 : 4;
                        if (fill)
                            filled_count += region.size();
                    }
        }

        // ---- the grid ----
        VoxelGrid grid(voxel_size, origin);
        for (int32_t z = 0; z < dims.z; ++z)
            for (int32_t y = 0; y < dims.y; ++y)
                for (int32_t x = 0; x < dims.x; ++x)
                {
                    const size_t index = index_of({ x, y, z });
                    if (state[index] != 1)
                        continue;
                    Voxel voxel = Voxel::unpack(colors[index]);
                    voxel.density = 255;
                    grid.set({ x, y, z }, voxel);
                }

        if (stats)
        {
            stats->triangles = triangle_count;
            stats->surface_voxels = surface_count;
            stats->filled_voxels = filled_count;
            stats->size = dims;
            stats->milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }
        return grid;
    }
}
