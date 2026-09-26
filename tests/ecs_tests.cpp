// Tests of the ecs module (standalone: depends only on std and assertions). Build and run target rhea_ecs_tests.

import std;
import ecs;

using namespace ecs;

struct Position { float x = 0, y = 0; };
struct Velocity { float x = 0, y = 0; };
struct Name { std::string value; };
struct Dead {};                               // tag
struct Big { alignas(64) double v[8]; };      // over-aligned

static int failures = 0;
#define EXPECT(...) do { if (!(__VA_ARGS__)) { std::println("FAIL {}:{}: {}", __LINE__, __func__, #__VA_ARGS__); ++failures; } } while (0)

void integrate(Query<Position, const Velocity> q, Res<SimTime> t)
{
    q.each([&](Position& p, const Velocity& v) { p.x += v.x * float(t->dt); p.y += v.y * float(t->dt); });
}

struct TickLog { std::vector<std::uint64_t> ticks; int updates = 0; };

void basic()
{
    Registry r;
    Entity a = r.create();
    r.add<Position>(a, 1.f, 2.f);
    r.add<Name>(a, "alpha");
    Entity b = r.create();
    r.add<Name>(b, "beta");
    r.add<Position>(b, 3.f, 4.f);
    r.add<Velocity>(b, 1.f, 0.f);
    r.add<Dead>(b);

    EXPECT(r.entity_count() == 2);
    EXPECT(r.get<Name>(a)->value == "alpha");
    EXPECT(r.get<Name>(b)->value == "beta");
    EXPECT(r.has<Dead>(b) && !r.has<Dead>(a));
    EXPECT(r.get<Velocity>(a) == nullptr);

    int n = 0;
    Query<const Position, const Name>(r).each([&](Entity e, const Position& p, const Name& nm) {
        ++n;
        if (e == a) EXPECT(p.x == 1.f && nm.value == "alpha");
        if (e == b) EXPECT(p.x == 3.f && nm.value == "beta");
    });
    EXPECT(n == 2);
    EXPECT(Query<Dead>(r).count() == 1);
    EXPECT(Query<Velocity, Dead>(r).count() == 1);

    r.remove<Dead>(b);
    EXPECT(!r.has<Dead>(b) && r.get<Name>(b)->value == "beta" && r.get<Velocity>(b)->x == 1.f);
    r.add<Position>(a, 9.f, 9.f); // replace
    EXPECT(r.get<Position>(a)->x == 9.f);

    r.destroy(a);
    EXPECT(!r.alive(a) && r.alive(b) && r.entity_count() == 1);
    EXPECT(r.get<Name>(a) == nullptr);
    Entity c = r.create();           // reuses a's slot with new generation
    EXPECT(c.index == a.index && c.generation != a.generation);
    EXPECT(!r.alive(a) && r.alive(c));
    EXPECT(std::format("{}", Entity{}) == "null");

    Entity big = r.create();
    auto& bg = r.add<Big>(big);
    EXPECT(reinterpret_cast<std::uintptr_t>(&bg) % 64 == 0);
    EXPECT(component_info(component_id<Name>()).name == "Name");
}

void commands_and_hooks()
{
    Registry r;
    int added = 0, removed = 0;
    std::string last_removed;
    r.on_add<Name>([&](Registry&, Entity, Name& n) { ++added; EXPECT(!n.value.empty()); });
    r.on_remove<Name>([&](Registry&, Entity, Name& n) { ++removed; last_removed = n.value; });
    r.on_add<Dead>([&](Registry&, Entity, Dead&) { ++added; });

    Commands cmd(r);
    Entity e = cmd.spawn(Name{"spawned"}, Position{5, 5});
    EXPECT(!r.alive(e));
    Entity gone = cmd.spawn();
    cmd.destroy(gone);
    cmd.add(e, Dead{});
    cmd.apply();
    EXPECT(r.alive(e) && !r.alive(gone));
    EXPECT(r.get<Name>(e)->value == "spawned" && r.get<Position>(e)->x == 5);
    EXPECT(added == 2);

    Commands cmd2(r);
    Query<const Name>(r).each([&](Entity x, const Name&) { cmd2.destroy(x); cmd2.add(x, Velocity{}); });
    cmd2.apply();
    EXPECT(!r.alive(e) && removed == 1 && last_removed == "spawned");
    EXPECT(r.entity_count() == 0);
}

void schedule()
{
    Registry r;
    Schedule s;
    s.fixed_dt = 0.1;
    r.set_resource<TickLog>();
    s.add<&integrate>(Phase::Fixed);
    s.add(Phase::Fixed, "log_tick", [](ResMut<TickLog> log, Res<SimTime> t) { log->ticks.push_back(t->tick); });
    s.add(Phase::Update, "count_updates", [](ResMut<TickLog> log) { ++log->updates; });
    s.add(Phase::Fixed, "spawner", [](Commands& cmd, Res<SimTime> t) { if (t->tick == 1) cmd.spawn(Position{}, Velocity{10, 0}); });

    EXPECT(s.get_systems()[0].name == "integrate");
    EXPECT(s.get_systems()[0].access.writes.size() == 1 && s.get_systems()[0].access.reads.size() == 1);

    s.run_frame(r, 0.05);   // 0 ticks
    EXPECT(r.resource<TickLog>().ticks.empty());
    s.run_frame(r, 0.06);   // 1 tick (0.11)
    s.run_frame(r, 0.25);   // 2 ticks (0.36 -> 3 ticks total)
    auto& log = r.resource<TickLog>();
    EXPECT((log.ticks == std::vector<std::uint64_t>{1, 2, 3}));
    EXPECT(log.updates == 3);
    // spawned at end of tick 1, integrated in ticks 2 and 3 -> x = 2 * 10 * 0.1
    float x = -1; Query<const Position>(r).each([&](const Position& p) { x = p.x; });
    EXPECT(std::abs(x - 2.f) < 1e-5f);
    EXPECT(std::abs(r.resource<FrameTime>().alpha - 0.6) < 1e-6);
    s.run_frame(r, 10.0);   // hitch: capped
    EXPECT(log.ticks.size() == 3 + s.max_ticks_per_frame);
}

void stress()
{
    Registry r;
    std::mt19937 rng(42);
    struct Ref { std::optional<std::string> name; std::optional<float> pos; bool dead = false; };
    std::unordered_map<Entity, Ref> ref;
    std::vector<Entity> live;
    for (int step = 0; step < 200000; ++step)
    {
        int op = rng() % 7;
        if (op == 0 || live.empty()) { Entity e = r.create(); live.push_back(e); ref[e]; continue; }
        size_t i = rng() % live.size(); Entity e = live[i]; Ref& rf = ref[e];
        switch (op)
        {
        case 1: { std::string s = std::format("n{}", step); r.add<Name>(e, s); rf.name = s; break; }
        case 2: r.remove<Name>(e); rf.name.reset(); break;
        case 3: { float v = float(step); r.add<Position>(e, v, v); rf.pos = v; break; }
        case 4: r.remove<Position>(e); rf.pos.reset(); break;
        case 5: if (rf.dead) r.remove<Dead>(e); else r.add<Dead>(e); rf.dead = !rf.dead; break;
        case 6: r.destroy(e); ref.erase(e); live[i] = live.back(); live.pop_back(); break;
        }
    }
    EXPECT(r.entity_count() == live.size());
    size_t names = 0;
    for (Entity e : live)
    {
        const Ref& rf = ref[e];
        EXPECT(r.alive(e));
        EXPECT(rf.name.has_value() == r.has<Name>(e));
        if (rf.name) { EXPECT(r.get<Name>(e)->value == *rf.name); ++names; }
        EXPECT(rf.pos.has_value() == r.has<Position>(e));
        if (rf.pos) EXPECT(r.get<Position>(e)->x == *rf.pos);
        EXPECT(rf.dead == r.has<Dead>(e));
    }
    EXPECT(Query<const Name>(r).count() == names);
    size_t visited = 0;
    Query<const Name>(r).each([&](Entity e, const Name& n) { ++visited; EXPECT(ref[e].name == n.value); });
    EXPECT(visited == names);
    std::println("stress: {} alive, {} archetypes", live.size(), r.get_archetypes().size());
}

struct ExternalService { int calls = 0; };
struct Settings { float speed = 2.0f; };

void use_services(ResMut<ExternalService> service, Res<Settings> settings, Query<Position> q)
{
    ++service->calls;
    q.each([&](Position& p) { p.x += settings->speed; });
}

void external_resources()
{
    Registry r;
    Schedule s;
    ExternalService service;
    const Settings settings;
    r.set_resource_ref(service);
    r.set_resource_ref(settings);   // const: registered as Settings
    r.create();
    r.add<Position>(r.create());
    s.add<&use_services>(Phase::Update);
    EXPECT(s.get_systems()[0].access.resource_writes.size() == 1 && s.get_systems()[0].access.resource_reads.size() == 1);
    s.run_frame(r, 0.0);
    s.run_frame(r, 0.0);
    EXPECT(service.calls == 2);
    EXPECT(&r.resource<ExternalService>() == &service);
    float x = 0; Query<const Position>(r).each([&](const Position& p) { x = p.x; });
    EXPECT(x == 4.0f);
}   // the registry must not delete the external objects

int main()
{
    external_resources();
    basic();
    commands_and_hooks();
    schedule();
    stress();
    if (failures)
        std::println("FAILED ({})", failures);
    else
        std::println("ALL PASSED");
    return failures;
}
