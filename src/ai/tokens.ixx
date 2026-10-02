export module ai:tokens;

import std.compat;
import ecs;

// Attack tokens (AI4): how many attackers may go for one target at once, whatever pack they come from (Doom 2016
// style). An attacker takes a token of the target before it attacks and gives it back when the attack ends
// (the action's stop). A token also runs out after `hold` s, so the ones of attackers that died or were
// destroyed mid attack come back by themselves. A resource of the world (ai::AttackTokens), created with the
// first Perception.
export namespace ai
{
    class AttackTokens
    {
    public:
        // true when attacker holds one of target's tokens (it already had one or got one of the `capacity`)
        bool acquire(ecs::Entity target, ecs::Entity attacker, int capacity, double now, double hold)
        {
            int held = 0;
            for (Holder& h : holders)
            {
                if (h.target != target)
                    continue;
                if (h.attacker == attacker)
                {
                    h.until = now + hold;
                    return true;
                }
                ++held;
            }
            if (held >= capacity)
                return false;
            holders.push_back({ target, attacker, now + hold });
            return true;
        }

        void release(ecs::Entity target, ecs::Entity attacker)
        {
            std::erase_if(holders, [&] (const Holder& h) { return h.target == target && h.attacker == attacker; });
        }

        // every token of the attacker (it died, it forgot its target)
        void release_all(ecs::Entity attacker)
        {
            std::erase_if(holders, [&] (const Holder& h) { return h.attacker == attacker; });
        }

        bool holds(ecs::Entity target, ecs::Entity attacker) const
        {
            return std::ranges::any_of(holders, [&] (const Holder& h) { return h.target == target && h.attacker == attacker; });
        }

        int held(ecs::Entity target) const
        {
            return int(std::ranges::count_if(holders, [&] (const Holder& h) { return h.target == target; }));
        }

        void expire(double now)
        {
            std::erase_if(holders, [&] (const Holder& h) { return h.until <= now; });
        }

    private:
        struct Holder
        {
            ecs::Entity target;
            ecs::Entity attacker;
            double until = 0.0;
        };
        std::vector<Holder> holders;
    };
}
