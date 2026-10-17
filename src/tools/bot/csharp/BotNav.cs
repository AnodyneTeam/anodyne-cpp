using AnodyneSharp.Entities;
using AnodyneSharp.MapData;
using AnodyneSharp.MapData.Tiles;
using AnodyneSharp.Registry;
using Microsoft.Xna.Framework;
using System;
using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>Tile-grid pathfinding over the current map's collision data (4-neighbour BFS, avoids holes/spikes/water).</summary>
    public static class BotNav
    {
        // Hitbox is 10x12; standing at a tile centre it is (x - 5, y - 6).
        public const float HalfW = 5f, HalfH = 6f;

        /// <summary>Tiles the bot has learned are blocked by something dynamic (gates, blocks, NPCs); cleared on map change.</summary>
        public static readonly BlockedSet Blocked = new();

        /// <summary>Learned obstacles; entries expire so a gate that was closed a while ago doesn't stay blocked after it opens.</summary>
        public sealed class BlockedSet
        {
            private readonly Dictionary<Point, long> _added = new();
            public const int LifetimeFrames = 300;

            public void Add(Point p)
            {
                // Gates are tracked from their entities (open/closed); never learn them as permanent obstacles.
                if (BotNav.IsGateTile(p))
                {
                    return;
                }
                _added[p] = BotRunner.Frame;
            }
            public void Clear() => _added.Clear();

            public bool Contains(Point p) => _added.TryGetValue(p, out long f) && BotRunner.Frame - f < LifetimeFrames;

            public IEnumerable<Point> Select_() => _added.Keys;
            public IEnumerable<R> Select<R>(Func<Point, R> f) => System.Linq.Enumerable.Select(_added.Keys, f);
        }
        private static string _blockedMap = "";

        public static Map CurrentMap => GlobalState.Map as Map;

        // Gates are entities laid over the tile data: an opened one is passable even if the tile beneath says wall,
        // a closed one blocks even if the tile says floor. Refreshed at the start of every search.
        private static HashSet<Point> _gatesOpen = new(), _gatesClosed = new();

        private static void RefreshGates()
        {
            _gatesOpen = new HashSet<Point>();
            _gatesClosed = new HashSet<Point>();
            foreach (var e in BotRunner.Entities())
            {
                if (!e.exists || !e.GetType().Name.EndsWith("Gate"))
                {
                    continue;
                }
                var t = new Point((int)(e.Position.X + 8) / 16, (int)(e.Position.Y + 8) / 16);
                (e.Solid ? _gatesClosed : _gatesOpen).Add(t);
            }
            // Screens that are not loaded: gates that are still shut are known from the map's entity presets.
            var grid = GlobalState.CurrentMapGrid;
            foreach (var pr in AnodyneSharp.Entities.EntityManager.GetMapEntities(GlobalState.CURRENT_MAP_NAME))
            {
                if (pr.Type == null || !pr.Type.Name.EndsWith("Gate") || !pr.Alive || ((int)pr.Position.X / 160 == grid.X && (int)pr.Position.Y / 160 == grid.Y))
                {
                    continue;
                }
                var t = new Point((int)(pr.Position.X + 8) / 16, (int)(pr.Position.Y + 8) / 16);
                _gatesClosed.Add(t);
                if (pr.Type.Name.StartsWith("Big"))
                {
                    _gatesClosed.Add(new Point(t.X + 1, t.Y));
                }
            }
        }

        /// <summary>BFS step counts over walkable tiles from `from` (stays inside `bounds` if given).</summary>
        public static Dictionary<Point, int> DistMap(Point from, bool allowWater, Rectangle? bounds = null, bool allowJump = false)
        {
            bool old = AllowWater, oldJ = AllowJump;
            AllowWater = allowWater;
            AllowJump = allowJump;
            RefreshGates();
            var haz = Hazards();
            var dist = new Dictionary<Point, int> { [from] = 0 };
            var q = new Queue<Point>();
            q.Enqueue(from);
            Point[] dirs = { new(1, 0), new(-1, 0), new(0, 1), new(0, -1) };
            while (q.Count > 0)
            {
                var c = q.Dequeue();
                foreach (var d in dirs)
                {
                    var n = new Point(c.X + d.X, c.Y + d.Y);
                    if (dist.ContainsKey(n) || (bounds.HasValue && !bounds.Value.Contains(n)) || haz.Contains(n) || !Walkable(n))
                    {
                        continue;
                    }
                    dist[n] = dist[c] + 1;
                    q.Enqueue(n);
                }
            }
            AllowWater = old;
            AllowJump = oldJ;
            return dist;
        }

        private static HashSet<(int, int)> _dustScreens;

        /// <summary>Screens (grid coords) that have loose dust to make a raft of, per the map's entity presets and what is loaded.</summary>
        public static bool ScreenHasDust(int sx, int sy)
        {
            if (_dustScreens == null)
            {
                _dustScreens = new HashSet<(int, int)>();
                foreach (var pr in AnodyneSharp.Entities.EntityManager.GetMapEntities(GlobalState.CURRENT_MAP_NAME))
                {
                    if (pr.Type != null && pr.Type.Name == "Dust")
                    {
                        _dustScreens.Add(((int)pr.Position.X / 160, (int)pr.Position.Y / 160));
                    }
                }
            }
            if (_dustScreens.Contains((sx, sy)))
            {
                return true;
            }
            // A raft left behind (or dust carried) on the screen we are on can be used again.
            var g = GlobalState.CurrentMapGrid;
            return g.X == sx && g.Y == sy && (BotRunner.Player.broom.dust != null || System.Linq.Enumerable.Any(BotRunner.Entities(), e => e.GetType().Name == "Dust"));
        }

        public static void ResetDustCache() => _dustScreens = null;

        /// <summary>
        /// Route that may cross water only on a raft: a raft can be made where the land tile you step off from lies on a screen with
        /// dust, and stays under you until you step back onto land. `wet` = already on a raft at `from`.
        /// </summary>
        public static List<Point> RaftPath(Point from, bool wet, Point goal, bool jump)
        {
            RefreshGates();
            var haz = Hazards();
            bool oldJ = AllowJump, oldW = AllowWater;
            AllowJump = jump;
            AllowWater = false;
            // State: tile, on a raft, and "the raft we just got off is lying here on this screen" (usable for the next crossing).
            var prev = new Dictionary<(Point, bool, bool), (Point, bool, bool)>();
            var q = new Queue<(Point, bool, bool)>();
            var start = (from, wet, false);
            prev[start] = start;
            q.Enqueue(start);
            Point[] dirs = { new(1, 0), new(-1, 0), new(0, 1), new(0, -1) };
            (Point, bool, bool)? found = null;
            while (q.Count > 0 && found == null)
            {
                var (c, w, carry) = q.Dequeue();
                foreach (var d in dirs)
                {
                    var n = new Point(c.X + d.X, c.Y + d.Y);
                    bool nw = IsConveyor(n) && !(jump && NarrowWater(n));
                    bool screenChanged = n.X / 10 != c.X / 10 || n.Y / 10 != c.Y / 10;
                    if (n != goal)
                    {
                        if (haz.Contains(n))
                        {
                            continue;
                        }
                        if (nw)
                        {
                            var (al, _) = CurrentMap.GetTileClass(n);
                            if (al != Touching.NONE || (!w && !carry && !ScreenHasDust(c.X / 10, c.Y / 10)))
                            {
                                continue;
                            }
                        }
                        else if (!Walkable(n))
                        {
                            continue;
                        }
                    }
                    bool ncarry = nw ? false : (w ? true : (carry && !screenChanged));
                    var key = (n, nw, ncarry);
                    if (prev.ContainsKey(key))
                    {
                        continue;
                    }
                    prev[key] = (c, w, carry);
                    if (n == goal)
                    {
                        found = key;
                        break;
                    }
                    q.Enqueue(key);
                }
            }
            AllowJump = oldJ;
            AllowWater = oldW;
            if (found == null)
            {
                return null;
            }
            var path = new List<Point>();
            var cur = found.Value;
            while (cur != start)
            {
                path.Add(cur.Item1);
                cur = prev[cur];
            }
            path.Reverse();
            return path;
        }

        /// <summary>Set while riding a dust raft: conveyor (river) tiles count as walkable then.</summary>
        public static bool AllowWater;

        /// <summary>Set once the boots are on: spike tiles count as walkable (they are jumped over).</summary>
        public static bool AllowJump;

        /// <summary>Water only one or two tiles across (in some straight line, with dry land on both sides): jumpable with the boots.</summary>
        public static bool NarrowWater(Point t)
        {
            bool Land(Point q)
            {
                var (al, ev) = CurrentMap.GetTileClass(q);
                return al == Touching.NONE && ev != CollisionEventType.CONVEYOR && ev != CollisionEventType.HOLE && ev != CollisionEventType.PUDDLE;
            }
            foreach (var d in new[] { new Point(1, 0), new Point(0, 1) })
            {
                int a = 0, b = 0;
                var p = new Point(t.X - d.X, t.Y - d.Y);
                while (a < 3 && IsConveyor(p)) { a++; p = new Point(p.X - d.X, p.Y - d.Y); }
                var landA = Land(p);
                p = new Point(t.X + d.X, t.Y + d.Y);
                while (b < 3 && IsConveyor(p)) { b++; p = new Point(p.X + d.X, p.Y + d.Y); }
                if (landA && Land(p) && a + b + 1 <= 2)
                {
                    return true;
                }
            }
            return false;
        }

        public static bool IsJumpable(Point t) => IsSpike(t) || (IsConveyor(t) && NarrowWater(t));

        public static bool IsConveyor(Point t) => CurrentMap.GetTileClass(t).Item2 == CollisionEventType.CONVEYOR;

        public static bool IsSpike(Point t) => CurrentMap.GetTileClass(t).Item2 == CollisionEventType.SPIKE;

        public static string DebugGates() => string.Join(" ", System.Linq.Enumerable.Select(_gatesClosed, t => $"{t.X},{t.Y}"));

        private static bool _respectClosedGates = true;

        public static bool IsGateTile(Point p) => _gatesOpen.Contains(p) || _gatesClosed.Contains(p);

        public static bool Walkable(Point t)
        {
            if (_gatesOpen.Contains(t))
            {
                return !Blocked.Contains(t);
            }
            if (_respectClosedGates && _gatesClosed.Contains(t))
            {
                return false;
            }
            var map = CurrentMap;
            if (map == null)
            {
                return false;
            }
            if (_blockedMap != GlobalState.CURRENT_MAP_NAME)
            {
                Blocked.Clear();
                _blockedMap = GlobalState.CURRENT_MAP_NAME;
            }
            if (Blocked.Contains(t))
            {
                return false;
            }
            var (allow, ev) = map.GetTileClass(t);
            if (allow == Touching.ANY)
            {
                return false;
            }
            switch (ev)
            {
                case CollisionEventType.HOLE:
                    return false;
                case CollisionEventType.PUDDLE:
                    return allow == Touching.NONE; // shallow water: slows you down but can be walked
                case CollisionEventType.SPIKE:
                    return AllowJump && allow == Touching.NONE;
                case CollisionEventType.CONVEYOR:
                    return allow == Touching.NONE && (AllowWater || (AllowJump && NarrowWater(t)));
            }
            return allow == Touching.NONE;
        }

        /// <summary>Entity-made floor hazards (cracked tiles that collapse, holes) the tile data doesn't show.</summary>
        private static bool _crossCracked;

        private static HashSet<Point> Hazards()
        {
            var set = new HashSet<Point>();
            foreach (var e in BotRunner.Entities())
            {
                if (!e.exists)
                {
                    continue;
                }
                string name = e.GetType().Name;
                if (name == "TreasureChest" || name == "Statue" || name == "Sign" || name.EndsWith("Quest") || (name == "CrackedTile" && !_crossCracked) || name == "Hole" || name == "Dust_Pit" || name.EndsWith("Door") || name.EndsWith("Portal"))
                {
                    set.Add(new Point((int)(e.Position.X + 8) / 16, (int)(e.Position.Y + 8) / 16));
                }
            }
            // Doors and portals of screens that are not loaded are known from the presets: never walk over them by accident.
            foreach (var pr in AnodyneSharp.Entities.EntityManager.GetMapEntities(GlobalState.CURRENT_MAP_NAME))
            {
                if (pr.Type != null && (pr.Type.Name.EndsWith("Door") || pr.Type.Name.EndsWith("Portal")) && pr.Type.Name != "NexusDoor")
                {
                    set.Add(new Point((int)(pr.Position.X + 8) / 16, (int)(pr.Position.Y + 8) / 16));
                    if (pr.Type.Name.StartsWith("Wide"))
                    {
                        for (int dx = -2; dx <= 2; dx++)
                        {
                            set.Add(new Point((int)(pr.Position.X + 8) / 16 + dx, (int)(pr.Position.Y + 8) / 16));
                        }
                    }
                }
            }
            return set;
        }

        /// <summary>Debug: the screens (160x160) containing at least one tile reachable on foot from a.</summary>
        public static SortedSet<(int, int)> ReachableScreens(Point a)
        {
            var seen = new HashSet<Point> { a };
            var q = new Queue<Point>();
            q.Enqueue(a);
            var hazards = Hazards();
            Point[] dirs = { new(1, 0), new(-1, 0), new(0, 1), new(0, -1) };
            var screens = new SortedSet<(int, int)>();
            while (q.Count > 0)
            {
                var c = q.Dequeue();
                screens.Add((c.X / 10, c.Y / 10));
                foreach (var d in dirs)
                {
                    var n = new Point(c.X + d.X, c.Y + d.Y);
                    if (seen.Contains(n) || hazards.Contains(n) || !Walkable(n))
                    {
                        continue;
                    }
                    seen.Add(n);
                    q.Enqueue(n);
                }
            }
            return screens;
        }

        public static string DebugRow(int ty, int x0, int x1)
        {
            RefreshGates();
            var sb = new System.Text.StringBuilder($"row {ty}: ");
            for (int x = x0; x <= x1; x++)
            {
                var p = new Point(x, ty);
                sb.Append(_gatesOpen.Contains(p) ? 'O' : _gatesClosed.Contains(p) ? 'C' : Walkable(p) ? '.' : '#');
            }
            return sb.ToString();
        }

        public static Point TileOf(Vector2 hitboxTopLeft) =>
            new((int)((hitboxTopLeft.X + HalfW) / 16), (int)((hitboxTopLeft.Y + HalfH) / 16));

        public static Vector2 CentreOf(Point t) => new(t.X * 16 + 8 - HalfW, t.Y * 16 + 8 - HalfH);

        public static List<Point> FindPath(Point a, Point b) => FindPath(a, t => t == b, b);

        /// <summary>Extra tiles the next searches must avoid (e.g. tiles near enemies); set by the caller, cleared with Avoid = null.</summary>
        public static HashSet<Point> Avoid;

        /// <summary>Tiles a PewLaser's beam sweeps: from the emitter in its facing direction until a wall or loose dust.</summary>
        public static HashSet<Point> LaneTiles() => new(LaneAxes().Keys);

        /// <summary>The beam tiles with the axis the beam travels along (0 = horizontal, 1 = vertical).</summary>
        public static Dictionary<Point, int> LaneAxes()
        {
            var set = new Dictionary<Point, int>();
            foreach (var e in BotRunner.Entities())
            {
                if (!e.exists || e.GetType().Name != "PewLaser")
                {
                    continue;
                }
                var d = e.facing switch
                {
                    Facing.RIGHT => new Point(1, 0),
                    Facing.LEFT => new Point(-1, 0),
                    Facing.UP => new Point(0, -1),
                    _ => new Point(0, 1)
                };
                var t = TileOf(e.Position + new Vector2(8, 8));
                for (int i = 1; i < 24; i++)
                {
                    t = new Point(t.X + d.X, t.Y + d.Y);
                    if (!Walkable(t) || System.Linq.Enumerable.Any(BotRunner.Entities(), x => x.exists && x.GetType().Name == "Dust" && TileOf(x.Position + new Vector2(8, 8)) == t))
                    {
                        break;
                    }
                    set[t] = d.X != 0 ? 0 : 1;
                }
            }
            return set;
        }

        /// <summary>Tiles within `margin` px of any living enemy except `except`.</summary>
        public static HashSet<Point> NearEnemies(int margin, Entity except = null)
        {
            var set = new HashSet<Point>();
            foreach (var e in BotRunner.Entities())
            {
                if (!e.exists || e == except || !e.GetType().IsDefined(typeof(AnodyneSharp.Entities.Enemy.EnemyAttribute), false))
                {
                    continue;
                }
                var r = e.Hitbox;
                r.Inflate(margin, margin);
                for (int ty = r.Top / 16; ty <= (r.Bottom - 1) / 16; ty++)
                {
                    for (int tx = r.Left / 16; tx <= (r.Right - 1) / 16; tx++)
                    {
                        set.Add(new Point(tx, ty));
                    }
                }
            }
            return set;
        }

        /// <summary>Shortest tile path from a to the nearest tile satisfying isGoal (excluding a), or null. `forced` is a goal tile allowed even if not walkable.</summary>
        public static List<Point> FindPath(Point a, Func<Point, bool> isGoal, Point? forced = null, bool strict = false)
        {
            // Prefer routes that respect closed gates; if there is none, assume they will open (or can be opened) on arrival
            // (unless `strict`).
            _respectClosedGates = true;
            var p = FindPathCore(a, isGoal, forced);
            if (p == null && !strict)
            {
                _respectClosedGates = false;
                p = FindPathCore(a, isGoal, forced);
                _respectClosedGates = true;
            }
            if (p == null && !strict)
            {
                // Cracked tiles only give way after a second of standing on them, so when they are the only way across, run over them.
                _crossCracked = true;
                p = FindPathCore(a, isGoal, forced);
                if (p == null)
                {
                    _respectClosedGates = false;
                    p = FindPathCore(a, isGoal, forced);
                    _respectClosedGates = true;
                }
                _crossCracked = false;
            }
            return p;
        }

        /// <summary>
        /// Dijkstra variant used on screens with laser beams: walking along a beam is costly (every bullet meets you), crossing
        /// it sideways is cheap, so the route crosses beams at right angles where it can.
        /// </summary>
        private static List<Point> FindPathWeighted(Point a, Func<Point, bool> isGoal, Point? forced, HashSet<Point> hazards, Dictionary<Point, int> lanes)
        {
            var best = new Dictionary<Point, int> { [a] = 0 };
            var prev = new Dictionary<Point, Point> { [a] = a };
            var pq = new PriorityQueue<Point, int>();
            pq.Enqueue(a, 0);
            Point[] dirs = { new(1, 0), new(-1, 0), new(0, 1), new(0, -1) };
            while (pq.TryDequeue(out var c, out int cost))
            {
                if (cost > best[c])
                {
                    continue;
                }
                if (c != a && isGoal(c))
                {
                    var path = new List<Point>();
                    for (var p = c; p != a; p = prev[p])
                    {
                        path.Add(p);
                    }
                    path.Reverse();
                    return path;
                }
                foreach (var d in dirs)
                {
                    var n = new Point(c.X + d.X, c.Y + d.Y);
                    if (n != forced && (hazards.Contains(n) || (Avoid != null && Avoid.Contains(n)) || !Walkable(n)))
                    {
                        continue;
                    }
                    int step = 1;
                    if (lanes.TryGetValue(n, out int axis) && ((axis == 0) == (d.Y == 0)))
                    {
                        step += 8; // moving along the beam
                    }
                    else if (lanes.ContainsKey(n))
                    {
                        step += 2; // crossing it
                    }
                    int nc = cost + step;
                    if (!best.TryGetValue(n, out int old) || nc < old)
                    {
                        best[n] = nc;
                        prev[n] = c;
                        pq.Enqueue(n, nc);
                    }
                }
            }
            return null;
        }

        private static List<Point> FindPathCore(Point a, Func<Point, bool> isGoal, Point? forced)
        {
            var map = CurrentMap;
            if (map == null)
            {
                return null;
            }
            if (isGoal(a))
            {
                return new List<Point>();
            }
            RefreshGates();
            var hazards = Hazards();
            var prev = new Dictionary<Point, Point> { [a] = a };
            var q = new Queue<Point>();
            q.Enqueue(a);
            Point[] dirs = { new(1, 0), new(-1, 0), new(0, 1), new(0, -1) };
            var lanes = LaneAxes();
            if (lanes.Count > 0 && Environment.GetEnvironmentVariable("ANODYNE_NO_WEIGHT") == null)
            {
                return FindPathWeighted(a, isGoal, forced, hazards, lanes);
            }
            while (q.Count > 0)
            {
                var c = q.Dequeue();
                foreach (var d in dirs)
                {
                    var n = new Point(c.X + d.X, c.Y + d.Y);
                    if (prev.ContainsKey(n) || (n != forced && (hazards.Contains(n) || (Avoid != null && Avoid.Contains(n)) || !Walkable(n))))
                    {
                        continue;
                    }
                    prev[n] = c;
                    if (isGoal(n))
                    {
                        var path = new List<Point>();
                        for (var p = n; p != a; p = prev[p])
                        {
                            path.Add(p);
                        }
                        path.Reverse();
                        return path;
                    }
                    q.Enqueue(n);
                }
            }
            return null;
        }
    }
}
