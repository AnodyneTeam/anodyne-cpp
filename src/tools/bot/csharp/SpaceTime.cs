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

        public static int Nodes; // nodes expanded by the last Plan (for performance tests)

        /// <returns>The index into Actions of the first block of a surviving plan, or -1 if none survives the horizon.</returns>
        public static int Plan(Rectangle start, Vector2 goal, IReadOnlyList<StThreat> threats, Func<Rectangle, bool> walkable, int horizon,
            float tol = 3f, int block = 3, int maxNodes = 2500, float weight = 2.5f)
        {
            var xs = new List<float> { start.X };
            var ys = new List<float> { start.Y };
            var ts = new List<int> { 0 };
            var first = new List<int> { -1 };
            var open = new PriorityQueue<int, float>();
            var seen = new HashSet<(int, int, int)>();
            float Hh(float x, float y) => (goal - new Vector2(x, y)).Length() / Walk;
            open.Enqueue(0, weight * Hh(start.X, start.Y));
            int bestLate = -1;
            int deepest = 0; // fallback: the survivor that lasted longest (ties: nearest the goal)
            Nodes = 0;
            while (open.TryDequeue(out int idx, out _) && xs.Count < maxNodes)
            {
                Nodes++;
                float nx = xs[idx], ny = ys[idx];
                int nt = ts[idx];
                if (nt > ts[deepest] || (nt == ts[deepest] && Hh(nx, ny) < Hh(xs[deepest], ys[deepest])))
                {
                    deepest = idx;
                }
                if (Hh(nx, ny) * Walk <= tol)
                {
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
                        if ((ax != 0 || ay != 0) && !walkable(mine))
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
            }
            if (bestLate >= 0)
            {
                return first[bestLate];
            }
            return ts[deepest] >= 2 * block ? first[deepest] : -1; // out of budget: follow the plan that survived the longest
        }
    }
}
