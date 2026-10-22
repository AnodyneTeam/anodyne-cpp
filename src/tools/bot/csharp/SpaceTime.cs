using Microsoft.Xna.Framework;
using System;
using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>One predicted danger: where it will be on each frame from now (null = not a danger then), plus a safety margin.</summary>
    public sealed class StThreat
    {
        public Rectangle?[] At;
        public int Margin = 2;
        public bool Stationary;
        public bool HurtsInAir; // (most touch dangers skip a player in the air)
    }

    /// <summary>
    /// Pure space-time planner (no game state, so it can be unit-tested on a fake room): finds the next frame's keys of a plan that
    /// reaches a goal without touching a predicted danger. Moves in short blocks of 8 directions or standing still.
    /// </summary>
    public static class SpaceTime
    {
        public const float Walk = 70f / 60f;

        /// <summary>The 9 actions: stand, 4 axis moves, 4 diagonals (a diagonal moves 0.7x on each axis).</summary>
        public static readonly (int dx, int dy)[] Actions =
        {
            (0, 0), (0, -1), (0, 1), (-1, 0), (1, 0), (1, -1), (-1, -1), (1, 1), (-1, 1),
        };

        /// <summary>Jumps (with the boots), as actions 9..13: on the spot, up, down, left, right. A jump is airFrames long, drifts 0.83x the walk.</summary>
        public static readonly (int dx, int dy)[] Jumps = { (0, 0), (0, -1), (0, 1), (-1, 0), (1, 0), (0, -1), (0, 1), (-1, 0), (1, 0) };

        /// <summary>Frames the way key is held after take-off, per jump (the short ones let go halfway: a shorter hop).</summary>
        public static readonly int[] JumpHold = { 0, 99, 99, 99, 99, 14, 14, 14, 14 };

        public static int Nodes; // nodes expanded by the last Plan (for performance tests)
        public static int CallsThisFrame, NodesThisFrame; // totals since the bot's frame began (the per-frame time budget watchdog reports them)
        public static bool Reached; // did the last Plan reach the goal (true) or only find a plan that stays alive (false)?

        /// <returns>The index into Actions of the first block of a surviving plan, or -1 if none survives the horizon.</returns>
        public static int Plan(Rectangle start, Vector2 goal, IReadOnlyList<StThreat> threats, Func<Rectangle, bool> walkable, int horizon,
            float tol = 3f, int block = 3, int maxNodes = 2500, float weight = 2.5f, bool survive = false, Func<Rectangle, bool> airOk = null, int airFrames = 28)
        {
            // Starting on something we cannot walk onto (a dust pile shoved under us, the edge of a pool): every step "hits" it, and we
            // would stand still in whatever comes. The first few frames may cross what we already stand on (the game lets us walk off it).
            bool stuckIn = !walkable(start);
            var xs = new List<float> { start.X };
            var ys = new List<float> { start.Y };
            var ts = new List<int> { 0 };
            var first = new List<int> { -1 };
            var open = new PriorityQueue<int, float>();
            var seen = new HashSet<(int, int, int)>();
            float Hh(float x, float y) => survive ? 1000f : (goal - new Vector2(x, y)).Length() / Walk; // survive: no goal, any plan that lasts the horizon will do
            open.Enqueue(0, weight * Hh(start.X, start.Y));
            int bestLate = -1;
            int deepest = 0; // fallback: the survivor that lasted longest (ties: nearest the goal)
            Nodes = 0;
            CallsThisFrame++;
            Reached = false;
            while (open.TryDequeue(out int idx, out _) && xs.Count < maxNodes)
            {
                Nodes++;
                NodesThisFrame++;
                float nx = xs[idx], ny = ys[idx];
                int nt = ts[idx];
                if (nt > ts[deepest] || (nt == ts[deepest] && Hh(nx, ny) < Hh(xs[deepest], ys[deepest])))
                {
                    deepest = idx;
                }
                if (!survive && Hh(nx, ny) * Walk <= tol)
                {
                    Reached = true;
                    return first[idx];
                }
                if (nt + block > horizon)
                {
                    if (bestLate < 0 || nt > ts[bestLate])
                    {
                        bestLate = idx;
                    }
                    continue;
                }
                for (int a = 0; a < Actions.Length; a++)
                {
                    var (ax, ay) = Actions[a];
                    float sp = (ax != 0 && ay != 0) ? Walk * 0.7f : Walk;
                    float x = nx, y = ny;
                    bool ok = true;
                    for (int f = 1; f <= block && ok; f++)
                    {
                        x += ax * sp;
                        y += ay * sp;
                        var mine = new Rectangle((int)x, (int)y, start.Width, start.Height);
                        if ((ax != 0 || ay != 0) && !walkable(mine) && !(stuckIn && nt + f <= 8))
                        {
                            ok = false;
                            break;
                        }
                        foreach (var th in threats)
                        {
                            var r = th.At[Math.Min(nt + f, horizon)];
                            if (r == null)
                            {
                                continue;
                            }
                            var rr = r.Value;
                            rr.Inflate(th.Margin, th.Margin);
                            if (mine.Intersects(rr))
                            {
                                ok = false;
                                break;
                            }
                        }
                    }
                    if (!ok || !seen.Add(((int)(x / 2), (int)(y / 2), (nt + block) / block)))
                    {
                        continue;
                    }
                    xs.Add(x);
                    ys.Add(y);
                    ts.Add(nt + block);
                    first.Add(first[idx] < 0 ? a : first[idx]);
                    open.Enqueue(xs.Count - 1, nt + block + weight * Hh(x, y));
                }
                if (airOk != null && nt + airFrames <= horizon)
                {
                    // Jumps: in the air only what hurts there counts and pits pass under; come down on floor, clear of everything.
                    for (int j = 0; j < Jumps.Length; j++)
                    {
                        var (jx, jy) = Jumps[j];
                        float x = nx, y = ny;
                        bool ok = true;
                        for (int f = 1; f <= airFrames && ok; f++)
                        {
                            if (f <= JumpHold[j])
                            {
                                x += jx * Walk * 0.83f;
                                y += jy * Walk * 0.83f;
                            }
                            var mine = new Rectangle((int)x, (int)y, start.Width, start.Height);
                            bool landing = f >= airFrames - 2;
                            if (!airOk(mine) || landing && !walkable(mine))
                            {
                                ok = false;
                                break;
                            }
                            foreach (var th in threats)
                            {
                                if (!landing && !th.HurtsInAir)
                                {
                                    continue;
                                }
                                var r = th.At[Math.Min(nt + f, horizon)];
                                if (r == null)
                                {
                                    continue;
                                }
                                var rr = r.Value;
                                rr.Inflate(th.Margin, th.Margin);
                                if (mine.Intersects(rr))
                                {
                                    ok = false;
                                    break;
                                }
                            }
                        }
                        if (!ok || !seen.Add(((int)(x / 2), (int)(y / 2), (nt + airFrames) / block)))
                        {
                            continue;
                        }
                        xs.Add(x);
                        ys.Add(y);
                        ts.Add(nt + airFrames);
                        first.Add(first[idx] < 0 ? Actions.Length + j : first[idx]);
                        open.Enqueue(xs.Count - 1, nt + airFrames + 6 + weight * Hh(x, y)); // (a little dearer than walking: a jump is a commitment)
                    }
                }
            }
            if (bestLate >= 0)
            {
                return first[bestLate];
            }
            if (ts[deepest] >= 2 * block)
            {
                return first[deepest]; // out of budget: follow the plan that survived the longest
            }
            // Nothing survives a block: we may already stand inside a danger's safety margin (pressed up against it), where every move
            // "hits" at once and we would freeze there. Then plan again on the bare boxes: any way out beats standing in it.
            bool squeezed = false;
            foreach (var th in threats)
            {
                for (int f = 0; f <= Math.Min(block, horizon) && !squeezed && th.Margin > 0; f++)
                {
                    if (th.At[f] is Rectangle r0)
                    {
                        r0.Inflate(th.Margin, th.Margin);
                        squeezed = r0.Intersects(start);
                    }
                }
            }
            if (squeezed)
            {
                var bare = new List<StThreat>();
                foreach (var th in threats)
                {
                    bare.Add(new StThreat { At = th.At, Margin = 0, Stationary = th.Stationary, HurtsInAir = th.HurtsInAir });
                }
                int calls = CallsThisFrame;
                int a = Plan(start, goal, bare, walkable, horizon, tol, block, maxNodes, weight, survive, airOk, airFrames);
                CallsThisFrame = calls + 1;
                return a;
            }
            return -1;
        }
    }
}
