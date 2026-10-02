// Tests of the ecs module (standalone: depends only on std and assertions). Build and run target rhea_ecs_tests.

import std;
import ecs;

#include "ecs/ecs_macros.h"

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

struct AutoLog { std::vector<std::string> calls; };
struct EarlySet {};
struct LaterSet {};

namespace
{
    [[=ecs::system<Phase::Update>, =ecs::in_set<LaterSet>, =ecs::after<EarlySet>]]
    void auto_b(ResMut<AutoLog> log) { log->calls.push_back("b"); }

    [[=ecs::system<Phase::Update>, =ecs::in_set<EarlySet>]]
    void auto_z(ResMut<AutoLog> log) { log->calls.push_back("z"); }

    [[=ecs::system<Phase::Update>, =ecs::system<Phase::Late>, =ecs::before<EarlySet>]]
    void auto_y(ResMut<AutoLog> log) { log->calls.push_back("y"); }
}

namespace   // reopened: members_of lists this block first
{
    [[=ecs::system<Phase::Update>, =ecs::after<LaterSet>]]
    void auto_a(ResMut<AutoLog> log) { log->calls.push_back("a"); }

    void not_a_system(ResMut<AutoLog> log) { log->calls.push_back("not_a_system"); }

    [[=ecs::on_add]]
    void velocity_added(Registry& r, Entity, Velocity& v) { r.resource<AutoLog>().calls.push_back(std::format("add {}", v.x)); }

    [[=ecs::on_remove]]
    void velocity_removed(Registry& r, Entity, Velocity& v) { r.resource<AutoLog>().calls.push_back(std::format("remove {}", v.x)); }

    ECS_REGISTER()
}

void auto_systems()
{
    Registry r;
    Schedule s;
    r.set_resource<AutoLog>();
    s.add_auto_systems();
    EXPECT(s.get_systems().size() == 5);
    EXPECT(set_name<EarlySet>() == "EarlySet");
    EXPECT(s.get_systems()[0].name == "auto_a");   // registered by name, ordered by before / after
    EXPECT(s.get_phase_systems(Phase::Late).size() == 1 && s.get_phase_systems(Phase::Late)[0]->name == "auto_y");
    s.run_frame(r, 0.0);
    EXPECT((r.resource<AutoLog>().calls == std::vector<std::string>{ "y", "z", "b", "a", "y" }));
    // all of them write AutoLog, but every pair is ordered (transitively)
    EXPECT(s.find_ambiguities().empty());

    add_auto_hooks(r);
    r.resource<AutoLog>().calls.clear();
    const Entity e = r.create();
    r.add<Velocity>(e, 2.f, 0.f);
    r.destroy(e);
    EXPECT((r.resource<AutoLog>().calls == std::vector<std::string>{ "add 2", "remove 2" }));
    (void)&not_a_system;
}

void ambiguities()
{
    Schedule s;
    s.add(Phase::Update, "w1", [](Query<Position>) {}).sets = { set_name<EarlySet>() };
    System& w2 = s.add(Phase::Update, "w2", [](Query<Position, const Velocity>) {});
    w2.sets = { set_name<LaterSet>() };
    w2.after = { set_name<EarlySet>() };
    System& r1 = s.add(Phase::Update, "r1", [](Query<const Position>) {});
    r1.after = { set_name<EarlySet>() };
    r1.ambiguous_with = { set_name<LaterSet>() };       // w2 / r1: intended
    s.add(Phase::Update, "v", [](Query<Velocity>) {});   // v / w2: Velocity
    s.add(Phase::Update, "reads", [](Query<const Position>, Query<const Velocity>) {});   // reads what w1, w2, v write
    s.add(Phase::Late, "log1", [](ResMut<AutoLog>) {});
    s.add(Phase::Late, "log2", [](Res<AutoLog>) {});
    s.add(Phase::Late, "exclusive", [](Registry&) {});

    std::vector<std::string> found;
    for (const Ambiguity& a : s.find_ambiguities())
        found.push_back(std::format("{}/{}: {}", a.first->name, a.second->name, a.conflict));
    std::ranges::sort(found);
    const std::vector<std::string> expected = {
        "log1/exclusive: Registry& (exclusive)",
        "log1/log2: AutoLog",
        "log2/exclusive: Registry& (exclusive)",
        "v/reads: Velocity",
        "w1/reads: Position",
        "w2/reads: Position",
        "w2/v: Velocity",
    };
    EXPECT(found == expected);
    if (found != expected)
        for (const std::string& f : found)
            std::println("  {}", f);
}

void filters()
{
    // With / Without: filter without handing the component out
    Registry r;
    Entity a = r.create(); r.add<Position>(a, 1.f, 0.f); r.add<Velocity>(a, 1.f, 0.f);
    Entity b = r.create(); r.add<Position>(b, 2.f, 0.f); r.add<Dead>(b);
    Entity c = r.create(); r.add<Position>(c, 3.f, 0.f); r.add<Velocity>(c, 0.f, 1.f); r.add<Dead>(c);

    std::vector<float> alive;
    Query<const Position, Without<Dead>>(r).each([&](const Position& p) { alive.push_back(p.x); });
    EXPECT(alive == std::vector<float>{ 1.f });

    std::vector<float> moving_dead;
    Query<Position, With<Velocity>, With<Dead>>(r).each([&](Entity, Position& p) { moving_dead.push_back(p.x); });
    EXPECT(moving_dead == std::vector<float>{ 3.f });

    EXPECT((Query<const Position, Without<Velocity>>(r).count() == 1));
    EXPECT((Query<const Position, With<Dead>, Without<Velocity>>(r).count() == 1));
    EXPECT((Query<const Position, Without<Dead>>(r).matches(a)));
    EXPECT((!Query<const Position, Without<Dead>>(r).matches(b)));
    EXPECT((Query<Position, Without<Dead>>(r).get(a)->x == 1.f));

    // filters are no access: a reader of Position with Without<Dead> conflicts only with Position writers
    Access access;
    Query<const Position, Without<Dead>, With<Velocity>>::describe_access(access);
    EXPECT(access.reads.size() == 1 && access.writes.empty());
}

struct HitEvent { int id = 0; };
struct Shout { int frame = 0; };
struct EarlyListeners {};   // a set

void events()
{
    // EventWriter / EventReader: every reader sees every event once, in order, across 0..N fixed ticks a frame
    Registry r;
    Schedule s;
    s.fixed_dt = 1.0 / 60.0;

    int next_hit = 0;
    std::vector<int> update_seen, late_seen, fixed_seen_shouts, update_seen_shouts;
    int frame_counter = 0;

    s.add(Phase::Fixed, "shoot", [&](EventWriter<HitEvent> hits) { hits.send({ next_hit++ }); });
    s.add(Phase::Update, "update_reader", [&](EventReader<HitEvent> hits) {
        for (const HitEvent& h : hits.read())
            update_seen.push_back(h.id);
    });
    s.add(Phase::Late, "late_reader", [&](EventReader<HitEvent> hits) {
        for (const HitEvent& h : hits.read())
            late_seen.push_back(h.id);
    });
    // per frame events read by a fixed phase system, and by an Update system that runs before the sender
    s.add(Phase::Update, "shout", [&](EventWriter<Shout> shouts) { shouts.emplace(frame_counter++); })
        .after.push_back(set_name<EarlyListeners>());
    s.add(Phase::FixedPre, "fixed_listener", [&](EventReader<Shout> shouts) {
        for (const Shout& sh : shouts.read())
            fixed_seen_shouts.push_back(sh.frame);
    });
    s.add(Phase::Update, "early_listener", [&](EventReader<Shout> shouts) {
        for (const Shout& sh : shouts.read())
            update_seen_shouts.push_back(sh.frame);
    }).sets.push_back(set_name<EarlyListeners>());   // runs before "shout": sees each shout one frame later

    const double tick = 1.0 / 60.0;
    // 1 tick, 0 ticks (high frame rate), 4 ticks (a hitch), ...
    const std::vector<double> frames = { tick, tick * 0.2, tick * 0.2, tick * 0.2, tick * 0.2, tick * 4.0, tick, tick * 0.5,
        tick * 0.5, tick * 3.0, tick * 0.1, tick };
    for (double dt : frames)
        s.run_frame(r, dt);

    std::vector<int> expected_hits(next_hit);
    std::iota(expected_hits.begin(), expected_hits.end(), 0);
    EXPECT(next_hit >= 8);
    EXPECT(update_seen == expected_hits);
    EXPECT(late_seen == expected_hits);

    std::vector<int> expected_shouts(frame_counter);
    std::iota(expected_shouts.begin(), expected_shouts.end(), 0);
    // the fixed listener has not seen the shouts after the last tick yet; the early listener misses the last one
    EXPECT(update_seen_shouts.size() == expected_shouts.size() - 1);
    EXPECT(std::ranges::equal(update_seen_shouts, expected_shouts | std::views::take(frame_counter - 1)));
    EXPECT(!fixed_seen_shouts.empty());
    EXPECT(std::ranges::equal(fixed_seen_shouts, expected_shouts | std::views::take(fixed_seen_shouts.size())));
    // 0-tick frames: the shouts of 4 frames waited for the next tick
    EXPECT(fixed_seen_shouts.size() >= 5);

    // old events go
    s.run_frame(r, tick * 3.0);
    s.run_frame(r, tick * 3.0);
    // the fixed sender keeps sending: only the HitEvents of the last two frames are alive (at most 4 ticks each)
    EXPECT(r.events<HitEvent>().size() <= 8);
    EXPECT(r.events<Shout>().size() <= 2);

    // ambiguities: a writer and a reader without an order conflict, two writers do not
    Schedule a;
    a.add(Phase::Update, "w1", [](EventWriter<HitEvent>) {});
    a.add(Phase::Update, "w2", [](EventWriter<HitEvent>) {});
    a.add(Phase::Update, "rd", [](EventReader<HitEvent>) {});
    std::vector<std::string> found;
    for (const Ambiguity& amb : a.find_ambiguities())
        found.push_back(std::format("{}/{}", amb.first->name, amb.second->name));
    std::ranges::sort(found);
    EXPECT(found == std::vector<std::string>{ "w1/rd", "w2/rd" });
    if (found != std::vector<std::string>{ "w1/rd", "w2/rd" })
        for (const std::string& f : found)
            std::println("  {}", f);
}

int main()

{
    external_resources();
    basic();
    commands_and_hooks();
    schedule();
    auto_systems();
    ambiguities();
    events();
    filters();
    stress();
    if (failures)
        std::println("FAILED ({})", failures);
    else
        std::println("ALL PASSED");
    return failures;
}
