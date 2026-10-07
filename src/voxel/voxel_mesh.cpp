module voxel;

import std.compat;
import glm;

import :grid;
import :mesh;

namespace
{
    using namespace voxel;

    // A merged rectangle of faces: dense coordinates of its first voxel, size in voxels along u and v
    struct Quad
    {
        int32_t axis = 0;
        int32_t sign = 1;
        glm::ivec3 first{ 0 };
        int32_t width = 0;
        int32_t height = 0;
        uint32_t x = 0;         // patch in the atlas (texels, padding included)
        uint32_t y = 0;
    };

    uint32_t next_pow2(uint32_t v)
    {
        uint32_t p = 1;
        while (p < v)
            p <<= 1;
        return p;
    }

    // Shelf packing of the patches into `width`, returns the height used (0: a patch is wider than width)
    uint32_t pack(std::vector<Quad>& quads, const std::vector<uint32_t>& order, uint32_t width, uint32_t texels,
                  uint32_t padding)
    {
        uint32_t x = 0, y = 0, shelf = 0;
        for (const uint32_t i : order)
        {
            Quad& quad = quads[i];
            const uint32_t w = uint32_t(quad.width) * texels + 2 * padding;
            const uint32_t h = uint32_t(quad.height) * texels + 2 * padding;
            if (w > width)
                return 0;
            if (x + w > width)
            {
                y += shelf;
                x = 0;
                shelf = 0;
            }
            quad.x = x;
            quad.y = y;
            x += w;
            shelf = std::max(shelf, h);
        }
        return y + shelf;
    }
}

namespace voxel
{
    std::optional<SurfaceMesh> build_surface_mesh(const VoxelGrid& grid, const SurfaceMeshSettings& settings,
        std::string* error)
    {
        const GridBounds bricks = grid.get_brick_bounds();
        if (bricks.is_empty())
            return SurfaceMesh{};
        return build_surface_mesh(grid, GridBounds{ bricks.min * brick_size, bricks.max * brick_size + (brick_size - 1) },
            settings, error);
    }

    std::optional<SurfaceMesh> build_surface_mesh(const VoxelGrid& grid, const GridBounds& voxels,
        const SurfaceMeshSettings& settings, std::string* error)
    {
        SurfaceMesh mesh;
        if (voxels.is_empty())
            return mesh;

        // ---- dense copy of the range with one voxel of neighbours around it ----
        const glm::ivec3 base = voxels.min - 1;                     // grid coordinate of dense (0, 0, 0)
        const glm::ivec3 dims = voxels.size() + 2;
        const size_t count = size_t(dims.x) * dims.y * dims.z;
        std::vector<uint32_t> dense(count, 0);                      // packed voxels, solid ones only
        auto index_of = [&] (glm::ivec3 d) { return size_t(d.x) + size_t(dims.x) * (size_t(d.y) + size_t(dims.y) * size_t(d.z)); };

        const glm::ivec3 last = base + dims - 1;
        for (const Brick& brick : grid.get_bricks())
        {
            const glm::ivec3 brick_min = brick.coord * brick_size;
            const glm::ivec3 brick_max = brick_min + (brick_size - 1);
            if (brick.solid_count == 0 || brick_max.x < base.x || brick_max.y < base.y || brick_max.z < base.z ||
                brick_min.x > last.x || brick_min.y > last.y || brick_min.z > last.z)
                continue;
            for (uint32_t i = 0; i < brick_voxels; ++i)
            {
                const uint32_t bits = brick.voxels[i];
                if (!is_solid_bits(bits))
                    continue;
                const glm::ivec3 d = brick_min + local_coord(i) - base;
                if (d.x < 0 || d.y < 0 || d.z < 0 || d.x >= dims.x || d.y >= dims.y || d.z >= dims.z)
                    continue;
                dense[index_of(d)] = bits | 0xff000000u;            // non zero: solid
            }
        }

        // ---- greedy rectangles per direction and slice ----
        std::vector<Quad> quads;
        std::vector<uint8_t> mask;
        for (int32_t axis = 0; axis < 3; ++axis)
        {
            const int32_t u = (axis + 1) % 3, v = (axis + 2) % 3;
            const int32_t size_u = dims[u] - 2, size_v = dims[v] - 2;
            mask.assign(size_t(size_u) * size_v, 0);
            for (const int32_t sign : { 1, -1 })
            {
                for (int32_t slice = 1; slice < dims[axis] - 1; ++slice)
                {
                    bool any = false;
                    for (int32_t j = 0; j < size_v; ++j)
                        for (int32_t i = 0; i < size_u; ++i)
                        {
                            glm::ivec3 d(0);
                            d[axis] = slice;
                            d[u] = i + 1;
                            d[v] = j + 1;
                            glm::ivec3 n = d;
                            n[axis] += sign;
                            const bool visible = dense[index_of(d)] != 0 && dense[index_of(n)] == 0;
                            mask[size_t(j) * size_u + i] = visible ? 1 : 0;
                            any |= visible;
                        }
                    if (!any)
                        continue;

                    for (int32_t j = 0; j < size_v; ++j)
                        for (int32_t i = 0; i < size_u;)
                        {
                            if (!mask[size_t(j) * size_u + i])
                            {
                                ++i;
                                continue;
                            }
                            int32_t width = 1;
                            while (i + width < size_u && mask[size_t(j) * size_u + i + width])
                                ++width;
                            int32_t height = 1;
                            for (; j + height < size_v; ++height)
                            {
                                bool row = true;
                                for (int32_t k = 0; k < width && row; ++k)
                                    row = mask[size_t(j + height) * size_u + i + k] != 0;
                                if (!row)
                                    break;
                            }
                            for (int32_t h = 0; h < height; ++h)
                                for (int32_t k = 0; k < width; ++k)
                                    mask[size_t(j + h) * size_u + i + k] = 0;

                            Quad quad{ .axis = axis, .sign = sign, .width = width, .height = height };
                            quad.first[axis] = slice;
                            quad.first[u] = i + 1;
                            quad.first[v] = j + 1;
                            quads.push_back(quad);
                            i += width;
                        }
                }
            }
        }
        if (quads.empty())
            return mesh;

        // ---- atlas layout ----
        std::vector<uint32_t> order(quads.size());
        std::iota(order.begin(), order.end(), 0u);
        std::ranges::stable_sort(order, [&] (uint32_t a, uint32_t b)
        {
            if (quads[a].height != quads[b].height)
                return quads[a].height > quads[b].height;
            return quads[a].width > quads[b].width;
        });

        const uint32_t padding = settings.padding;
        uint32_t texels = std::max(settings.texels_per_voxel, 1u);
        uint32_t width = 0, height = 0;
        const bool fixed = settings.atlas_width > 0 && settings.atlas_height > 0;
        for (;;)
        {
            if (fixed)
            {
                const uint32_t used = pack(quads, order, settings.atlas_width, texels, padding);
                if (used != 0 && used <= settings.atlas_height)
                {
                    width = settings.atlas_width;
                    height = settings.atlas_height;
                    break;
                }
                if (texels == 1)
                {
                    if (error)
                        *error = std::format("{} quads do not fit the atlas of {} x {} texels", quads.size(),
                                             settings.atlas_width, settings.atlas_height);
                    return std::nullopt;
                }
                texels /= 2;
                continue;
            }
            uint64_t area = 0;
            for (const Quad& quad : quads)
                area += uint64_t(uint32_t(quad.width) * texels + 2 * padding) * (uint32_t(quad.height) * texels + 2 * padding);
            width = next_pow2(uint32_t(std::ceil(std::sqrt(double(area)))));
            bool fits = false;
            for (; width <= settings.max_atlas_size; width *= 2)
            {
                const uint32_t used = pack(quads, order, width, texels, padding);
                if (used != 0 && used <= width)
                {
                    height = next_pow2(used);
                    fits = true;
                    break;
                }
            }
            if (fits)
                break;
            if (texels == 1)
            {
                if (error)
                    *error = std::format("{} quads do not fit an atlas of {}^2 texels", quads.size(), settings.max_atlas_size);
                return std::nullopt;
            }
            texels /= 2;
        }

        mesh.atlas_width = width;
        mesh.atlas_height = height;
        mesh.texels_per_voxel = texels;
        mesh.atlas.assign(size_t(width) * height, 0xff000000u);
        mesh.quads = uint32_t(quads.size());
        mesh.positions.reserve(quads.size() * 4);
        mesh.normals.reserve(quads.size() * 4);
        mesh.tangents.reserve(quads.size() * 4);
        mesh.uvs.reserve(quads.size() * 4);
        mesh.indices.reserve(quads.size() * 6);

        const float voxel_size = grid.get_voxel_size();
        const glm::vec3 origin = grid.get_origin();
        for (const Quad& quad : quads)
        {
            const int32_t u = (quad.axis + 1) % 3, v = (quad.axis + 2) % 3;

            // the patch: every texel the color of its voxel, the padding the color of the nearest edge texel
            const int32_t patch_w = quad.width * int32_t(texels), patch_h = quad.height * int32_t(texels);
            for (int32_t ty = 0; ty < patch_h + 2 * int32_t(padding); ++ty)
                for (int32_t tx = 0; tx < patch_w + 2 * int32_t(padding); ++tx)
                {
                    const int32_t lx = std::clamp(tx - int32_t(padding), 0, patch_w - 1);
                    const int32_t ly = std::clamp(ty - int32_t(padding), 0, patch_h - 1);
                    glm::ivec3 d = quad.first;
                    d[u] += lx / int32_t(texels);
                    d[v] += ly / int32_t(texels);
                    mesh.atlas[size_t(quad.y + ty) * width + quad.x + tx] = dense[index_of(d)];
                }

            // corners (0, 0), (w, 0), (w, h), (0, h) in voxels along u, v
            const uint32_t first_vertex = uint32_t(mesh.positions.size());
            glm::vec3 normal(0.0f), tangent(0.0f);
            normal[quad.axis] = float(quad.sign);
            tangent[u] = 1.0f;
            const std::array<glm::ivec2, 4> corners{ glm::ivec2(0, 0), glm::ivec2(quad.width, 0),
                                                     glm::ivec2(quad.width, quad.height), glm::ivec2(0, quad.height) };
            for (const glm::ivec2& corner : corners)
            {
                glm::ivec3 p = quad.first + base;                       // grid voxel coordinate
                p[quad.axis] += quad.sign > 0 ? 1 : 0;                  // the face's plane
                p[u] += corner.x;
                p[v] += corner.y;
                mesh.positions.push_back(origin + glm::vec3(float(p.x), float(p.y), float(p.z)) * voxel_size);
                mesh.normals.push_back(normal);
                mesh.tangents.push_back(glm::vec4(tangent, 1.0f));
                mesh.uvs.push_back(glm::vec2(
                    float(quad.x + padding + uint32_t(corner.x) * texels) / float(width),
                    float(quad.y + padding + uint32_t(corner.y) * texels) / float(height)));
            }
            // u x v = axis: (0, 1, 2) is counter-clockwise seen from +axis
            const std::array<uint32_t, 6> front{ 0, 1, 2, 0, 2, 3 };
            const std::array<uint32_t, 6> back{ 0, 2, 1, 0, 3, 2 };
            for (const uint32_t corner : quad.sign > 0 ? front : back)
                mesh.indices.push_back(first_vertex + corner);
        }
        return mesh;
    }
}
