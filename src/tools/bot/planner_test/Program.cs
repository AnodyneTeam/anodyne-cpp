using AnodyneSharp.Recording;
using Microsoft.Xna.Framework;
using System;
using System.Collections.Generic;
using System.Diagnostics;

// Unit tests for the space-time planner on fake rooms (no game, no window): check the logic and the cost in milliseconds.
static class Program
{
    const int H = 90;
    static int fails;
    static float W = float.Parse(Environment.GetEnvironmentVariable("W") ?? "2.5");

    static void Check(string name, bool ok, string extra = "")
    {
        Console.WriteLine($"{(ok ? "PASS" : "FAIL")}  {name} {extra}");
        if (!ok) { fails++; }
    }

    // A room: walls as a set of solid 16px tiles.
    sealed class Room
    {
        public HashSet<(int, int)> Solid = new();
        public bool Walkable(Rectangle r)
        {
            foreach (var (x, y) in new[] { (r.Left, r.Top), (r.Right - 1, r.Top), (r.Left, r.Bottom - 1), (r.Right - 1, r.Bottom - 1) })
            {
                if (x < 0 || y < 0 || Solid.Contains((x / 16, y / 16))) { return false; }
            }
            return true;
        }
    }

    static StThreat Patroller(Rectangle start, float vx, float xMin, float xMax)
    {
        // Bounces between xMin and xMax at vx px/frame.
        var t = new StThreat { At = new Rectangle?[H + 1], Margin = 1 };
        float x = start.X, v = vx;
        for (int f = 0; f <= H; f++)
        {
            t.At[f] = new Rectangle((int)x, start.Y, start.Width, start.Height);
            x += v;
            if (x < xMin) { x = xMin; v = -v; }
            if (x > xMax) { x = xMax; v = -v; }
        }
        return t;
    }

    static StThreat Bullet(float x0, float y, float vx, int w, int h, int margin = 0)
    {
        var t = new StThreat { At = new Rectangle?[H + 1], Margin = margin };
        for (int f = 0; f <= H; f++) { t.At[f] = new Rectangle((int)(x0 + vx * f), (int)y, w, h); }
        return t;
    }

    static (int action, double ms, int nodes) Run(Rectangle start, Vector2 goal, List<StThreat> th, Room room)
    {
        var sw = Stopwatch.StartNew();
        int a = SpaceTime.Plan(start, goal, th, room.Walkable, H);
        sw.Stop();
        return (a, sw.Elapsed.TotalMilliseconds, SpaceTime.Nodes);
    }

    static void Fixture(string path)
    {
        var lines = System.IO.File.ReadAllLines(path);
        int i = 0;
        var pl = lines[i++].Split(' '); var player = new Rectangle(int.Parse(pl[1]), int.Parse(pl[2]), int.Parse(pl[3]), int.Parse(pl[4]));
        var gl = lines[i++].Split(' '); var goal = new Vector2(float.Parse(gl[1]), float.Parse(gl[2]));
        int horizon = int.Parse(lines[i++].Split(' ')[1]);
        var threats = new List<(string name, StThreat t)>();
        while (lines[i].StartsWith("threat"))
        {
            var h = lines[i++].Split(' ');
            var t = new StThreat { At = new Rectangle?[horizon + 1], Margin = int.Parse(h[2]), Stationary = h[3] == "1" };
            for (int f = 0; f <= horizon; f++)
            {
                var l = lines[i++];
                if (l != "-") { var q = l.Split(' '); t.At[f] = new Rectangle(int.Parse(q[0]), int.Parse(q[1]), int.Parse(q[2]), int.Parse(q[3])); }
            }
            threats.Add((h[1], t));
        }
        var fh = lines[i++].Split(' ');
        int fx = int.Parse(fh[1]), fy = int.Parse(fh[2]), fw = int.Parse(fh[3]), fhh = int.Parse(fh[4]);
        var room = new Room();
        for (int y = 0; y < fhh; y++) { for (int x = 0; x < fw; x++) { if (lines[i + y][x] == '#') { room.Solid.Add((fx + x, fy + y)); } } }
        Console.WriteLine($"--- {System.IO.Path.GetFileName(path)}: player {player} goal {goal}; threats: " + string.Join(", ", threats.ConvertAll(t => $"{t.name}(m{t.t.Margin})")));
        foreach (int m in new[] { -1, 0, 1, 2 })
        {
            var th = new List<StThreat>();
            foreach (var (name, t) in threats) { th.Add(new StThreat { At = t.At, Margin = m < 0 ? t.Margin : m, Stationary = t.Stationary }); }
            var sw = Stopwatch.StartNew();
            int a = SpaceTime.Plan(player, goal, th, room.Walkable, horizon, maxNodes: 20000, weight: W);
            Console.WriteLine($"  margin {(m < 0 ? "as recorded" : m.ToString())}: action {a} ({sw.Elapsed.TotalMilliseconds:0.0} ms, {SpaceTime.Nodes} nodes)");
        }
        // Is the player already inside a danger (so nothing can save them)?
        for (int f = 1; f <= 6; f++)
        {
            foreach (var (name, t) in threats)
            {
                if (t.At[f] is Rectangle r) { var rr = r; rr.Inflate(t.Margin, t.Margin); if (rr.Intersects(player)) { Console.WriteLine($"  frame {f}: standing still already hit by {name} {rr}"); } }
            }
        }
    }

    static void Main(string[] args)
    {
        if (args.Length > 1 && args[0] == "seed") { PolicyTests.VerboseSeed = int.Parse(args[1]); args = new string[0]; }
        if (args.Length > 1 && args[0] == "dbgv") { Debug.Vertical(int.Parse(args[1])); return; }
        if (args.Length > 0) { foreach (var a in args) { Fixture(a); } return; }
        var player = new Rectangle(64, 100, 10, 12);

        {   // 1. open floor, nothing around
            var room = new Room();
            var (a, ms, n) = Run(player, new Vector2(120, 100), new List<StThreat>(), room);
            Check("open floor: goal reachable", a >= 0, $"({ms:0.0} ms, {n} nodes)");
            Check("open floor: cheap", ms < 20 && n < 200);
        }
        {   // 2. beam of slow bullets (40px/s, 20px apart, 8 wide x 16 tall) across a room: cross it from below
            var room = new Room();
            var th = new List<StThreat>();
            for (int k = -4; k < 12; k++) { th.Add(Bullet(k * 20 + 5, 70, 40f / 60f, 8, 16)); }
            var p = new Rectangle(60, 100, 10, 12);
            var (a, ms, n) = Run(p, new Vector2(60, 40), th, room);
            Check("slow beam: a way across exists (diagonal ride)", a >= 0, $"({ms:0.0} ms, {n} nodes)");
            Check("slow beam: cheap", ms < 50, $"({ms:0.0} ms)");
        }
        {   // 3. one-tile corridor with a patroller, entering from the side exit: wait or time the dash
            var room = new Room();
            for (int x = 0; x < 12; x++) { room.Solid.Add((x, 5)); room.Solid.Add((x, 7)); } // corridor row 6 (y 96..112), walls above/below
            room.Solid.Remove((3, 7)); room.Solid.Remove((4, 7));                             // exit below at cols 3,4
            room.Solid.Remove((6, 5));                                                        // nook above at col 6
            var pat = Patroller(new Rectangle(9 * 16, 98, 10, 10), 0.5f, 3 * 16, 11 * 16 - 10);
            var p = new Rectangle(3 * 16 + 3, 7 * 16 + 2, 10, 12);
            var (a, ms, n) = Run(p, new Vector2(6 * 16 + 3, 5 * 16 + 2), new List<StThreat> { pat }, room);
            Check("corridor patrol: plan to the nook exists", a >= 0, $"(action {a}, {ms:0.0} ms, {n} nodes)");
        }
        {   // 4. hopeless: a wall of bullets everywhere -> must give up quickly, not burn the frame
            var room = new Room();
            var th = new List<StThreat>();
            var all = new StThreat { At = new Rectangle?[H + 1], Margin = 2 };
            for (int f = 0; f <= H; f++) { all.At[f] = new Rectangle(0, 0, 400, 400); }
            th.Add(all);
            var (a, ms, n) = Run(player, new Vector2(200, 100), th, room);
            Check("hopeless: returns none", a < 0);
            Check("hopeless: fast", ms < 20, $"({ms:0.0} ms, {n} nodes)");
        }
        {   // 5. goal unreachable (walled in) but safe: must not explode in cost
            var room = new Room();
            for (int x = 0; x < 20; x++) { for (int y = 0; y < 20; y++) { if (x > 8) { room.Solid.Add((x, y)); } } }
            var th = new List<StThreat> { Bullet(0, 90, 0.7f, 8, 16) };
            var (a, ms, n) = Run(player, new Vector2(250, 100), th, room);
            Check("unreachable goal: bounded cost", ms < 100 && n <= 2500, $"({ms:0.0} ms, {n} nodes, action {a})");
        }
        fails += PolicyTests.Run((n, ok, x) => Check(n, ok, x));
        Console.WriteLine(fails == 0 ? "ALL PASS" : $"{fails} FAILED");
        Environment.Exit(fails == 0 ? 0 : 1);
    }
}
