using Microsoft.Xna.Framework;
using System;
using System.Collections.Generic;
using System.Linq;

namespace AnodyneSharp.Recording
{
    /// <summary>What the Seer fight policy sees each frame (plain data, so it can be unit-tested against a simulated Seer).</summary>
    public sealed class SeerCtx
    {
        public Rectangle P;
        public PFacing Facing;
        public bool Swinging;
        public Rectangle Seer;
        public List<Rectangle> Orbs = new();
        public Rectangle? Wave;
        public bool Vulnerable;
        public int Health;                 // boss hp (7 at the start; <= 6 is the second stage)
        public float MinCY = -1e6f, MaxCY = 1e6f; // limits of the boss's dive (centre Y), stage 2
        public Func<Rectangle, bool> Walk = _ => true;
        public float WaveStopY = float.NaN;   // the wave ends when it reaches this Y (the first dust it touches), NaN if none
    }

    public sealed class SeerMem
    {
        public Vector2? PrevSeer;
        public Vector2 SeerVel;
        public List<Vector2> PrevOrbs = new();
        public List<float> OrbOmega = new();
        public Vector2? PrevWave;
        public Vector2 WaveVel;
        public int Frame, LastSwing = -100;
    }

    /// <summary>
    /// The Seer fight: learn the boss's, its orbs' and its wave's motion from the last frames, predict them (the boss bouncing between its
    /// dive limits, the orbs circling it), plan a path through space and time to a spot from which the broom reaches it, strike only when
    /// standing still for the whole swing is safe, and otherwise keep clear.
    /// </summary>
    public static class SeerPolicy
    {
        public const int Horizon = 60;

        private static Vector2 Predict(Vector2 c, Vector2 v, int f, float minY, float maxY)
        {
            float x = c.X + v.X * f;
            float y = c.Y, vy = v.Y;
            for (int k = 0; k < f; k++)
            {
                y += vy;
                if (y > maxY) { y = 2 * maxY - y; vy = -2.0f; }       // it dives at 90px/s and climbs back at 120px/s
                else if (y < minY) { y = 2 * minY - y; vy = 1.5f; }
            }
            return new Vector2(x, y);
        }

        private static Rectangle Around(Vector2 c, int w, int h) => new((int)(c.X - w / 2f), (int)(c.Y - h / 2f), w, h);

        public static PKey[] Decide(SeerCtx c, SeerMem m)
        {
            m.Frame++;
            var sc = new Vector2(c.Seer.Center.ToXna().X, c.Seer.Center.ToXna().Y);
            if (m.PrevSeer.HasValue)
            {
                m.SeerVel = m.SeerVel * 0.6f + (sc - m.PrevSeer.Value) * 0.4f;
            }
            m.PrevSeer = sc;
            // orbs: angular speed round the boss, from the change of their angle since the last frame (matched by index)
            var orbC = c.Orbs.Select(o => new Vector2(o.X, o.Y)).ToList(); // top-left corners: they circle the boss centre at radius 16/32 (8 once it is angry)
            while (m.OrbOmega.Count < orbC.Count) { m.OrbOmega.Add(0f); }
            for (int i = 0; i < orbC.Count && i < m.PrevOrbs.Count; i++)
            {
                var rel = orbC[i] - sc;
                var pr = m.PrevOrbs[i] - (m.PrevSeer.Value - m.SeerVel); // previous orb relative to the previous boss position (approx.)
                if (rel.Length() > 1 && pr.Length() > 1)
                {
                    float da = MathF.Atan2(rel.Y, rel.X) - MathF.Atan2(pr.Y, pr.X);
                    while (da > MathF.PI) { da -= 2 * MathF.PI; }
                    while (da < -MathF.PI) { da += 2 * MathF.PI; }
                    m.OrbOmega[i] = m.OrbOmega[i] * 0.7f + da * 0.3f;
                }
            }
            m.PrevOrbs = orbC;
            Vector2 wv = Vector2.Zero;
            Vector2? wc = c.Wave.HasValue ? new Vector2(c.Wave.Value.Center.ToXna().X, c.Wave.Value.Center.ToXna().Y) : null;
            if (wc.HasValue && m.PrevWave.HasValue)
            {
                m.WaveVel = wc.Value - m.PrevWave.Value;
            }
            m.PrevWave = wc;
            wv = m.WaveVel;
            bool stage2 = c.Health <= 6;
            // predicted danger rectangles for the next frames
            var threats = new List<StThreat>();
            var seerT = new StThreat { At = new Rectangle?[Horizon + 1], Margin = 6 };
            for (int f = 0; f <= Horizon; f++)
            {
                seerT.At[f] = Around(Predict(sc, m.SeerVel, f, c.MinCY, c.MaxCY), c.Seer.Width, c.Seer.Height);
            }
            threats.Add(seerT);
            for (int i = 0; i < orbC.Count; i++)
            {
                var th = new StThreat { At = new Rectangle?[Horizon + 1], Margin = 6 };
                var rel = orbC[i] - sc;
                float om = i < m.OrbOmega.Count ? m.OrbOmega[i] : 0f;
                for (int f = 0; f <= Horizon; f++)
                {
                    var bc = Predict(sc, m.SeerVel, f, c.MinCY, c.MaxCY);
                    if (stage2)
                    {
                        th.At[f] = new Rectangle((int)(bc.X - 8), (int)(bc.Y - 8), 32, 32); // whirling at 8px (corner-anchored 16px orbs): x,y from -8 to +24 round the boss centre
                    }
                    else
                    {
                        float ang = om * f;
                        var rot = new Vector2(rel.X * MathF.Cos(ang) - rel.Y * MathF.Sin(ang), rel.X * MathF.Sin(ang) + rel.Y * MathF.Cos(ang));
                        var tl = bc + rot;
                        th.At[f] = new Rectangle((int)tl.X, (int)tl.Y, c.Orbs[i].Width, c.Orbs[i].Height);
                    }
                }
                threats.Add(th);
            }
            if (c.Wave.HasValue)
            {
                var th = new StThreat { At = new Rectangle?[Horizon + 1], Margin = 4 };
                for (int f = 0; f <= Horizon; f++)
                {
                    var r = c.Wave.Value;
                    int dy = (int)(wv.Y * f);
                    if (!float.IsNaN(c.WaveStopY) && r.Y + dy > c.WaveStopY) { dy = (int)(c.WaveStopY - r.Y); } // it ends on the dust
                    r.Offset((int)(wv.X * f), dy);
                    th.At[f] = r;
                }
                threats.Add(th);
            }
            var pb = c.P;
            // A swing roots us for ~22 frames: strike only if standing here that long is safe and the broom meets the boss when it lands.
            bool StandingSafe(int frames)
            {
                for (int f = 1; f <= Math.Min(frames, Horizon); f++)
                {
                    foreach (var th in threats)
                    {
                        var r = th.At[f];
                        if (r == null) { continue; }
                        var rr = r.Value;
                        rr.Inflate(th.Margin, th.Margin);
                        if (pb.Intersects(rr)) { return false; }
                    }
                }
                return true;
            }
            Rectangle BroomRect(PFacing f) => f switch
            {
                PFacing.Up => new Rectangle(pb.X - 5, pb.Y - 17, 24, 12),
                PFacing.Down => new Rectangle(pb.X - 11, pb.Bottom + 4, 24, 12),
                PFacing.Left => new Rectangle(pb.X - 13, pb.Y - 5, 12, 24),
                _ => new Rectangle(pb.Right + 3, pb.Y - 5, 12, 24)
            };
            var seerAt8 = seerT.At[8] ?? c.Seer;
            if (c.Vulnerable && !c.Swinging && BroomRect(c.Facing).Intersects(seerAt8) && StandingSafe(24) && m.Frame - m.LastSwing > 26)
            {
                m.LastSwing = m.Frame;
                return new[] { PKey.Accept };
            }
            // where to be
            var cands = new List<(Vector2 pos, PFacing face)>
            {
                (new Vector2(seerAt8.Center.ToXna().X - 5, seerAt8.Bottom + 7), PFacing.Up),
                (new Vector2(seerAt8.Right + 7, seerAt8.Center.ToXna().Y - 6), PFacing.Left),
                (new Vector2(seerAt8.Left - 17, seerAt8.Center.ToXna().Y - 6), PFacing.Right),
                (new Vector2(seerAt8.Center.ToXna().X - 5, seerAt8.Top - 19), PFacing.Down),
            };
            if (stage2)
            {
                // its orbs whirl out to 16px right of and 12px below the body: only the left side and the top are safe to strike from
                cands = cands.Where(k => k.face == PFacing.Right || k.face == PFacing.Down).ToList();
            }
            cands = cands.OrderBy(k => (k.pos - new Vector2(pb.X, pb.Y)).LengthSquared()).ToList();
            Vector2 goal = new Vector2(pb.X, pb.Y);
            PFacing goalFace = PFacing.Up;
            if (c.Vulnerable)
            {
                foreach (var cand in cands)
                {
                    int a0 = SpaceTime.Plan(pb, cand.pos, threats, c.Walk, Horizon);
                    if (a0 >= 0 && SpaceTime.Reached)
                    {
                        goal = cand.pos;
                        goalFace = cand.face;
                        break;
                    }
                }
            }
            int a = SpaceTime.Plan(pb, goal, threats, c.Walk, Horizon);
            if (a < 0)
            {
                foreach (var th in threats) { th.Margin = 0; }
                a = SpaceTime.Plan(pb, goal, threats, c.Walk, Horizon);
            }
            if (a < 0)
            {
                return new PKey[0];
            }
            var (ax, ay) = SpaceTime.Actions[a];
            var keys = new List<PKey>();
            if (ax < 0) { keys.Add(PKey.Left); } else if (ax > 0) { keys.Add(PKey.Right); }
            if (ay < 0) { keys.Add(PKey.Up); } else if (ay > 0) { keys.Add(PKey.Down); }
            if (c.Vulnerable && keys.Count == 0 && c.Facing != goalFace && (new Vector2(pb.X, pb.Y) - goal).Length() < 6)
            {
                keys.Add(goalFace == PFacing.Up ? PKey.Up : goalFace == PFacing.Down ? PKey.Down : goalFace == PFacing.Left ? PKey.Left : PKey.Right);
            }
            return keys.ToArray();
        }
    }
}
