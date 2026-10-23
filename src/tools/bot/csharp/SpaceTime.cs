using Microsoft.Xna.Framework;
using System;
using System.Collections.Generic;
using System.Linq;

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
        /// <param name="padAt">Dash pads: the way the pad under a box's centre sends a walker (0,0 for none). Walking on over one the way it
        /// points, that axis speeds up to 1.3x (1.77x from a second pad) for as long as the key stays held.</param>
        /// <param name="dash0">The dash we are already on (x and y speed factors, signed; 0 = none).</param>
        public static int Plan(Rectangle start, Vector2 goal, IReadOnlyList<StThreat> threats, Func<Rectangle, bool> walkable, int horizon,
            float tol = 3f, int block = 3, int maxNodes = 2500, float weight = 2.5f, bool survive = false, Func<Rectangle, bool> airOk = null, int airFrames = 28,
            Func<Rectangle, (int dx, int dy)> padAt = null, Vector2 dash0 = default)
        {
            // Starting on something we cannot walk onto (a dust pile shoved under us, the edge of a pool): every step "hits" it, and we
            // would stand still in whatever comes. The first few frames may cross what we already stand on (the game lets us walk off it).
            bool stuckIn = !walkable(start);
            var xs = new List<float> { start.X };
            var ys = new List<float> { start.Y };
            var ts = new List<int> { 0 };
            var first = new List<int> { -1 };
            var dxs = new List<float> { dash0.X };
            var dys = new List<float> { dash0.Y };
            // (the game's Player.Dash: a pad sets the axis to 1.3x if not dashing, to 1.77x if already, and cancels a dash the other way)
            static float PadDash(float d, int dir) => d == 0 ? dir * 1.3f : d * dir < 0 ? 0f : Math.Abs(d) < 1.5f ? dir * 1.77f : d;
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
                    float ddx = dxs[idx], ddy = dys[idx];
                    // (a dash lasts only while its key is held)
                    if (ddx * ax <= 0)
                    {
                        ddx = 0;
                    }
                    if (ddy * ay <= 0)
                    {
                        ddy = 0;
                    }
                    bool ok = true;
                    for (int f = 1; f <= block && ok; f++)
                    {
                        var before = padAt == null ? (0, 0) : padAt(new Rectangle((int)x, (int)y, start.Width, start.Height));
                        x += ddx != 0 ? ddx * Walk : ax * sp;
                        y += ddy != 0 ? ddy * Walk : ay * sp;
                        var mine = new Rectangle((int)x, (int)y, start.Width, start.Height);
                        if (padAt != null && (ax != 0 || ay != 0))
                        {
                            var (pdx, pdy) = padAt(mine);
                            if ((pdx != 0 || pdy != 0) && (pdx, pdy) != before)
                            {
                                // (not holding its way, its dash is let go at once, and it ends one we had the other way)
                                if (pdx != 0)
                                {
                                    ddx = ax == pdx ? PadDash(ddx, pdx) : 0;
                                }
                                else
                                {
                                    ddy = ay == pdy ? PadDash(ddy, pdy) : 0;
                                }
                            }
                        }
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
                    if (!ok || !seen.Add(((int)(x / 2) * 8 + (ddx == 0 ? 0 : 1) * 2 + (ddy == 0 ? 0 : 1), (int)(y / 2), (nt + block) / block)))
                    {
                        continue;
                    }
                    xs.Add(x);
                    ys.Add(y);
                    dxs.Add(ddx);
                    dys.Add(ddy);
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
                        // (a dash carries on through a jump the way it goes, for as long as its key stays held: it flies further)
                        float jsx = jx != 0 && dxs[idx] * jx > 0 ? Math.Abs(dxs[idx]) : 0.83f;
                        float jsy = jy != 0 && dys[idx] * jy > 0 ? Math.Abs(dys[idx]) : 0.83f;
                        bool ok = true;
                        for (int f = 1; f <= airFrames && ok; f++)
                        {
                            if (f <= JumpHold[j])
                            {
                                x += jx * Walk * jsx;
                                y += jy * Walk * jsy;
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
                        if (!ok || !seen.Add(((int)(x / 2) * 8, (int)(y / 2), (nt + airFrames) / block)))
                        {
                            continue;
                        }
                        xs.Add(x);
                        ys.Add(y);
                        dxs.Add(0f);
                        dys.Add(0f);
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
                int a = Plan(start, goal, bare, walkable, horizon, tol, block, maxNodes, weight, survive, airOk, airFrames, padAt, dash0);
                CallsThisFrame = calls + 1;
                return a;
            }
            return -1;
        }
    }
    /// <summary>A dash trap that charges on sight (the Hotel's and Apartments' OnSightDashTrap), as the planner simulates it.</summary>
    public sealed class SightTrap
    {
        public float X, Y, Vx, Vy; // hitbox top-left, px/s
        public int Mode; // 0 idle, 1 charging, 2 going home
        public float HomeX, HomeY;
        public int W = 14, H = 14;
        public bool Touching; // stopped by a wall or hole this frame: turns back on its next update

        public SightTrap Copy() => (SightTrap)MemberwiseClone();
    }

    /// <summary>
    /// Pure planner for a room of sight-triggered dash traps (the room's answer depends on where we go: a trap charges when we cross its
    /// row or column, 80px/s straight at us, bounces off the first wall, hole or other trap at half speed and goes home). The search
    /// carries the traps' whole state along each candidate path, so it can bait one out and slip past while it goes home, or be in the
    /// air (a trap only hurts on the ground) as one passes. Returns blocks of (action, frames): actions 0-8 as SpaceTime.Actions, 9-13
    /// a jump as SpaceTime.Jumps 0-4 (take-off, then the way held for the whole air time).
    /// </summary>
    public static class SightTrapPlanner
    {
        public const int AirFrames = 30;
        public static int Nodes;

        public sealed class Step
        {
            public int Action, Frames;
            public float X, Y; // where we should be when it is done
        }

        private sealed class Node
        {
            public float X, Y;
            public int T;
            public SightTrap[] Traps;
            public Node Parent;
            public int Action, Frames;
        }

        /// <summary>The pixels a box at (x,y) covers (a fraction past a pixel edge covers that pixel too).</summary>
        private static Rectangle Cover(float x, float y, int w, int h)
        {
            int l = (int)Math.Floor(x), t = (int)Math.Floor(y);
            return new Rectangle(l, t, (int)Math.Ceiling(x + w) - l, (int)Math.Ceiling(y + h) - t);
        }

        /// <summary>A walk (or air drift) of one frame, x then y, as the game resolves it: a way into a wall stops flush against it.</summary>
        private static void Move(ref float x, ref float y, float dx, float dy, int w, int h, Func<Rectangle, bool> can)
        {
            if (dx != 0)
            {
                float nx = x + dx;
                if (can(Cover(nx, y, w, h)))
                {
                    x = nx;
                }
                else
                {
                    float flush = dx > 0 ? (float)Math.Floor(nx + w) - w : (float)Math.Ceiling(nx);
                    if ((dx > 0 ? flush > x : flush < x) && can(Cover(flush, y, w, h)))
                    {
                        x = flush;
                    }
                }
            }
            if (dy != 0)
            {
                float ny = y + dy;
                if (can(Cover(x, ny, w, h)))
                {
                    y = ny;
                }
                else
                {
                    float flush = dy > 0 ? (float)Math.Floor(ny + h) - h : (float)Math.Ceiling(ny);
                    if ((dy > 0 ? flush > y : flush < y) && can(Cover(x, flush, w, h)))
                    {
                        y = flush;
                    }
                }
            }
        }

        private static bool Overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) =>
            ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;

        private static void Bounce(SightTrap t)
        {
            if (t.Mode == 1)
            {
                t.Vx = -t.Vx / 2;
                t.Vy = -t.Vy / 2;
                t.Mode = 2;
            }
        }

        /// <summary>
        /// One frame of every trap, in the game's order: each looks at where we were (it updates before we move), then moves; then the
        /// frame's collisions (a wall or hole it runs into, or only reaches on its right or bottom edge, turns it back on its next
        /// update; two traps that meet both turn back at once, and one that meets us on the ground hurts). Boxes are whole pixels,
        /// cut down from the positions, as the game's hitboxes are. True if one touches us on the ground.
        /// </summary>
        private static bool StepTraps(SightTrap[] ts, float prevX, float prevY, float pxf, float pyf, int pw, int ph, bool air, Func<Rectangle, bool> trapFree, int margin)
        {
            int qx = (int)prevX, qy = (int)prevY;
            foreach (var t in ts)
            {
                if (t.Touching)
                {
                    t.Touching = false;
                    Bounce(t);
                }
                if (t.Mode == 0)
                {
                    int tx = (int)t.X, ty = (int)t.Y;
                    bool seeX = qx <= tx + t.W && qx + pw >= tx;
                    bool seeY = qy <= ty + t.H && qy + ph >= ty;
                    if (seeX || seeY)
                    {
                        // (the game's FaceTowards: from its top-left to our centre, the longer axis wins)
                        float dx = prevX + pw / 2f - t.X, dy = prevY + ph / 2f - t.Y;
                        if (Math.Abs(dx) > Math.Abs(dy))
                        {
                            t.Vx = dx > 0 ? 80 : -80;
                            t.Vy = 0;
                        }
                        else
                        {
                            t.Vx = 0;
                            t.Vy = dy > 0 ? 80 : -80;
                        }
                        t.Mode = 1;
                    }
                }
                else if (t.Mode == 2)
                {
                    float hx = t.HomeX - t.X, hy = t.HomeY - t.Y;
                    if (hx * hx + hy * hy < 4)
                    {
                        t.X = t.HomeX;
                        t.Y = t.HomeY;
                        t.Vx = t.Vy = 0;
                        t.Mode = 0;
                    }
                }
                t.X += t.Vx / 60f;
                t.Y += t.Vy / 60f;
                if ((t.Vx != 0 || t.Vy != 0) && !trapFree(new Rectangle((int)t.X, (int)t.Y, t.W + 1, t.H + 1)))
                {
                    t.Touching = true; // (the game lets it into the wall a pixel and turns it back on its next update)
                }
            }
            for (int i = 0; i < ts.Length; i++)
            {
                for (int j = i + 1; j < ts.Length; j++)
                {
                    var a = ts[i];
                    var b = ts[j];
                    if (Overlap((int)a.X, (int)a.Y, a.W, a.H, (int)b.X, (int)b.Y, b.W, b.H))
                    {
                        Bounce(a);
                        Bounce(b);
                    }
                }
            }
            int px = (int)pxf, py = (int)pyf;
            bool hit = false;
            foreach (var t in ts)
            {
                if (!air && Overlap(px, py, pw, ph, (int)t.X - margin, (int)t.Y - margin, t.W + 2 * margin, t.H + 2 * margin))
                {
                    hit = true;
                }
            }
            return hit;
        }

        /// <summary>Debug: the frame-by-frame states a plan predicts (our top-left, then each trap's), for comparing with the game.</summary>
        public static List<string> Replay(Rectangle start, Vector2 startPos, SightTrap[] traps, List<Step> plan, Func<Rectangle, bool> walkable, Func<Rectangle, bool> airOk,
            Func<Rectangle, bool> trapFree)
        {
            var outp = new List<string>();
            float x = startPos.X, y = startPos.Y;
            var tr = traps.Select(t => t.Copy()).ToArray();
            foreach (var st in plan)
            {
                bool jump = st.Action >= SpaceTime.Actions.Length;
                var (ax, ay) = jump ? SpaceTime.Jumps[st.Action - SpaceTime.Actions.Length] : SpaceTime.Actions[st.Action];
                float sp = jump ? SpaceTime.Walk * 0.83f : (ax != 0 && ay != 0) ? SpaceTime.Walk * 0.7f : SpaceTime.Walk;
                for (int f = 1; f <= st.Frames; f++)
                {
                    float px0 = x, py0 = y;
                    bool air = jump && f < st.Frames;
                    Move(ref x, ref y, ax * sp, ay * sp, start.Width, start.Height, air ? airOk : walkable);
                    bool hit = StepTraps(tr, px0, py0, x, y, start.Width, start.Height, air, trapFree, 0);
                    outp.Add($"a{st.Action} me {x:0.0},{y:0.0}{(air ? " air" : "")}{(hit ? " HIT" : "")} | " + string.Join(" ", tr.Select(t => $"{t.X:0.0},{t.Y:0.0}/m{t.Mode}")));
                }
            }
            return outp;
        }

        public static List<Step> Plan(Rectangle start, Vector2 startPos, Vector2 goal, SightTrap[] traps, Func<Rectangle, bool> walkable, Func<Rectangle, bool> airOk,
            Func<Rectangle, bool> trapFree, bool canJump, int block = 4, int maxNodes = 40000, float tol = 2.5f, int margin = 1, int maxT = 1800,
            Func<float, float, float> framesToGoal = null)
        {
            int pw = start.Width, ph = start.Height;
            var open = new PriorityQueue<Node, float>();
            var seen = new HashSet<long>();
            // (how far the goal is, in frames: the caller's estimate round the walls when it has one, else straight there)
            float H(float x, float y) => (goal - new Vector2(x, y)).Length() <= tol ? 0f
                : Math.Max((goal - new Vector2(x, y)).Length() / SpaceTime.Walk, framesToGoal?.Invoke(x, y) ?? 0f);
            open.Enqueue(new Node { X = startPos.X, Y = startPos.Y, Traps = traps.Select(t => t.Copy()).ToArray(), Action = -1 }, 1.5f * H(start.X, start.Y));
            Nodes = 0;
            while (open.TryDequeue(out var n, out _) && Nodes < maxNodes)
            {
                Nodes++;
                if ((goal - new Vector2(n.X, n.Y)).Length() <= tol)
                {
                    var steps = new List<Step>();
                    for (var c = n; c.Parent != null; c = c.Parent)
                    {
                        steps.Add(new Step { Action = c.Action, Frames = c.Frames, X = c.X, Y = c.Y });
                    }
                    steps.Reverse();
                    return steps;
                }
                if (n.T > maxT)
                {
                    continue;
                }
                int nActions = SpaceTime.Actions.Length + (canJump ? 5 : 0);
                for (int a = 0; a < nActions; a++)
                {
                    bool jump = a >= SpaceTime.Actions.Length;
                    var (ax, ay) = jump ? SpaceTime.Jumps[a - SpaceTime.Actions.Length] : SpaceTime.Actions[a];
                    float sp = jump ? SpaceTime.Walk * 0.83f : (ax != 0 && ay != 0) ? SpaceTime.Walk * 0.7f : SpaceTime.Walk;
                    int frames = jump ? AirFrames : block;
                    float x = n.X, y = n.Y;
                    var tr = n.Traps.Select(t => t.Copy()).ToArray();
                    bool ok = true;
                    for (int f = 1; f <= frames && ok; f++)
                    {
                        float px0 = x, py0 = y;
                        bool air = jump && f < frames;
                        Move(ref x, ref y, ax * sp, ay * sp, pw, ph, air ? airOk : walkable);
                        if (!air && (ax != 0 || ay != 0) && x == px0 && y == py0)
                        {
                            ok = false; // (walking into a wall: the same as standing, which is its own action)
                            break;
                        }
                        if (StepTraps(tr, px0, py0, x, y, pw, ph, air, trapFree, margin))
                        {
                            ok = false;
                        }
                    }
                    if (!ok || (jump && !walkable(Cover(x, y, pw, ph))))
                    {
                        continue;
                    }
                    long key = ((long)(int)(x / 2) << 48) ^ ((long)(int)(y / 2) << 36);
                    foreach (var t in tr)
                    {
                        key = key * 1000003 + (t.Mode * 7919 + (int)(t.X / 4) * 131 + (int)(t.Y / 4));
                    }
                    if (!seen.Add(key))
                    {
                        continue;
                    }
                    var m = new Node { X = x, Y = y, T = n.T + frames, Traps = tr, Parent = n, Action = a, Frames = frames };
                    open.Enqueue(m, m.T + 1.5f * H(x, y));
                }
            }
            return null;
        }
    }
}
