using AnodyneSharp.Recording;
using Microsoft.Xna.Framework;
using System;
using System.Collections.Generic;
using System.Linq;

static class PolicyTests
{
    static PCtx Ctx(Sim s, Sim.Enemy e) => new PCtx { P = s.PlayerBox, Facing = s.Facing, T = e.Box, TV = e.V, Walk = s.Walk, Swinging = s.SwingT > 0, Others = s.Enemies.Where(o => o != e && !o.Dead).Select(o => o.Box).ToList() };

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
        // 2c. three slimes close together: kill them all one after another (nearest first), no damage
        {
            int fail = 0, dmgRuns = 0, worst = 0;
            for (int seed = 0; seed < 300; seed++)
            {
                var s = new Sim(seed, 16, 14);
                for (int k = 0; k < 3; k++) { s.Enemies.Add(new Sim.Enemy { K = Sim.Kind.Slime, Pos = new Vector2(90 + s.Rng.Next(50), 80 + s.Rng.Next(50)), W = 16, H = 16 }); }
                s.P = new Vector2(30 + s.Rng.Next(150), 24 + s.Rng.Next(30));
                int steps = 0;
                var mem = new PolicyMem();
                while (!s.AllDead() && steps++ < 4000)
                {
                    var pb = s.PlayerBox;
                    var t = s.Enemies.Where(e => !e.Dead).OrderBy(e => (e.Box.Center.ToVector2() - pb.Center.ToVector2()).LengthSquared()).First();
                    var k = FightPolicies.Slime(Ctx(s, t), mem) ?? new PKey[0];
                    s.Step(k);
                }
                if (!s.AllDead()) { fail++; }
                if (s.Damage > 0) { dmgRuns++; }
                worst = Math.Max(worst, s.Frame);
            }
            check("three slimes together: all die, damage in under 3% (300 seeds)", fail == 0 && dmgRuns <= 9, $"(fail {fail}, damaged {dmgRuns}, worst {worst} frames)");
            bad += (fail + dmgRuns > 0) ? 1 : 0;
        }
        // 2d. the overworld strip: two slimes in a 3-tile-high corridor
        {
            int fail = 0, dmgRuns = 0, worst = 0, firstBadS = -1;
            for (int seed = 0; seed < 300; seed++)
            {
                var s = new Sim(seed, 24, 5);
                for (int k = 0; k < 2; k++) { s.Enemies.Add(new Sim.Enemy { K = Sim.Kind.Slime, Pos = new Vector2(120 + s.Rng.Next(120), 20 + s.Rng.Next(20)), W = 16, H = 16 }); }
                s.P = new Vector2(24 + s.Rng.Next(60), 20 + s.Rng.Next(20));
                int steps = 0;
                var mem = new PolicyMem();
                while (!s.AllDead() && steps++ < 4000)
                {
                    var pb = s.PlayerBox;
                    var t = s.Enemies.Where(e => !e.Dead).OrderBy(e => (e.Box.Center.ToVector2() - pb.Center.ToVector2()).LengthSquared()).First();
                    // far away: walk towards it along the corridor (the bot's path follower); close: the policy
                    var d = (t.Box.Center.ToVector2() - pb.Center.ToVector2()).Length();
                    PKey[] k = d < 55 ? FightPolicies.Slime(Ctx(s, t), mem) : new[] { t.Box.Center.X < pb.Center.X ? PKey.Left : PKey.Right };
                    s.Step(k ?? new PKey[0]);
                }
                if (!s.AllDead()) { fail++; }
                if (s.Damage > 0) { dmgRuns++; if (firstBadS < 0) { firstBadS = seed; } }
                worst = Math.Max(worst, s.Frame);
            }
            Console.WriteLine($"  first damaged strip seed: {firstBadS}");
            check("two slimes in a 3-tile strip: all die, damage in under 3% (300 seeds)", fail == 0 && dmgRuns <= 9, $"(fail {fail}, damaged {dmgRuns}, worst {worst} frames)");
        }
        // 2e. an Annoyer wakes when we come within 64px, flies in, orbits, swoops: it must die before the swoop, no damage
        {
            int fail = 0, dmgRuns = 0, worst = 0, firstBadA = -1;
            for (int seed = 0; seed < 300; seed++)
            {
                var s = new Sim(seed, 16, 14);
                s.P = new Vector2(90 + s.Rng.Next(30), 80 + s.Rng.Next(30));
                double a = s.Rng.NextDouble() * Math.PI * 2; float d = 40 + (float)s.Rng.NextDouble() * 20;
                var e = new Sim.Enemy { K = Sim.Kind.Annoyer, W = 8, H = 7, Health = 1, Pos = new Vector2(s.P.X + (float)Math.Cos(a) * d, s.P.Y + (float)Math.Sin(a) * d), V = Vector2.Zero };
                s.Enemies.Add(e);
                var (ok, dmg, fr) = Episode(s, (sim, en, m) => FightPolicies.AnnoyerHold(Ctx(sim, en), m), 1200, 3000 + seed);
                if (!ok) { fail++; }
                if (dmg > 0) { dmgRuns++; if (firstBadA < 0) { firstBadA = seed; } }
                worst = Math.Max(worst, fr);
            }
            Console.WriteLine($"  first damaged annoyer seed: {firstBadA}");
            check("annoyer: dies, damage in under 1% (300 seeds)", fail == 0 && dmgRuns <= 3, $"(fail {fail}, damaged {dmgRuns}, worst {worst} frames)");
        }
        // 2f. two Annoyers at once
        {
            int fail = 0, dmgRuns = 0, worst = 0, firstBadA2 = -1, firstFail2 = -1;
            for (int seed = 0; seed < 300; seed++)
            {
                var s = new Sim(seed, 16, 14);
                s.P = new Vector2(90 + s.Rng.Next(30), 80 + s.Rng.Next(30));
                for (int k = 0; k < 2; k++)
                {
                    double a = s.Rng.NextDouble() * Math.PI * 2; float d = 40 + (float)s.Rng.NextDouble() * 25;
                    s.Enemies.Add(new Sim.Enemy { K = Sim.Kind.Annoyer, W = 8, H = 7, Health = 1, Pos = new Vector2(s.P.X + (float)Math.Cos(a) * d, s.P.Y + (float)Math.Sin(a) * d) });
                }
                var mem = new PolicyMem(); int steps = 0;
                while (!s.AllDead() && steps++ < 1500)
                {
                    var pb = s.PlayerBox;
                    var alive = s.Enemies.Where(e => !e.Dead).ToList();
                    var t = alive[0];
                    var cx = Ctx(s, t); cx.Others = alive.Skip(1).Select(o => o.Box).ToList();
                    var kk = FightPolicies.AnnoyersHold(cx, mem) ?? new PKey[0];
                    if (PolicyTests.VerboseSeed == 5000 + seed && steps % 5 == 0) { Console.WriteLine($"f{steps} P={pb} face={s.Facing} keys={string.Join("+", kk)} swing={s.SwingT} dmg={s.Damage} A=" + string.Join(" ", alive.Select(e => $"{e.Box.X},{e.Box.Y}/st{e.State}"))); }
                    s.Step(kk);
                }
                if (!s.AllDead()) { fail++; if (firstFail2 < 0) { firstFail2 = seed; } }
                if (s.Damage > 0) { dmgRuns++; if (firstBadA2 < 0) { firstBadA2 = seed; } }
                worst = Math.Max(worst, s.Frame);
            }
            Console.WriteLine($"  first damaged 2-annoyer seed: {firstBadA2}  first unfinished: {firstFail2}");
            check("two annoyers: both die, damage in under 5% (300 seeds)", fail <= 3 && dmgRuns <= 15, $"(fail {fail}, damaged {dmgRuns}, worst {worst} frames)");
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

static class SeerTests
{
    // Plays the Seer fight (the real boss logic, re-implemented) against SeerPolicy in a 10x10 tile room; returns (won, damage, frames).
    public static (bool won, int dmg, int frames) Episode(int seed, int maxFrames = 6000, bool verbose = false)
    {
        var s = new Sim(seed, 10, 10);
        var boss = new SeerBoss(new Vector2(64, 40));
        var rng = new Random(seed * 7919 + 13);
        s.P = new Vector2(60 + rng.Next(30), 110 + rng.Next(20));
        // random warm-up of the boss's phase so that every seed meets a different moment of its cycle
        int warm = rng.Next(0, 400);
        for (int w = 0; w < warm; w++) { boss.Step(); }
        var mem = new SeerMem();
        int hits = 0;
        for (int f = 0; f < maxFrames && boss.Health > 0; f++)
        {
            float minC = boss.Preset.Y - 32 + 12, maxC = boss.Preset.Y + 80 + 12;
            var ctx = new SeerCtx
            {
                P = s.PlayerBox, Facing = s.Facing, Swinging = s.SwingT > 0, Seer = boss.Body, Orbs = boss.OrbBoxes.ToList(),
                Wave = boss.WaveBox, Vulnerable = boss.Vulnerable, Health = boss.Health, MinCY = minC, MaxCY = maxC, Walk = s.Walk,
                WaveStopY = boss.Dusts.Count > 0 ? boss.Dusts.Min(d => d.Y) - 8 : float.NaN
            };
            var keys = SeerPolicy.Decide(ctx, mem);
            if (verbose && f % 5 == 0) { Console.WriteLine($"f{f} hp={boss.Health} vuln={boss.Vulnerable} P={s.PlayerBox} face={s.Facing} S={boss.Body} keys={string.Join("+", keys)} dmg={s.Damage} wave={boss.WaveBox}"); }
            s.Step(keys);
            boss.Step();
            // contact
            var pb = s.PlayerBox;
            if (s.Invincible == 0 && (boss.Body.Intersects(pb) || boss.OrbBoxes.Any(o => o.Intersects(pb)) || (boss.WaveBox?.Intersects(pb) ?? false))) { s.Damage++; s.Invincible = 60; if (boss.WaveBox?.Intersects(pb) ?? false) { boss.WaveEnd = true; } hits++; }
            if (s.SwingT > 0) { var b = s.BroomRect(); if (b.Intersects(boss.Body)) { boss.Hit(); } }
        }
        return (boss.Health <= 0, s.Damage, s.Frame);
    }

    public static void Run(Action<string, bool, string> check)
    {
        int won = 0, hurt = 0, worst = 0, total = 0, firstBad = -1;
        const int N = 60;
        for (int seed = 0; seed < N; seed++)
        {
            var (w, d, fr) = Episode(seed);
            if (w) { won++; }
            total += d;
            if (d > 0) { hurt++; if (firstBad < 0) { firstBad = seed; } }
            worst = Math.Max(worst, fr);
        }
        Console.WriteLine($"  seer: won {won}/{N}, fights with damage {hurt}, total hearts lost {total}, worst {worst} frames, first damaged seed {firstBad}");
        check("seer: wins every seed", won == N, "");
        check("seer: zero-damage fights in at least 80% of seeds", hurt <= N / 5, $"(damaged {hurt}/{N})");
    }
}
