using AnodyneSharp.Recording;
using Microsoft.Xna.Framework;
using System;
using System.Collections.Generic;

/// <summary>A tiny re-implementation of the combat rules (broom reach, Shieldy shield, slime wander) for testing fight policies in milliseconds.</summary>
sealed class Sim
{
    public readonly HashSet<(int, int)> Solid = new();
    public Vector2 P;                 // player hitbox top-left
    public PFacing Facing = PFacing.Down;
    public int SwingT;                // frames of the current swing left (0 = none)
    public int SwingLen = 22;
    public PFacing SwingFacing;
    public int Damage, Invincible;
    public int Frame;
    public Random Rng;

    public enum Kind { Shieldy, Slime }
    public sealed class Enemy
    {
        public Kind K;
        public Vector2 Pos;
        public Vector2 V;       // px/s
        public int W, H, Health = 2, HitTimer, HurtT;
        public bool Dead;
        public Rectangle Box => new((int)Pos.X, (int)Pos.Y, W, H);
    }
    public readonly List<Enemy> Enemies = new();

    public Sim(int seed, int tilesW, int tilesH)
    {
        Rng = new Random(seed);
        for (int x = 0; x < tilesW; x++) { Solid.Add((x, 0)); Solid.Add((x, tilesH - 1)); }
        for (int y = 0; y < tilesH; y++) { Solid.Add((0, y)); Solid.Add((tilesW - 1, y)); }
    }

    public bool Walk(Rectangle r)
    {
        foreach (var (x, y) in new[] { (r.Left, r.Top), (r.Right - 1, r.Top), (r.Left, r.Bottom - 1), (r.Right - 1, r.Bottom - 1) })
        {
            if (x < 0 || y < 0 || Solid.Contains((x / 16, y / 16))) { return false; }
        }
        return true;
    }

    public Rectangle PlayerBox => new((int)P.X, (int)P.Y, 10, 12);

    Rectangle Broom()
    {
        int fr = Math.Min(4, SwingElapsed() * 14 / 60);
        float px = P.X, py = P.Y;
        switch (SwingFacing)
        {
            case PFacing.Down: return new Rectangle((int)(px - 11), (int)(py + 16 + (fr == 0 ? -8 : fr == 1 ? -5 : fr == 2 ? -2 : 0)), 24, 12);
            case PFacing.Up: return new Rectangle((int)(px - 5), (int)(py - 17 + (fr == 0 ? 12 : fr == 1 ? 6 : fr == 2 ? 2 : 0)), 24, 12);
            case PFacing.Left: return new Rectangle((int)(px - 13 + (fr == 0 ? 10 : fr == 1 ? 6 : fr == 2 ? -1 : 0)), (int)(py - 5), 12, 24);
            default: return new Rectangle((int)(px + 13 + (fr == 0 ? -12 : fr == 1 ? -8 : fr == 2 ? -1 : 0)), (int)(py - 5), 12, 24);
        }
    }

    int SwingElapsed() => SwingLen - SwingT;

    static Vector2 Dir(PFacing f) => f switch { PFacing.Up => new Vector2(0, -1), PFacing.Down => new Vector2(0, 1), PFacing.Left => new Vector2(-1, 0), _ => new Vector2(1, 0) };

    public void Step(PKey[] keys)
    {
        Frame++;
        if (Invincible > 0) { Invincible--; }
        // player
        var facingBefore = Facing;
        float vx = 0, vy = 0;
        int dirs = 0;
        foreach (var k in keys)
        {
            if (k == PKey.Up) { vy -= 1; Facing = PFacing.Up; dirs++; }
            else if (k == PKey.Down) { vy += 1; Facing = PFacing.Down; dirs++; }
            else if (k == PKey.Left) { vx -= 1; Facing = PFacing.Left; dirs++; }
            else if (k == PKey.Right) { vx += 1; Facing = PFacing.Right; dirs++; }
        }
        bool attack = Array.IndexOf(keys, PKey.Accept) >= 0;
        if (SwingT == 0 && attack) { SwingT = SwingLen; SwingFacing = facingBefore; } // the swing goes the way we faced BEFORE this frame's direction key
        float sp = (vx != 0 && vy != 0) ? 70f / 60f * 0.7f : 70f / 60f;
        if (SwingT > 0) { vx = 0; vy = 0; } // the player does not walk while swinging
        var np = P + new Vector2(vx * sp, 0);
        if (Walk(new Rectangle((int)np.X, (int)np.Y, 10, 12))) { P = np; }
        np = P + new Vector2(0, vy * sp);
        if (Walk(new Rectangle((int)np.X, (int)np.Y, 10, 12))) { P = np; }
        // enemies
        foreach (var e in Enemies)
        {
            if (e.Dead) { continue; }
            if (e.HitTimer > 0) { e.HitTimer--; }
            if (e.K == Kind.Shieldy)
            {
                var n = e.Pos + e.V / 60f;
                if (e.V != Vector2.Zero)
                {
                    if (Walk(new Rectangle((int)n.X, (int)n.Y, e.W, e.H))) { e.Pos = n; } else { e.V = -e.V; }
                }
            }
            else
            {
                if (e.HurtT > 0) { e.HurtT--; }
                else if (Frame % 30 == 0)
                {
                    bool standFrame = (Frame / 20) % 2 == 1;
                    e.V = standFrame ? Vector2.Zero : new Vector2((float)Rng.NextDouble() - 0.5f, (float)Rng.NextDouble() - 0.5f) * 20f;
                }
                var n = e.Pos + e.V / 60f;
                if (Walk(new Rectangle((int)n.X, (int)n.Y, e.W, e.H))) { e.Pos = n; } else { e.V = Vector2.Zero; }
            }
            // contact
            if (Invincible == 0 && e.Box.Intersects(PlayerBox)) { Damage++; Invincible = 60; }
            // broom
            if (SwingT > 0)
            {
                var b = Broom();
                if (b.Intersects(e.Box))
                {
                    if (e.K == Kind.Shieldy && e.HitTimer == 0)
                    {
                        e.HitTimer = 24;
                        if (SwingFacing == PFacing.Down) { e.Health--; }
                        else if (SwingFacing != PFacing.Down)
                        {
                            var np2 = e.Pos + Dir(SwingFacing) * 8;
                            if (Walk(new Rectangle((int)np2.X, (int)np2.Y, e.W, e.H))) { e.Pos = np2; }
                        }
                    }
                    else if (e.K == Kind.Slime && e.HitTimer == 0)
                    {
                        e.HitTimer = 16; e.Health--; e.V = Dir(SwingFacing) * 100; e.HurtT = 16;
                    }
                }
            }
            if (e.Health <= 0) { e.Dead = true; }
        }
        if (SwingT > 0) { SwingT--; }
    }

    public bool AllDead() { foreach (var e in Enemies) { if (!e.Dead) { return false; } } return true; }
}
