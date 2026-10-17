using AnodyneSharp.Recording;
using Microsoft.Xna.Framework;
using System;
using System.Collections.Generic;

static class PolicyTests
{
    static PCtx Ctx(Sim s, Sim.Enemy e) => new PCtx { P = s.PlayerBox, Facing = s.Facing, T = e.Box, TV = e.V, Walk = s.Walk };

    // Runs one episode; returns (killed, damage, frames).
    public static int VerboseSeed = -1;
    static (bool ok, int dmg, int frames) Episode(Sim s, Func<Sim, Sim.Enemy, PolicyMem, PKey[]> policy, int maxFrames, int seed = -2)
    {
        var mem = new PolicyMem();
        var e = s.Enemies[0];
        int lastDmg = 0;
        for (int f = 0; f < maxFrames && !s.AllDead(); f++)
        {
            var keys = policy(s, e, mem) ?? new PKey[0];
            if (seed == VerboseSeed && (f % 10 == 0 || s.Damage != lastDmg)) { Console.WriteLine($"f{f} P={s.PlayerBox} face={s.Facing} E={e.Box} v={e.V} hp={e.Health} keys={string.Join("+", keys)} dmg={s.Damage} swing={s.SwingT}"); }
            lastDmg = s.Damage;
            s.Step(keys);
        }
        return (s.AllDead(), s.Damage, s.Frame);
    }

    public static int Run(Action<string, bool, string> check)
    {
        int bad = 0;
        // 1. stationary Shieldy, we start somewhere north of it (random column, random height)
        {
            int fail = 0, dmgRuns = 0, worst = 0;
            for (int seed = 0; seed < 300; seed++)
            {
                var s = new Sim(seed, 14, 12);
                var e = new Sim.Enemy { K = Sim.Kind.Shieldy, Pos = new Vector2(100, 120), W = 10, H = 10 };
                s.Enemies.Add(e);
                s.P = new Vector2(30 + s.Rng.Next(120), 24 + s.Rng.Next(60));
                var (ok, dmg, fr) = Episode(s, (sim, en, m) => FightPolicies.BoomStationary(Ctx(sim, en), m) is PKey[] k ? k : Walkto(sim, en), 900);
                if (!ok) { fail++; }
                if (dmg > 0) { dmgRuns++; }
                worst = Math.Max(worst, fr);
            }
            check("stationary Shieldy from the north: dies, no damage (300 seeds)", fail == 0 && dmgRuns == 0, $"(fail {fail}, damaged {dmgRuns}, worst {worst} frames)");
            bad += (fail + dmgRuns > 0) ? 1 : 0;
        }
        // 2. slime wandering: start 30..90px away
        {
            int fail = 0, dmgRuns = 0, worst = 0;
            for (int seed = 0; seed < 300; seed++)
            {
                var s = new Sim(seed, 14, 12);
                var e = new Sim.Enemy { K = Sim.Kind.Slime, Pos = new Vector2(100, 80), W = 16, H = 16 };
                s.Enemies.Add(e);
                double a = s.Rng.NextDouble() * Math.PI * 2; double d = 40 + s.Rng.NextDouble() * 50;
                s.P = new Vector2(108 + (float)(Math.Cos(a) * d), 88 + (float)(Math.Sin(a) * d));
                s.P.X = Math.Clamp(s.P.X, 20, 150); s.P.Y = Math.Clamp(s.P.Y, 20, 150);
                var (ok, dmg, fr) = Episode(s, (sim, en, m) => FightPolicies.Slime(Ctx(sim, en), m), 2400);
                if (!ok) { fail++; }
                if (dmg > 0) { dmgRuns++; }
                worst = Math.Max(worst, fr);
            }
            check("slime: dies, no damage (300 seeds)", fail == 0 && dmgRuns == 0, $"(fail {fail}, damaged {dmgRuns}, worst {worst} frames)");
            bad += (fail + dmgRuns > 0) ? 1 : 0;
        }
        // 2b. slime, we start pressed against a wall with the slime close on the open side
        {
            int fail = 0, dmgRuns = 0, worst = 0, firstBadC = -1;
            for (int seed = 0; seed < 300; seed++)
            {
                var s = new Sim(seed, 14, 12);
                bool top = seed % 2 == 0;
                s.P = new Vector2(80 + s.Rng.Next(40), top ? 16 : 192 - 16 - 12);
                var e = new Sim.Enemy { K = Sim.Kind.Slime, Pos = new Vector2(s.P.X - 3 + s.Rng.Next(6), top ? s.P.Y + 12 + 2 + s.Rng.Next(10) : s.P.Y - 16 - 2 - s.Rng.Next(10)), W = 16, H = 16 };
                s.Enemies.Add(e);
                var (ok, dmg, fr) = Episode(s, (sim, en, m) => FightPolicies.Slime(Ctx(sim, en), m), 2400, 1000 + seed);
                if (!ok) { fail++; }
                if (dmg > 0) { dmgRuns++; if (firstBadC < 0) { firstBadC = seed; } }
                worst = Math.Max(worst, fr);
            }
            Console.WriteLine($"  first damaged cornered seed: {firstBadC}");
            check("slime, cornered against a wall: dies, damage in under 2% of pinned starts (300 seeds)", fail == 0 && dmgRuns <= 6, $"(fail {fail}, damaged {dmgRuns}, worst {worst} frames)");
            bad += (fail + dmgRuns > 0) ? 1 : 0;
        }
        // 3. Shieldy walking up and down a column, we start above it
        {
            int fail = 0, dmgRuns = 0, worst = 0, firstBad = -1;
            for (int seed = 0; seed < 300; seed++)
            {
                var s = new Sim(seed, 14, 12);
                var e = new Sim.Enemy { K = Sim.Kind.Shieldy, Pos = new Vector2(100, 70 + s.Rng.Next(80)), W = 10, H = 10, V = new Vector2(0, s.Rng.Next(2) == 0 ? 30 : -30) };
                s.Enemies.Add(e);
                s.P = new Vector2(60 + s.Rng.Next(90), 20 + s.Rng.Next(30));
                var (ok, dmg, fr) = Episode(s, (sim, en, m) => FightPolicies.VerticalShieldy(Ctx(sim, en), m) is PKey[] k ? k : Walkto(sim, en), 2400, seed);
                if (!ok) { fail++; }
                if (dmg > 0) { dmgRuns++; if (firstBad < 0) { firstBad = seed; } }
                worst = Math.Max(worst, fr);
            }
            Console.WriteLine($"  first damaged vertical seed: {firstBad}");
            check("vertical Shieldy from the north: dies, no damage (300 seeds)", fail == 0 && dmgRuns == 0, $"(fail {fail}, damaged {dmgRuns}, worst {worst} frames)");
            bad += (fail + dmgRuns > 0) ? 1 : 0;
        }
        return bad;
    }

    // crude navigation for when the policy does not apply: head for the spot 20px above the enemy
    static PKey[] Walkto(Sim s, Sim.Enemy e)
    {
        var pb = s.PlayerBox;
        float tx = e.Box.Center.X - 5, ty = e.Box.Top - 18;
        if (Math.Abs(tx - pb.X) > 3) { return new[] { tx < pb.X ? PKey.Left : PKey.Right }; }
        if (Math.Abs(ty - pb.Y) > 3) { return new[] { ty < pb.Y ? PKey.Up : PKey.Down }; }
        return new PKey[0];
    }
}

static class Debug
{
    public static void Vertical(int seed)
    {
        var s = new Sim(seed, 14, 12);
        var e = new Sim.Enemy { K = Sim.Kind.Shieldy, Pos = new Vector2(100, 70 + s.Rng.Next(80)), W = 10, H = 10, V = new Vector2(0, s.Rng.Next(2) == 0 ? 30 : -30) };
        s.Enemies.Add(e);
        s.P = new Vector2(60 + s.Rng.Next(90), 20 + s.Rng.Next(30));
        var m = new PolicyMem();
        int lastDmg = 0;
        for (int f = 0; f < 700 && !s.AllDead(); f++)
        {
            var pb = s.PlayerBox;
            var k = e.Box.Top > pb.Bottom - 2 ? FightPolicies.VerticalShieldy(new PCtx { P = pb, Facing = s.Facing, T = e.Box, TV = e.V, Walk = s.Walk }, m) : null;
            string ks = k == null ? "walk" : string.Join("+", k);
            if (f % 10 == 0 || s.Damage != lastDmg) { Console.WriteLine($"f{f} P={pb} face={s.Facing} E={e.Box} v={e.V.Y} hp={e.Health} keys={ks} dmg={s.Damage} swing={s.SwingT}"); }
            lastDmg = s.Damage;
            s.Step(k ?? new PKey[0]);
            if (f > 200 && s.Damage > 0) { break; }
        }
    }
}
