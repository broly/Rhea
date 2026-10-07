// Tests of the voxel module (standalone: depends only on std and glm). Build and run target rhea_voxel_tests.

import std;
import glm;
import voxel;

using namespace voxel;

static int failures = 0;
#define EXPECT(...) do { if (!(__VA_ARGS__)) { std::println("FAIL {}:{}: {}", __LINE__, __func__, #__VA_ARGS__); ++failures; } } while (0)

static Voxel solid(std::uint8_t r = 200, std::uint8_t g = 100, std::uint8_t b = 50) { return { r, g, b, 255 }; }

// a solid box of voxels [min, max]
static void fill(VoxelGrid& grid, glm::ivec3 min, glm::ivec3 max, Voxel value)
{
    for (int z = min.z; z <= max.z; ++z)
        for (int y = min.y; y <= max.y; ++y)
            for (int x = min.x; x <= max.x; ++x)
                grid.set({ x, y, z }, value);
}

void coordinates()
{
    const Voxel v{ 1, 2, 3, 200 };
    EXPECT(v.pack() == 0xc8030201u);
    EXPECT(Voxel::unpack(v.pack()) == v);
    EXPECT(v.is_solid() && !Voxel{ 9, 9, 9, 127 }.is_solid() && Voxel{}.is_empty());

    for (std::uint32_t i = 0; i < brick_voxels; ++i)
        EXPECT(local_index(local_coord(i)) == i);
    EXPECT(local_index({ 1, 2, 3 }) == 1 + 2 * 8 + 3 * 64);

    // floor division for negative voxels: -1 is in brick -1, voxel 7
    EXPECT(brick_of({ -1, -8, -9 }) == glm::ivec3(-1, -1, -2));
    EXPECT(local_of({ -1, -8, -9 }) == glm::ivec3(7, 0, 7));
    EXPECT(brick_of({ 7, 8, 15 }) == glm::ivec3(0, 1, 1));

    VoxelGrid grid(0.1f, glm::vec3(-1.0f, 0.0f, 0.0f));
    EXPECT(grid.voxel_at({ -1.0f, 0.0f, 0.0f }) == glm::ivec3(0));
    EXPECT(grid.voxel_at({ -1.05f, 0.25f, 0.0f }) == glm::ivec3(-1, 2, 0));
    const glm::vec3 c = grid.voxel_center({ 2, 0, 0 });
    EXPECT(std::abs(c.x - -0.75f) < 1e-5f && std::abs(c.y - 0.05f) < 1e-5f);
}

void voxels_and_bricks()
{
    VoxelGrid grid;
    EXPECT(grid.get_brick_count() == 0 && grid.get_brick_bounds().is_empty());

    grid.set({ 0, 0, 0 }, Voxel{});                       // empty: no brick
    EXPECT(grid.get_brick_count() == 0);

    grid.set({ 3, 4, 5 }, solid());
    grid.set({ -1, 0, 0 }, solid(1, 2, 3));
    grid.set({ 8, 0, 0 }, Voxel{ 5, 5, 5, 10 });          // not solid, still stored
    EXPECT(grid.get_brick_count() == 3);
    EXPECT(grid.get({ 3, 4, 5 }) == solid());
    EXPECT(grid.get({ -1, 0, 0 }) == solid(1, 2, 3));
    EXPECT(grid.get({ 4, 4, 5 }).is_empty());
    EXPECT(grid.get({ 100, 0, 0 }).is_empty());
    EXPECT(grid.get_solid_count() == 2);

    // solid count follows overwrites
    grid.set({ 3, 4, 5 }, solid(9, 9, 9));
    EXPECT(grid.get_solid_count() == 2);
    grid.set({ 3, 4, 5 }, Voxel{});
    EXPECT(grid.get_solid_count() == 1);
    EXPECT(grid.get_brick_count() == 3);                  // emptied, kept until asked
    EXPECT(grid.remove_empty_bricks() == 1);
    EXPECT(grid.get_brick_count() == 2);
    EXPECT(!grid.find_brick({ 0, 0, 0 }));
    // the lookup still points at the moved brick
    EXPECT(grid.get({ -1, 0, 0 }) == solid(1, 2, 3));
    EXPECT(grid.get({ 8, 0, 0 }).density == 10);

    const GridBounds bounds = grid.get_brick_bounds();
    EXPECT(bounds.min == glm::ivec3(-1, 0, 0) && bounds.max == glm::ivec3(1, 0, 0));
    EXPECT(bounds.size() == glm::ivec3(3, 1, 1));
    const auto [lo, hi] = grid.get_local_bounds();
    EXPECT(std::abs(lo.x - -0.8f) < 1e-5f && std::abs(hi.x - 1.6f) < 1e-5f && std::abs(hi.y - 0.8f) < 1e-5f);

    grid.remove_brick({ -1, 0, 0 });
    EXPECT(grid.get_brick_count() == 1 && grid.get({ -1, 0, 0 }).is_empty() && grid.get({ 8, 0, 0 }).density == 10);
    grid.clear();
    EXPECT(grid.get_brick_count() == 0 && grid.get({ 8, 0, 0 }).is_empty());
}

void surface()
{
    // a 10^3 cube across 8 bricks: its 8^3 inside is hidden
    VoxelGrid grid;
    fill(grid, glm::ivec3(-3), glm::ivec3(6), solid());
    EXPECT(grid.get_brick_count() == 8);
    EXPECT(grid.get_solid_count() == 1000);
    EXPECT(grid.count_surface_voxels() == 1000 - 512);

    // a hole opens a cavity: its 6 neighbours become surface too
    grid.set({ 1, 1, 1 }, Voxel{ 0, 0, 0, 20 });
    EXPECT(grid.get_solid_count() == 999);
    EXPECT(grid.count_surface_voxels() == 1000 - 512 + 6);
}

void hashing()
{
    VoxelGrid a;
    VoxelGrid b;
    fill(a, { 0, 0, 0 }, { 9, 1, 1 }, solid());
    fill(a, { -20, 0, 0 }, { -18, 0, 0 }, solid(1, 1, 1));
    // the same content, bricks added in another order
    fill(b, { -20, 0, 0 }, { -18, 0, 0 }, solid(1, 1, 1));
    fill(b, { 0, 0, 0 }, { 9, 1, 1 }, solid());
    EXPECT(a.compute_hash() == b.compute_hash());

    b.set({ 0, 0, 0 }, solid(0, 0, 0));
    EXPECT(a.compute_hash() != b.compute_hash());
    EXPECT(VoxelGrid(0.1f).compute_hash() != VoxelGrid(0.25f).compute_hash());

    EXPECT(hash_value(hash_seed, 1.0f) != hash_value(hash_seed, 2.0f));
    EXPECT(hash_string(hash_seed, "abc") == hash_bytes(hash_seed, "abc", 3));
}

void gpu_layout()
{
    VoxelGrid grid(0.25f, glm::vec3(1.0f, 2.0f, 3.0f));
    fill(grid, { 0, 0, 0 }, { 1, 1, 1 }, solid());
    grid.set({ 17, -1, 8 }, solid(7, 8, 9));
    grid.get_brick(*grid.find_brick({ 2, -1, 1 })).generation = 3;

    const GpuGridData data = pack_for_gpu(grid);
    EXPECT(data.header.origin_voxel_size == glm::vec4(1.0f, 2.0f, 3.0f, 0.25f));
    EXPECT(data.header.map_min == glm::ivec4(0, -1, 0, 2));
    EXPECT(glm::ivec3(data.header.map_size) == glm::ivec3(3, 2, 2));
    EXPECT(data.bricks.size() == 2 && data.voxels.size() == 2 * brick_voxels && data.brick_map.size() == 12);
    EXPECT(data.voxels_bytes() == 2 * 2048);

    std::uint32_t mapped = 0;
    for (std::uint32_t index : data.brick_map)
        mapped += index != empty_brick ? 1 : 0;
    EXPECT(mapped == 2);

    for (std::uint32_t i = 0; i < data.bricks.size(); ++i)
    {
        const GpuBrick& gpu = data.bricks[i];
        EXPECT(data.brick_map[brick_map_index(data.header, gpu.coord)] == i);
        const Brick& brick = grid.get_brick(i);
        EXPECT(gpu.coord == brick.coord);
        EXPECT((gpu.meta & 0x3ffu) == brick.solid_count);
        EXPECT(((gpu.meta >> 10) & 0xffu) == brick.generation);
        EXPECT(std::equal(brick.voxels.begin(), brick.voxels.end(), data.voxels.begin() + i * brick_voxels));
    }

    // voxel (17, -1, 8): brick (2, -1, 1), local (1, 7, 0)
    const std::uint32_t brick = data.brick_map[brick_map_index(data.header, { 2, -1, 1 })];
    EXPECT(Voxel::unpack(data.voxels[brick * brick_voxels + local_index({ 1, 7, 0 })]) == solid(7, 8, 9));
    EXPECT(brick_map_index(data.header, { 3, 0, 0 }) == empty_brick);
    EXPECT(brick_map_index(data.header, { 0, -2, 0 }) == empty_brick);

    EXPECT(pack_for_gpu(VoxelGrid()).brick_map.empty());
}

void serialization()
{
    VoxelGrid grid(0.1f, glm::vec3(0.5f, -2.0f, 4.0f));
    fill(grid, { -5, 0, 0 }, { 12, 3, 2 }, solid());
    grid.set({ 40, 40, 40 }, Voxel{ 1, 2, 3, 60 });
    Brick& far = grid.get_brick(*grid.find_brick({ 5, 5, 5 }));
    far.generation = 7;
    far.flags = brick_flags::detached | brick_flags::dirty;

    std::stringstream stream;
    EXPECT(write_grid(stream, grid, 1234));
    const std::string bytes = stream.str();
    EXPECT(bytes.size() == 4 + 4 + 8 + 4 + 12 + 4 + grid.get_brick_count() * (16 + 2048));

    std::uint64_t hash = 0;
    std::string error;
    std::optional<VoxelGrid> loaded = read_grid(stream, &hash, &error);
    EXPECT(loaded.has_value());
    if (!loaded)
    {
        std::println("  {}", error);
        return;
    }
    EXPECT(hash == 1234);
    EXPECT(loaded->compute_hash() == grid.compute_hash());
    EXPECT(loaded->get_solid_count() == grid.get_solid_count());
    EXPECT(loaded->get_voxel_size() == 0.1f && loaded->get_origin() == glm::vec3(0.5f, -2.0f, 4.0f));
    const Brick& loaded_far = loaded->get_brick(*loaded->find_brick({ 5, 5, 5 }));
    EXPECT(loaded_far.generation == 7 && loaded_far.flags == brick_flags::detached);   // dirty is not stored

    // damaged files
    std::stringstream truncated(bytes.substr(0, bytes.size() - 100));
    EXPECT(!read_grid(truncated, nullptr, &error) && error.starts_with("truncated"));
    std::string wrong = bytes;
    wrong[0] = 'X';
    std::stringstream wrong_magic(wrong);
    EXPECT(!read_grid(wrong_magic, nullptr, &error) && error == "not a voxel grid file");
    std::string newer = bytes;
    newer[4] = 99;
    std::stringstream wrong_version(newer);
    EXPECT(!read_grid(wrong_version, nullptr, &error) && error.starts_with("version 99"));
}

void cache()
{
    const std::filesystem::path dir = std::filesystem::temp_directory_path()
        / std::format("rhea_voxel_tests_{}", std::chrono::steady_clock::now().time_since_epoch().count());
    const VoxelCache voxel_cache(dir);

    VoxelGrid grid;
    fill(grid, { 0, 0, 0 }, { 3, 3, 3 }, solid());

    EXPECT(!voxel_cache.load("house/a", 42));
    std::string error;
    EXPECT(voxel_cache.store("house/a", 42, grid, &error));
    EXPECT(voxel_cache.file_of("house/a").filename() == "house_a.rvox");
    EXPECT(std::filesystem::exists(voxel_cache.file_of("house/a")));

    const std::optional<VoxelGrid> hit = voxel_cache.load("house/a", 42);
    EXPECT(hit && hit->compute_hash() == grid.compute_hash());
    EXPECT(!voxel_cache.load("house/a", 43));             // the source changed: rebuild
    EXPECT(!voxel_cache.load("house/b", 42));
    EXPECT(!VoxelCache({}).store("x", 1, grid));

    // no temporary files left behind
    std::uint32_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
        files += entry.is_regular_file() ? 1 : 0;
    EXPECT(files == 1);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

// ---- voxelizer ----

struct TestMesh
{
    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;
    std::vector<std::uint32_t> indices;
};

// axis aligned box, `cells` quads per face side, triangles counter-clockwise seen from outside
static void add_box(TestMesh& mesh, glm::vec3 lo, glm::vec3 hi, int cells = 1)
{
    for (int axis = 0; axis < 3; ++axis)
        for (int sign : { -1, 1 })
        {
            const int u = (axis + 1) % 3, v = (axis + 2) % 3;
            for (int j = 0; j < cells; ++j)
                for (int i = 0; i < cells; ++i)
                {
                    const std::uint32_t first = std::uint32_t(mesh.positions.size());
                    for (const glm::ivec2 c : { glm::ivec2(0, 0), glm::ivec2(1, 0), glm::ivec2(1, 1), glm::ivec2(0, 1) })
                    {
                        glm::vec3 p(0.0f);
                        p[axis] = sign > 0 ? hi[axis] : lo[axis];
                        p[u] = lo[u] + (hi[u] - lo[u]) * float(i + c.x) / float(cells);
                        p[v] = lo[v] + (hi[v] - lo[v]) * float(j + c.y) / float(cells);
                        mesh.positions.push_back(p);
                        mesh.uvs.push_back(glm::vec2(float(i + c.x) / float(cells), float(j + c.y) / float(cells)));
                    }
                    const std::array<std::uint32_t, 6> order = sign > 0 ? std::array<std::uint32_t, 6>{ 0, 1, 2, 0, 2, 3 }
                                                                        : std::array<std::uint32_t, 6>{ 0, 2, 1, 0, 3, 2 };
                    for (std::uint32_t k : order)
                        mesh.indices.push_back(first + k);
                }
        }
}

static VoxelizePrimitive primitive_of(const TestMesh& mesh, std::uint32_t material = 0)
{
    return { mesh.positions, mesh.uvs, mesh.indices, material };
}

void triangle_box()
{
    const glm::vec3 a(0, 0, 0), b(1, 0, 0), c(0, 1, 0);
    EXPECT(triangle_overlaps_box(a, b, c, { 0.2f, 0.2f, 0.0f }, glm::vec3(0.05f)));
    EXPECT(triangle_overlaps_box(a, b, c, { 0.2f, 0.2f, 0.04f }, glm::vec3(0.05f)));     // plane inside the box
    EXPECT(!triangle_overlaps_box(a, b, c, { 0.2f, 0.2f, 0.2f }, glm::vec3(0.05f)));
    EXPECT(!triangle_overlaps_box(a, b, c, { 0.8f, 0.8f, 0.0f }, glm::vec3(0.05f)));     // beyond the hypotenuse
    EXPECT(triangle_overlaps_box(a, b, c, { 0.52f, 0.52f, 0.0f }, glm::vec3(0.05f)));    // the hypotenuse crosses it
    // a big triangle through a small box: no vertex inside
    EXPECT(triangle_overlaps_box({ -10, -10, 0 }, { 10, -10, 0 }, { 0, 10, 0 }, { 0, 0, 0 }, glm::vec3(0.01f)));

    const glm::vec3 w = closest_point_barycentric({ 0.25f, 0.25f, 1.0f }, a, b, c);
    EXPECT(std::abs(w.y - 0.25f) < 1e-5f && std::abs(w.z - 0.25f) < 1e-5f);
    EXPECT(closest_point_barycentric({ 2.0f, -1.0f, 0.0f }, a, b, c) == glm::vec3(0, 1, 0));
}

void voxelizer()
{
    // a closed box: a shell one or two voxels thick, the inside (a room) stays empty
    TestMesh room;
    add_box(room, { 0.0f, 0.0f, 0.0f }, { 2.0f, 1.5f, 1.0f });
    const VoxelizeMaterial red{ .color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) };
    VoxelizeStats stats;
    std::string error;
    const std::array primitives{ primitive_of(room) };
    std::optional<VoxelGrid> grid = voxelize(primitives, std::span(&red, 1), { .voxel_size = 0.1f }, &stats, &error);
    EXPECT(grid.has_value());
    if (!grid)
        return;
    EXPECT(stats.triangles == 12 && stats.filled_voxels == 0 && stats.surface_voxels > 0);
    EXPECT(grid->get(grid->voxel_at({ 1.0f, 0.75f, 0.5f })).is_empty());                // the room
    EXPECT(grid->is_solid(grid->voxel_at({ 1.0f, 0.0f, 0.5f })));                        // the floor
    EXPECT(grid->is_solid(grid->voxel_at({ 2.0f, 0.75f, 0.5f })));                       // a wall
    EXPECT(grid->get(grid->voxel_at({ 1.0f, 0.0f, 0.5f })) == Voxel(255, 0, 0, 255));
    EXPECT(grid->get(glm::ivec3(0)).is_empty());                                          // the margin

    // a wall of two layers 30 cm apart: the gap between them is filled, the room inside stays empty
    TestMesh walls;
    add_box(walls, { 0.0f, 0.0f, 0.0f }, { 3.0f, 2.5f, 3.0f }, 4);
    add_box(walls, { 0.3f, 0.3f, 0.3f }, { 2.7f, 2.2f, 2.7f }, 4);
    const std::array wall_primitives{ primitive_of(walls) };
    grid = voxelize(wall_primitives, std::span(&red, 1), { .voxel_size = 0.1f, .fill_thickness = 3 }, &stats, &error);
    EXPECT(grid.has_value());
    if (!grid)
        return;
    EXPECT(stats.filled_voxels > 0);
    EXPECT(grid->is_solid(grid->voxel_at({ 1.5f, 1.25f, 0.15f })));                      // inside the wall
    EXPECT(grid->get(grid->voxel_at({ 1.5f, 1.25f, 1.5f })).is_empty());                  // the room
    std::println("voxelizer: two layer walls, {} triangles, {} x {} x {}: {} surface + {} filled voxels in {:.1f} ms",
        stats.triangles, stats.size.x, stats.size.y, stats.size.z, stats.surface_voxels, stats.filled_voxels, stats.milliseconds);

    // texture colors: a 2 x 1 image, left texel red, right texel blue, on a slab 2 m long in z (the top face:
    // axis y, u along z), 1 texel = 1 m: level 0, bilinear (read at the texel centers)
    Image image{ 2, 1, { 0xff0000ffu, 0xffff0000u } };
    const VoxelizeMaterial textured{ .base_color = &image };
    TestMesh slab;
    add_box(slab, { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.1f, 2.0f });
    const std::array slab_primitives{ primitive_of(slab) };
    grid = voxelize(slab_primitives, std::span(&textured, 1), { .voxel_size = 0.1f }, &stats, &error);
    EXPECT(grid.has_value());
    if (grid)
    {
        const Voxel near_left = grid->get(grid->voxel_at({ 0.5f, 0.1f, 0.5f }));
        const Voxel near_right = grid->get(grid->voxel_at({ 0.5f, 0.1f, 1.5f }));
        // the voxel centers sit 5 cm off the texel centers: a little of the other texel (sRGB 63 = 5 % linear)
        EXPECT(near_left.r > 240 && near_left.b < 80);
        EXPECT(near_right.b > 240 && near_right.r < 80);
    }

    // skipped materials and errors
    const VoxelizeMaterial skipped{ .skip = true };
    EXPECT(!voxelize(primitives, std::span(&skipped, 1), {}, nullptr, &error) && error == "no triangles to voxelize");
    EXPECT(!voxelize(primitives, std::span(&red, 1), { .voxel_size = 0.001f, .max_voxels = 1000 }, nullptr, &error));

    // determinism: same input, same grid
    const std::optional<VoxelGrid> once = voxelize(wall_primitives, std::span(&textured, 1), { .voxel_size = 0.1f });
    const std::optional<VoxelGrid> again = voxelize(wall_primitives, std::span(&textured, 1), { .voxel_size = 0.1f });
    EXPECT(once && again && once->compute_hash() == again->compute_hash());

    // a house sized benchmark: 8 x 6 x 12 m, two layer walls, ~24k triangles
    TestMesh house;
    add_box(house, { 0.0f, 0.0f, 0.0f }, { 8.0f, 6.0f, 12.0f }, 32);
    add_box(house, { 0.2f, 0.2f, 0.2f }, { 7.8f, 5.8f, 11.8f }, 32);
    const std::array house_primitives{ primitive_of(house) };
    grid = voxelize(house_primitives, std::span(&textured, 1), { .voxel_size = 0.1f }, &stats);
    EXPECT(grid.has_value());
    std::println("voxelizer: house sized box, {} triangles, {} x {} x {}: {} surface + {} filled voxels, {} bricks in {:.1f} ms",
        stats.triangles, stats.size.x, stats.size.y, stats.size.z, stats.surface_voxels, stats.filled_voxels,
        grid ? grid->get_brick_count() : 0u, stats.milliseconds);
    if (grid)
    {
        const auto start = std::chrono::steady_clock::now();
        const std::optional<SurfaceMesh> mesh = build_surface_mesh(*grid);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        EXPECT(mesh.has_value());
        if (mesh)
            std::println("surface mesh: {} quads, atlas {} x {} ({} texels per voxel) in {:.1f} ms", mesh->quads,
                mesh->atlas_width, mesh->atlas_height, mesh->texels_per_voxel, ms);
    }
}

// ---- surface mesh ----

static double mesh_area(const SurfaceMesh& mesh)
{
    double area = 0.0;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const glm::vec3 a = mesh.positions[mesh.indices[i]], b = mesh.positions[mesh.indices[i + 1]], c = mesh.positions[mesh.indices[i + 2]];
        area += 0.5 * double(glm::length(glm::cross(b - a, c - a)));
    }
    return area;
}

void surface_mesh()
{
    VoxelGrid grid(0.5f, glm::vec3(1.0f, 0.0f, 0.0f));
    grid.set({ 5, 5, 5 }, solid());
    std::optional<SurfaceMesh> mesh = build_surface_mesh(grid);
    EXPECT(mesh && mesh->quads == 6 && mesh->positions.size() == 24 && mesh->indices.size() == 36);
    if (!mesh)
        return;
    EXPECT(std::abs(mesh_area(*mesh) - 6.0 * 0.25) < 1e-4);
    // counter-clockwise seen from outside: the winding's normal is the face normal
    bool outward = true;
    for (std::size_t i = 0; i < mesh->indices.size(); i += 3)
    {
        const glm::vec3 a = mesh->positions[mesh->indices[i]], b = mesh->positions[mesh->indices[i + 1]], c = mesh->positions[mesh->indices[i + 2]];
        outward = outward && glm::dot(glm::cross(b - a, c - a), mesh->normals[mesh->indices[i]]) > 0.0f;
    }
    EXPECT(outward);
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const glm::vec3& p : mesh->positions)
    {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    EXPECT(lo == glm::vec3(3.5f, 2.5f, 2.5f) && hi == glm::vec3(4.0f, 3.0f, 3.0f));
    EXPECT(mesh->texels_per_voxel == 4 && mesh->atlas_width >= 8);

    // a block of two colors: still one quad per side, both colors in the atlas
    grid.clear();
    fill(grid, { 0, 0, 0 }, { 2, 1, 0 }, solid(255, 0, 0));
    grid.set({ 2, 1, 0 }, solid(0, 0, 255));
    mesh = build_surface_mesh(grid);
    EXPECT(mesh && mesh->quads == 6);
    if (mesh)
    {
        EXPECT(std::ranges::count(mesh->atlas, 0xff0000ffu) > 0 && std::ranges::count(mesh->atlas, 0xffff0000u) > 0);
        EXPECT(std::abs(mesh_area(*mesh) - 2.0 * (6 + 3 + 2) * 0.25) < 1e-4);
    }

    // meshes of two ranges together cover the same faces as the mesh of the whole grid
    grid.clear();
    for (int z = 0; z < 20; ++z)
        for (int y = 0; y < 12; ++y)
            for (int x = 0; x < 20; ++x)
                if ((x * 7 + y * 13 + z * 5) % 11 < 6)
                    grid.set({ x, y, z }, solid(std::uint8_t(x * 10), std::uint8_t(y * 20), std::uint8_t(z * 10)));
    const std::optional<SurfaceMesh> whole = build_surface_mesh(grid);
    const std::optional<SurfaceMesh> left = build_surface_mesh(grid, GridBounds{ { 0, 0, 0 }, { 9, 11, 19 } });
    const std::optional<SurfaceMesh> right = build_surface_mesh(grid, GridBounds{ { 10, 0, 0 }, { 23, 15, 23 } });
    EXPECT(whole && left && right);
    if (whole && left && right)
        EXPECT(std::abs(mesh_area(*whole) - mesh_area(*left) - mesh_area(*right)) < 1e-3);

    // the atlas shrinks the texels per voxel to fit
    mesh = build_surface_mesh(grid, { .texels_per_voxel = 16, .padding = 1, .max_atlas_size = 512 });
    EXPECT(mesh && mesh->texels_per_voxel < 16 && mesh->atlas_width <= 512 && mesh->atlas_height <= 512);
    std::string error;
    EXPECT(!build_surface_mesh(grid, { .texels_per_voxel = 1, .padding = 1, .max_atlas_size = 16 }, &error) && !error.empty());

    // a fixed atlas: that size exactly, fewer texels per voxel when needed
    mesh = build_surface_mesh(grid, { .texels_per_voxel = 8, .padding = 1, .atlas_width = 2048, .atlas_height = 1024 });
    EXPECT(mesh && mesh->atlas_width == 2048 && mesh->atlas_height == 1024 && mesh->texels_per_voxel <= 8);
    EXPECT(!build_surface_mesh(grid, { .texels_per_voxel = 4, .padding = 1, .atlas_width = 16, .atlas_height = 16 }));

    EXPECT(build_surface_mesh(VoxelGrid{}) && build_surface_mesh(VoxelGrid{})->empty());
}

// ---- 3D JPEG ----

void jpeg3d()
{
    // tables: libjpeg's quality scale, never 0
    const auto best = quantization_table(100, false);
    const auto worst = quantization_table(1, false);
    EXPECT(std::ranges::all_of(best, [] (std::int32_t q) { return q >= 1 && q <= 3; }));
    EXPECT(worst[0] > 100 && worst[511] >= worst[0]);
    EXPECT(quantization_table(50, true)[511] > quantization_table(50, false)[0]);

    // a flat block stays flat (its level moves by the DC quantization); high quality keeps a gradient within a
    // few levels
    std::array<std::int32_t, 512> flat{};
    flat.fill(200);
    jpeg_block(flat, quantization_table(10, false));
    EXPECT(std::ranges::all_of(flat, [&] (std::int32_t v) { return v == flat[0] && std::abs(v - 200) <= 10; }));
    std::array<std::int32_t, 512> ramp{}, coded{};
    for (std::uint32_t i = 0; i < 512; ++i)
    {
        const glm::ivec3 p = local_coord(i);
        ramp[i] = 40 + p.x * 20 + p.y * 3 + ((p.z * 37) % 11);
    }
    coded = ramp;
    jpeg_block(coded, quantization_table(95, false));
    int max_error = 0;
    for (std::uint32_t i = 0; i < 512; ++i)
        max_error = std::max(max_error, std::abs(coded[i] - ramp[i]));
    EXPECT(max_error <= 4);
    // low quality loses detail
    coded = ramp;
    jpeg_block(coded, quantization_table(5, false));
    int low_error = 0;
    for (std::uint32_t i = 0; i < 512; ++i)
        low_error = std::max(low_error, std::abs(coded[i] - ramp[i]));
    EXPECT(low_error > max_error);

    // grid shifts: 1..7, they change between generations
    bool varies = false;
    for (int g = 0; g < 8; ++g)
    {
        const glm::ivec3 s = jpeg_grid_shift(42, g);
        EXPECT(s.x >= 1 && s.x <= 7 && s.y >= 1 && s.y <= 7 && s.z >= 1 && s.z <= 7);
        varies = varies || s != jpeg_grid_shift(42, 0);
    }
    EXPECT(varies);

    // a solid colored box: a hit in the middle recompresses blocks there, far voxels stay, a thick solid stays
    // mostly solid at a moderate quality
    VoxelGrid grid;
    for (int z = 0; z < 48; ++z)
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 48; ++x)
                grid.set({ x, y, z }, solid(std::uint8_t(60 + x * 3), std::uint8_t(90 + y * 2), std::uint8_t(30 + (z * 5) % 120)));
    const std::uint64_t before = grid.get_solid_count();
    const Voxel far_before = grid.get({ 45, 30, 45 });
    VoxelGrid same = grid;
    const VoxelJpegSettings settings{ .quality = 20, .density_quality = 40 };
    const VoxelJpegStats stats = recompress(grid, { 10, 10, 10 }, 8, settings, 1234);
    EXPECT(stats.blocks > 0 && !stats.changed.is_empty());
    EXPECT(grid.get({ 45, 30, 45 }) == far_before);
    EXPECT(grid.get_solid_count() > before * 9 / 10);
    std::println("jpeg3d: solid box, {} blocks, solid {} -> {} in {:.2f} ms", stats.blocks, stats.solid_before,
        stats.solid_after, stats.milliseconds);

    // determinism: the same hit gives the same voxels, another seed other ones
    recompress(same, { 10, 10, 10 }, 8, settings, 1234);
    EXPECT(same.compute_hash() == grid.compute_hash());
    VoxelGrid other = grid;
    recompress(other, { 10, 10, 10 }, 8, settings, 99);
    recompress(grid, { 10, 10, 10 }, 8, settings, 1234);
    EXPECT(other.compute_hash() != grid.compute_hash());

    // dithered holes: none at the edge of the radius, many at the center, the same for the same seed
    {
        auto slab = [] ()
        {
            VoxelGrid g;
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                    g.set({ x, y, 0 }, solid());
            return g;
        };
        auto count_ring = [] (const VoxelGrid& g, float r0, float r1)
        {
            int solid_voxels = 0, all = 0;
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                {
                    const float r = std::sqrt(float((x - 32) * (x - 32) + (y - 32) * (y - 32)));
                    if (r < r0 || r >= r1)
                        continue;
                    ++all;
                    solid_voxels += g.is_solid({ x, y, 0 }) ? 1 : 0;
                }
            return double(solid_voxels) / std::max(all, 1);
        };
        const VoxelJpegSettings holey{ .quality = 100, .density_quality = 100, .key_keep = 0, .key_appear = 256,
                                       .holes = 80, .holes_falloff = 2 };
        VoxelGrid a = slab(), b = slab();
        const VoxelJpegStats holes_stats = recompress(a, { 32, 32, 0 }, 24, holey, 77);
        recompress(b, { 32, 32, 0 }, 24, holey, 77);
        const double inner = count_ring(a, 0.0f, 6.0f), outer = count_ring(a, 18.0f, 24.0f), beyond = count_ring(a, 25.0f, 31.0f);
        std::println("jpeg3d: holes 80%: {} punched, solid {:.2f} at the center, {:.2f} near the edge, {:.2f} outside",
            holes_stats.holes, inner, outer, beyond);
        EXPECT(holes_stats.holes > 0 && a.compute_hash() == b.compute_hash());
        EXPECT(inner < 0.5 && outer > 0.85 && beyond == 1.0);
    }

    // the bricks touched count their generations
    bool counted = false;
    for (const Brick& brick : grid.get_bricks())
        counted = counted || brick.generation > 0;
    EXPECT(counted);

    // a textured wall (stripes along x, 3 voxels thick): the codec keeps a pattern on its surface (the surface
    // colors pulled into the empty voxels), a sharpen raises its contrast
    auto stripes = [] ()
    {
        VoxelGrid g;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                for (int z = 10; z < 13; ++z)
                    g.set({ x, y, z }, ((x / 2) % 2) ? solid(200, 150, 90) : solid(90, 60, 40));
        return g;
    };
    auto surface_contrast = [] (const VoxelGrid& g)
    {
        double sum = 0.0, sum2 = 0.0;
        int n = 0;
        for (int y = 4; y < 28; ++y)
            for (int x = 4; x < 28; ++x)
            {
                const Voxel v = g.get({ x, y, 10 });
                if (v.density == 0)
                    continue;
                sum += v.r;
                sum2 += double(v.r) * v.r;
                ++n;
            }
        return n ? std::sqrt(std::max(sum2 / n - (sum / n) * (sum / n), 0.0)) : 0.0;
    };
    VoxelGrid striped = stripes();
    const double contrast_before = surface_contrast(striped);
    recompress(striped, { 16, 16, 11 }, 24, { .quality = 30, .density_quality = 90 }, 5);
    const double contrast_coded = surface_contrast(striped);
    VoxelGrid sharpened = stripes();
    recompress(sharpened, { 16, 16, 11 }, 24, { .quality = 30, .density_quality = 90, .sharpen = 80 }, 5);
    const double contrast_sharpened = surface_contrast(sharpened);
    std::println("jpeg3d: stripes contrast {:.1f} -> {:.1f}, sharpened {:.1f}", contrast_before, contrast_coded, contrast_sharpened);
    EXPECT(contrast_coded > contrast_before * 0.5);
    EXPECT(contrast_sharpened > contrast_coded);

    // a wall 4 voxels thick under coarse density steps with noise: it breaks up hit by hit, not all at once
    VoxelGrid wall;
    for (int y = 0; y < 40; ++y)
        for (int x = 0; x < 40; ++x)
            for (int z = 20; z < 24; ++z)
                wall.set({ x, y, z }, solid(150, 110, 70));
    const std::uint64_t wall_before = wall.get_solid_count();
    std::uint64_t wall_after = 0;
    for (std::uint32_t hit = 0; hit < 4; ++hit)
    {
        const VoxelJpegStats wall_stats = recompress(wall, { 20, 20, 21 }, 16,
            { .quality = 15, .density_quality = 40, .density_scale = 1600, .density_noise = 120 }, hit);
        wall_after = wall.get_solid_count();
        std::println("jpeg3d: thin wall, hit {}: {} blocks, solid {} -> {}", hit + 1, wall_stats.blocks,
            wall_stats.solid_before, wall_stats.solid_after);
    }
    if constexpr (jpeg_shape_mode == JpegShapeMode::density)
        EXPECT(wall_after > wall_before / 2 && wall_after < wall_before);
    else
        EXPECT(wall_after > wall_before / 2);

    // the black key modes: a voxel of a black color still counts as there (lifted to key_floor), a thin bright
    // wall in black space rings: erode only loses voxels, black_key may grow some next to it
    if constexpr (jpeg_shape_mode != JpegShapeMode::density)
    {
        VoxelGrid keyed;
        for (int y = 0; y < 24; ++y)
            for (int x = 0; x < 24; ++x)
                keyed.set({ x, y, 12 }, (x + y) % 5 ? solid(230, 220, 200) : solid(0, 0, 0));
        const std::uint64_t keyed_before = keyed.get_solid_count();
        std::uint64_t outside = 0;
        recompress(keyed, { 12, 12, 12 }, 16, { .quality = 5, .key_keep = 14, .key_appear = 40 }, 3);
        for (const Brick& brick : keyed.get_bricks())
            for (std::uint32_t i = 0; i < brick_voxels; ++i)
                if (is_solid_bits(brick.voxels[i]) && (brick.coord * brick_size + local_coord(i)).z != 12)
                    ++outside;
        std::println("jpeg3d: black key, one voxel wall at q5: solid {} -> {}, {} outside the wall",
            keyed_before, keyed.get_solid_count(), outside);
        if constexpr (jpeg_shape_mode == JpegShapeMode::black_key_erode)
            EXPECT(outside == 0 && keyed.get_solid_count() <= keyed_before);
    }

    // a house sized hit: radius 4 m at 10 cm on two layer walls
    TestMesh house;
    add_box(house, { 0.0f, 0.0f, 0.0f }, { 8.0f, 6.0f, 12.0f }, 32);
    add_box(house, { 0.25f, 0.25f, 0.25f }, { 7.75f, 5.75f, 11.75f }, 32);
    const VoxelizeMaterial red{ .color = glm::vec4(0.8f, 0.4f, 0.2f, 1.0f) };
    const std::array house_primitives{ primitive_of(house) };
    std::optional<VoxelGrid> voxels = voxelize(house_primitives, std::span(&red, 1), { .voxel_size = 0.1f });
    EXPECT(voxels.has_value());
    if (voxels)
    {
        const VoxelJpegStats hit = recompress(*voxels, voxels->voxel_at({ 4.0f, 3.0f, 0.0f }), 40,
            { .quality = 20, .density_quality = 35 }, 7);
        std::println("jpeg3d: house sized hit, r 40 voxels: {} blocks, solid {} -> {} in {:.1f} ms", hit.blocks,
            hit.solid_before, hit.solid_after, hit.milliseconds);
    }
}

int main()
{
    coordinates();
    voxels_and_bricks();
    surface();
    hashing();
    gpu_layout();
    serialization();
    cache();
    triangle_box();
    voxelizer();
    surface_mesh();
    jpeg3d();
    if (failures)
        std::println("FAILED ({})", failures);
    else
        std::println("ALL PASSED");
    return failures;
}
