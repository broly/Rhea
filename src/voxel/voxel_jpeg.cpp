module voxel;

import std.compat;
import glm;

import :grid;
import :jpeg;

namespace
{
    using namespace voxel;

    constexpr JpegShapeMode shape_mode = jpeg_shape_mode;

    // Orthonormal DCT-II of 8 points, x 2^14: dct[k][n] = a(k) cos((2n + 1) k pi / 16), a(0) = sqrt(1/8), else 1/2.
    // Literal on purpose: computed with std::cos it could differ by a bit between C runtimes.
    constexpr int32_t dct_shift = 14;
    constexpr std::array<std::array<int32_t, 8>, 8> dct{ {
        { 5793,  5793,  5793,  5793,  5793,  5793,  5793,  5793 },
        { 8035,  6811,  4551,  1598, -1598, -4551, -6811, -8035 },
        { 7568,  3135, -3135, -7568, -7568, -3135,  3135,  7568 },
        { 6811, -1598, -8035, -4551,  4551,  8035,  1598, -6811 },
        { 5793, -5793, -5793,  5793,  5793, -5793, -5793,  5793 },
        { 4551, -8035,  1598,  6811, -6811, -1598,  8035, -4551 },
        { 3135, -7568,  7568, -3135, -3135,  7568, -7568,  3135 },
        { 1598, -4551,  6811, -8035,  8035, -6811,  4551, -1598 },
    } };

    // extra fraction bits of the samples through the transforms
    constexpr int32_t sample_shift = 3;

    // JPEG Annex K tables
    constexpr std::array<std::array<int32_t, 8>, 8> luma_table{ {
        { 16, 11, 10, 16, 24, 40, 51, 61 },
        { 12, 12, 14, 19, 26, 58, 60, 55 },
        { 14, 13, 16, 24, 40, 57, 69, 56 },
        { 14, 17, 22, 29, 51, 87, 80, 62 },
        { 18, 22, 37, 56, 68, 109, 103, 77 },
        { 24, 35, 55, 64, 81, 104, 113, 92 },
        { 49, 64, 78, 87, 103, 121, 120, 101 },
        { 72, 92, 95, 98, 112, 100, 103, 99 },
    } };
    constexpr std::array<std::array<int32_t, 8>, 8> chroma_table{ {
        { 17, 18, 24, 47, 99, 99, 99, 99 },
        { 18, 21, 26, 66, 99, 99, 99, 99 },
        { 24, 26, 56, 99, 99, 99, 99, 99 },
        { 47, 66, 99, 99, 99, 99, 99, 99 },
        { 99, 99, 99, 99, 99, 99, 99, 99 },
        { 99, 99, 99, 99, 99, 99, 99, 99 },
        { 99, 99, 99, 99, 99, 99, 99, 99 },
        { 99, 99, 99, 99, 99, 99, 99, 99 },
    } };

    uint32_t mix(uint32_t h)
    {
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return h;
    }

    // one axis of the 3D transform: 64 lines of 8 values, `stride` apart
    void transform_axis(std::array<int32_t, 512>& values, int32_t stride, bool inverse)
    {
        // the other two axes
        const int32_t a = stride == 1 ? 8 : 1;
        const int32_t b = stride == 64 ? 8 : 64;
        for (int32_t j = 0; j < 8; ++j)
            for (int32_t i = 0; i < 8; ++i)
            {
                const int32_t base = i * a + j * b;
                std::array<int64_t, 8> in{};
                for (int32_t n = 0; n < 8; ++n)
                    in[n] = values[base + n * stride];
                for (int32_t k = 0; k < 8; ++k)
                {
                    int64_t sum = 0;
                    for (int32_t n = 0; n < 8; ++n)
                        sum += (inverse ? int64_t(dct[n][k]) : int64_t(dct[k][n])) * in[n];
                    values[base + k * stride] = int32_t((sum + (int64_t(1) << (dct_shift - 1))) >> dct_shift);
                }
            }
    }

    int32_t divide_rounded(int32_t value, int32_t divisor)
    {
        return value >= 0 ? (value + divisor / 2) / divisor : -((-value + divisor / 2) / divisor);
    }

    int32_t clamp255(int32_t v) { return std::clamp(v, 0, 255); }

    // Unsharp mask of the colors in the blocks (first voxels `blocks`, all inside `box`): every voxel with a color
    // moves away from the mean of its 6 neighbours with a color by `percent` % of the difference. Read from a
    // copy: the result does not depend on the order.
    // Dithered holes (VoxelJpegSettings::holes): integer only, the same voxels on every machine. Returns the bounds
    // of the voxels emptied.
    GridBounds punch_holes(VoxelGrid& grid, glm::ivec3 center, int32_t radius, const VoxelJpegSettings& settings,
                           uint32_t seed, uint64_t& count)
    {
        GridBounds punched;
        const int64_t radius_squared = std::max<int64_t>(int64_t(radius) * radius, 1);
        const int64_t strength = int64_t(std::clamp(settings.holes, 0, 100)) * 65536 / 100;      // 0..65536
        const int32_t falloff = std::clamp(settings.holes_falloff, 1, 8);
        const glm::ivec3 lo = center - radius, hi = center + radius;
        std::vector<glm::ivec3> emptied;
        for (const Brick& brick : grid.get_bricks())
        {
            const glm::ivec3 brick_min = brick.coord * brick_size;
            const glm::ivec3 brick_max = brick_min + (brick_size - 1);
            if (brick.solid_count == 0 || brick_max.x < lo.x || brick_max.y < lo.y || brick_max.z < lo.z ||
                brick_min.x > hi.x || brick_min.y > hi.y || brick_min.z > hi.z)
                continue;
            for (uint32_t i = 0; i < brick_voxels; ++i)
            {
                if ((brick.voxels[i] >> 24) == 0)
                    continue;
                const glm::ivec3 v = brick_min + local_coord(i);
                const glm::ivec3 o = v - center;
                const int64_t d2 = int64_t(o.x) * o.x + int64_t(o.y) * o.y + int64_t(o.z) * o.z;
                if (d2 >= radius_squared)
                    continue;
                // (1 - (d / r)^2)^falloff in 16 bit fixed point
                const int64_t t = 65536 - d2 * 65536 / radius_squared;
                int64_t probability = strength;
                for (int32_t k = 0; k < falloff; ++k)
                    probability = (probability * t) >> 16;
                const uint32_t h = mix(uint32_t(v.x) * 0x8da6b343u ^ uint32_t(v.y) * 0xd8163841u ^
                                       uint32_t(v.z) * 0xcb1ab31fu ^ seed * 0x2c1b3c6du ^ 0x68e31da4u);
                if (int64_t(h & 0xffffu) < probability)
                    emptied.push_back(v);
            }
        }
        for (const glm::ivec3& v : emptied)
        {
            grid.set(v, Voxel{});
            punched = punched.is_empty() ? GridBounds{ v, v } : GridBounds{ glm::min(punched.min, v), glm::max(punched.max, v) };
        }
        count += emptied.size();
        return punched;
    }

    void sharpen_blocks(VoxelGrid& grid, const std::vector<glm::ivec3>& blocks, const GridBounds& box, int32_t percent)
    {
        const glm::ivec3 base = box.min - 1;
        const glm::ivec3 dims = box.size() + 2;
        const size_t count = size_t(dims.x) * dims.y * dims.z;
        auto index_of = [&] (glm::ivec3 v) { const glm::ivec3 d = v - base; return size_t(d.x) + size_t(dims.x) * (size_t(d.y) + size_t(dims.y) * size_t(d.z)); };

        std::vector<Voxel> copy(count);
        for (int32_t z = 0; z < dims.z; ++z)
            for (int32_t y = 0; y < dims.y; ++y)
                for (int32_t x = 0; x < dims.x; ++x)
                    copy[size_t(x) + size_t(dims.x) * (size_t(y) + size_t(dims.y) * size_t(z))] = grid.get(base + glm::ivec3(x, y, z));

        for (const glm::ivec3& block_min : blocks)
            for (uint32_t i = 0; i < 512; ++i)
            {
                const glm::ivec3 v = block_min + local_coord(i);
                const Voxel center = copy[index_of(v)];
                if (center.density == 0)
                    continue;
                int32_t n = 0, sum_r = 0, sum_g = 0, sum_b = 0;
                for (const glm::ivec3& o : { glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0),
                                             glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1) })
                {
                    const Voxel neighbour = copy[index_of(v + o)];
                    if (neighbour.density == 0)
                        continue;
                    ++n;
                    sum_r += neighbour.r;
                    sum_g += neighbour.g;
                    sum_b += neighbour.b;
                }
                if (n == 0)
                    continue;
                auto sharpened = [&] (int32_t c, int32_t sum) { return uint8_t(clamp255(c + (percent * (c * n - sum)) / (100 * n))); };
                const Voxel result{ sharpened(center.r, sum_r), sharpened(center.g, sum_g), sharpened(center.b, sum_b), center.density };
                if (result != center)
                    grid.set(v, result);
            }
    }
}

namespace voxel
{
    glm::ivec3 jpeg_grid_shift(uint32_t seed, int32_t generation)
    {
        const uint32_t h = mix(seed * 0x9e3779b9u + uint32_t(generation) * 0x85ebca6bu + 0x165667b1u);
        return { 1 + int32_t(h % 7u), 1 + int32_t((h >> 8) % 7u), 1 + int32_t((h >> 16) % 7u) };
    }

    std::array<int32_t, 512> quantization_table(int32_t quality, bool chroma)
    {
        quality = std::clamp(quality, 1, 100);
        const int32_t scale = quality < 50 ? 5000 / quality : 200 - 2 * quality;
        const auto& k = chroma ? chroma_table : luma_table;
        std::array<int32_t, 512> table{};
        for (int32_t z = 0; z < 8; ++z)
            for (int32_t y = 0; y < 8; ++y)
                for (int32_t x = 0; x < 8; ++x)
                {
                    // the 2D table on the three planes through the frequency
                    const int32_t base = (k[x][y] + k[y][z] + k[x][z]) / 3;
                    const int32_t step = std::clamp((base * scale + 50) / 100, 1, 255);
                    // a 3D orthonormal DCT gathers sqrt(8) times the energy of a 2D one in its coefficients
                    table[x + 8 * y + 64 * z] = std::max((step * 181 + 32) >> 6, 1);
                }
        return table;
    }

    void jpeg_block(std::array<int32_t, 512>& values, const std::array<int32_t, 512>& table)
    {
        for (int32_t& v : values)
            v = (v - 128) * (1 << sample_shift);
        transform_axis(values, 1, false);
        transform_axis(values, 8, false);
        transform_axis(values, 64, false);
        for (size_t i = 0; i < values.size(); ++i)
        {
            const int32_t step = table[i] << sample_shift;
            values[i] = divide_rounded(values[i], step) * step;
        }
        transform_axis(values, 64, true);
        transform_axis(values, 8, true);
        transform_axis(values, 1, true);
        for (int32_t& v : values)
            v = clamp255(((v + (1 << (sample_shift - 1))) >> sample_shift) + 128);
    }

    VoxelJpegStats recompress(VoxelGrid& grid, glm::ivec3 center, int32_t radius, const VoxelJpegSettings& settings,
        uint32_t seed)
    {
        const auto start = std::chrono::steady_clock::now();
        VoxelJpegStats stats;
        const std::array<int32_t, 512> luma = quantization_table(settings.quality, false);
        const std::array<int32_t, 512> chroma = quantization_table(settings.quality, true);
        std::array<int32_t, 512> density_table = quantization_table(settings.density_quality, false);
        // AC only: a coarse DC moves the level of the whole block (thin walls vanish or blocks fill up)
        for (size_t i = 1; i < density_table.size(); ++i)
            density_table[i] = std::max(int32_t((int64_t(density_table[i]) * std::max(settings.density_scale, 1) + 50) / 100), 1);
        const int64_t radius_squared = int64_t(radius) * radius;

        std::array<Voxel, 512> block{};
        std::vector<glm::ivec3> processed;      // first voxels of the blocks recompressed (the sharpen's)
        std::array<int32_t, 512> y{}, cb{}, cr{}, d{};

        // 2 x 2 x 2 cells: each value their mean
        auto merge_cells = [] (std::array<int32_t, 512>& values)
        {
            for (int32_t z = 0; z < 8; z += 2)
                for (int32_t yy = 0; yy < 8; yy += 2)
                    for (int32_t x = 0; x < 8; x += 2)
                    {
                        int32_t sum = 0;
                        for (int32_t i = 0; i < 8; ++i)
                            sum += values[local_index({ x + (i & 1), yy + ((i >> 1) & 1), z + (i >> 2) })];
                        const int32_t mean = (sum + 4) / 8;
                        for (int32_t i = 0; i < 8; ++i)
                            values[local_index({ x + (i & 1), yy + ((i >> 1) & 1), z + (i >> 2) })] = mean;
                    }
        };

        auto add_changed = [&] (const GridBounds& bounds)
        {
            if (bounds.is_empty())
                return;
            stats.changed = stats.changed.is_empty() ? bounds
                : GridBounds{ glm::min(stats.changed.min, bounds.min), glm::max(stats.changed.max, bounds.max) };
        };
        if (settings.holes > 0 && settings.holes_before)
            add_changed(punch_holes(grid, center, radius, settings, seed, stats.holes));

        for (int32_t generation = 0; generation < std::max(settings.generations, 1); ++generation)
        {
            const glm::ivec3 shift = jpeg_grid_shift(seed, generation);
            // blocks start at shift + 8 k
            auto floor8 = [] (int32_t v) { return v >= 0 ? v / 8 : -((-v + 7) / 8); };
            auto first_block = [&] (int32_t axis) { return floor8(center[axis] - radius - shift[axis]); };
            auto last_block = [&] (int32_t axis) { return floor8(center[axis] + radius - shift[axis]); };
            std::set<std::tuple<int32_t, int32_t, int32_t>> touched;

            for (int32_t bz = first_block(2); bz <= last_block(2); ++bz)
                for (int32_t by = first_block(1); by <= last_block(1); ++by)
                    for (int32_t bx = first_block(0); bx <= last_block(0); ++bx)
                    {
                        const glm::ivec3 block_min = shift + glm::ivec3(bx, by, bz) * 8;
                        // the block's center (block_min + 4) within the radius
                        const glm::ivec3 offset = (block_min + 4) - center;
                        if (int64_t(offset.x) * offset.x + int64_t(offset.y) * offset.y + int64_t(offset.z) * offset.z > radius_squared)
                            continue;

                        // ---- gather ----
                        uint32_t colored = 0;
                        uint32_t solid = 0;
                        for (uint32_t i = 0; i < 512; ++i)
                        {
                            block[i] = grid.get(block_min + local_coord(i));
                            colored += block[i].density > 0 ? 1 : 0;
                            solid += block[i].is_solid() ? 1 : 0;
                        }
                        if (colored == 0)
                            continue;
                        ++stats.blocks;
                        stats.solid_before += solid;
                        processed.push_back(block_min);

                        // ---- empty voxels: the colors of the surface pulled out into them (6-connected, breadth
                        // first in index order), as a JPEG pads its edge blocks by repeating the last pixels. The
                        // block then holds the surface's pattern along its normal, the codec rings in it like in 2D;
                        // a flat fill would put the block into its DC term: one flat color per block ----
                        std::array<int32_t, 512> r{}, g{}, b{};
                        std::array<uint8_t, 512> filled{};
                        std::array<uint16_t, 512> queue{};
                        uint32_t tail = 0;
                        for (uint32_t i = 0; i < 512; ++i)
                        {
                            d[i] = block[i].density;
                            if (block[i].density == 0)
                                continue;
                            r[i] = block[i].r;
                            g[i] = block[i].g;
                            b[i] = block[i].b;
                            if constexpr (shape_mode != JpegShapeMode::density)
                            {
                                // only emptiness is black: dark voxels lifted to the floor's luma
                                const int32_t luma = (19595 * r[i] + 38470 * g[i] + 7471 * b[i] + 32768) >> 16;
                                if (luma < settings.key_floor)
                                {
                                    const int32_t lift = settings.key_floor - luma;
                                    r[i] = clamp255(r[i] + lift);
                                    g[i] = clamp255(g[i] + lift);
                                    b[i] = clamp255(b[i] + lift);
                                }
                            }
                            filled[i] = 1;
                            queue[tail++] = uint16_t(i);
                        }
                        // black modes: the empty voxels stay black (r, g, b = 0)
                        if constexpr (shape_mode != JpegShapeMode::density)
                            tail = 0;
                        for (uint32_t head = 0; head < tail; ++head)
                        {
                            const uint32_t i = queue[head];
                            const glm::ivec3 p = local_coord(i);
                            for (const glm::ivec3& n : { glm::ivec3(1, 0, 0), glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0),
                                                         glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1), glm::ivec3(0, 0, -1) })
                            {
                                const glm::ivec3 q = p + n;
                                if (q.x < 0 || q.y < 0 || q.z < 0 || q.x > 7 || q.y > 7 || q.z > 7)
                                    continue;
                                const uint32_t j = local_index(q);
                                if (filled[j])
                                    continue;
                                filled[j] = 1;
                                r[j] = r[i];
                                g[j] = g[i];
                                b[j] = b[i];
                                queue[tail++] = uint16_t(j);
                            }
                        }

                        if (shape_mode == JpegShapeMode::density && settings.density_noise > 0)
                            for (uint32_t i = 0; i < 512; ++i)
                            {
                                if (d[i] == 0)
                                    continue;
                                const glm::ivec3 v = block_min + local_coord(i);
                                const uint32_t h = mix(uint32_t(v.x) * 0x8da6b343u ^ uint32_t(v.y) * 0xd8163841u ^
                                                       uint32_t(v.z) * 0xcb1ab31fu ^ seed * 0x9e3779b9u ^ uint32_t(generation));
                                const int32_t span = 2 * settings.density_noise + 1;
                                d[i] = clamp255(d[i] + int32_t(h % uint32_t(span)) - settings.density_noise);
                            }
                        if (settings.downscale >= 2)
                        {
                            merge_cells(r);
                            merge_cells(g);
                            merge_cells(b);
                            merge_cells(d);
                        }
                        // ---- RGB -> YCbCr (JFIF, libjpeg's fixed point) ----
                        for (uint32_t i = 0; i < 512; ++i)
                        {
                            y[i] = (19595 * r[i] + 38470 * g[i] + 7471 * b[i] + 32768) >> 16;
                            cb[i] = (-11059 * r[i] - 21709 * g[i] + 32768 * b[i] + (128 << 16) + 32767) >> 16;
                            cr[i] = (32768 * r[i] - 27439 * g[i] - 5329 * b[i] + (128 << 16) + 32767) >> 16;
                        }
                        if (settings.chroma == JpegChroma::half)
                        {
                            merge_cells(cb);
                            merge_cells(cr);
                        }

                        // ---- the codec ----
                        jpeg_block(y, luma);
                        jpeg_block(cb, chroma);
                        jpeg_block(cr, chroma);
                        if constexpr (shape_mode == JpegShapeMode::density)
                            jpeg_block(d, density_table);

                        // ---- back ----
                        uint32_t solid_after = 0;
                        for (uint32_t i = 0; i < 512; ++i)
                        {
                            const int32_t cbv = cb[i] - 128, crv = cr[i] - 128;
                            uint8_t density = 0;
                            if constexpr (shape_mode == JpegShapeMode::density)
                                density = uint8_t(d[i] < settings.erase_below ? 0 : d[i]);
                            else
                            {
                                // the decoded luma keys the shape
                                const bool was_there = block[i].density > 0;
                                const bool kept = was_there && y[i] >= settings.key_keep;
                                const bool appears = shape_mode == JpegShapeMode::black_key && !was_there && y[i] >= settings.key_appear;
                                density = kept ? block[i].density : appears ? uint8_t(255) : uint8_t(0);
                            }
                            Voxel voxel{
                                uint8_t(clamp255(y[i] + ((91881 * crv + 32768) >> 16))),
                                uint8_t(clamp255(y[i] + ((-22554 * cbv - 46802 * crv + 32768) >> 16))),
                                uint8_t(clamp255(y[i] + ((116130 * cbv + 32768) >> 16))),
                                density,
                            };
                            if (voxel.density == 0)
                                voxel = Voxel{};
                            solid_after += voxel.is_solid() ? 1 : 0;
                            if (voxel == block[i])
                                continue;
                            grid.set(block_min + local_coord(i), voxel);
                        }
                        stats.solid_after += solid_after;

                        for (int32_t i = 0; i < 8; ++i)
                        {
                            const glm::ivec3 corner = block_min + glm::ivec3(i & 1, (i >> 1) & 1, i >> 2) * 7;
                            const glm::ivec3 brick = brick_of(corner);
                            touched.insert({ brick.x, brick.y, brick.z });
                        }
                        const GridBounds extent{ block_min, block_min + 7 };
                        if (stats.changed.is_empty())
                            stats.changed = extent;
                        else
                        {
                            stats.changed.min = glm::min(stats.changed.min, extent.min);
                            stats.changed.max = glm::max(stats.changed.max, extent.max);
                        }
                    }

            for (const auto& [x, yy, z] : touched)
                if (const std::optional<uint32_t> index = grid.find_brick({ x, yy, z }))
                {
                    Brick& brick = grid.get_brick(*index);
                    brick.generation = uint8_t(std::min<int32_t>(brick.generation + 1, 255));
                    brick.flags |= brick_flags::dirty;
                }
        }

        if (settings.holes > 0 && !settings.holes_before)
            add_changed(punch_holes(grid, center, radius, settings, seed, stats.holes));

        if (settings.sharpen > 0 && !processed.empty())
            sharpen_blocks(grid, processed, stats.changed, settings.sharpen);

        grid.remove_empty_bricks();
        stats.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        return stats;
    }
}
