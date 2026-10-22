using AnodyneSharp.Entities;
using AnodyneSharp.MapData;
using AnodyneSharp.MapData.Tiles;
using AnodyneSharp.Registry;
using Microsoft.Xna.Framework;
using System;
using System.Collections.Generic;
using System.Linq;

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
            public int Count => _added.Count;

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
            // A key block with a key in hand is a door we open on the way (the walker does, on reaching it): plan through it.
            bool haveKey = GlobalState.inventory.GetCurrentMapKeys() > 0;
            foreach (var e in BotRunner.Entities())
            {
                if (!e.exists || !e.GetType().Name.EndsWith("Gate"))
                {
                    continue;
                }
                var t = new Point((int)(e.Position.X + 8) / 16, (int)(e.Position.Y + 8) / 16);
                (e.Solid && !(haveKey && e is AnodyneSharp.Entities.Gadget.SmallKeyGate) ? _gatesClosed : _gatesOpen).Add(t);
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
                if (haveKey && pr.Type == typeof(AnodyneSharp.Entities.Gadget.SmallKeyGate))
                {
                    _gatesOpen.Add(t);
                    continue;
                }
                // A gate on the edge of the next screen, right against this one: entering a screen over a gate holds it open.
                int gx = t.X / 10, gy = t.Y / 10, lx = t.X % 10, ly = t.Y % 10;
                if (pr.Type == typeof(AnodyneSharp.Entities.Gadget.Gate) && !pr.Type.Name.StartsWith("Big") &&
                    ((ly == 0 && gx == grid.X && gy == grid.Y + 1) || (ly == 9 && gx == grid.X && gy == grid.Y - 1) ||
                     (lx == 0 && gy == grid.Y && gx == grid.X + 1) || (lx == 9 && gy == grid.Y && gx == grid.X - 1)))
                {
                    _gatesOpen.Add(t);
                    continue;
                }
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
        private static Dictionary<(int, int), Dictionary<Point, int>> _screenComps = new();
        private static Dictionary<(int, int), HashSet<int>> _dustComps = new();

        private static void BuildComps(int sx, int sy)
        {
            if (_screenComps.ContainsKey((sx, sy)))
            {
                return;
            }
            var comp = new Dictionary<Point, int>();
            int id = 0;
            bool oldW = AllowWater, oldJ = AllowJump;
            AllowWater = false;
            AllowJump = false;
            RefreshGates();
            var rect = new Rectangle(sx * 10, sy * 10, 10, 10);
            Point[] dirs = { new(1, 0), new(-1, 0), new(0, 1), new(0, -1) };
            for (int y = rect.Y; y < rect.Y + 10; y++)
            {
                for (int x = rect.X; x < rect.X + 10; x++)
                {
                    var t = new Point(x, y);
                    if (comp.ContainsKey(t) || !Walkable(t))
                    {
                        continue;
                    }
                    id++;
                    var q = new Queue<Point>();
                    q.Enqueue(t);
                    comp[t] = id;
                    while (q.Count > 0)
                    {
                        var c = q.Dequeue();
                        foreach (var d in dirs)
                        {
                            var n = new Point(c.X + d.X, c.Y + d.Y);
                            if (rect.Contains(n) && !comp.ContainsKey(n) && Walkable(n))
                            {
                                comp[n] = id;
                                q.Enqueue(n);
                            }
                        }
                    }
                }
            }
            AllowWater = oldW;
            AllowJump = oldJ;
            var dc = new HashSet<int>();
            var dustTiles = new List<Point>();
            foreach (var pr in AnodyneSharp.Entities.EntityManager.GetMapEntities(GlobalState.CURRENT_MAP_NAME))
            {
                if (pr.Type != null && pr.Type.Name == "Dust" && (int)pr.Position.X / 160 == sx && (int)pr.Position.Y / 160 == sy)
                {
                    dustTiles.Add(new Point((int)(pr.Position.X + 8) / 16, (int)(pr.Position.Y + 8) / 16));
                }
            }
            var g = GlobalState.CurrentMapGrid;
            if (g.X == sx && g.Y == sy)
            {
                foreach (var e in BotRunner.Entities())
                {
                    if (e.GetType().Name == "Dust")
                    {
                        dustTiles.Add(TileOf(e.Position + new Vector2(8, 8)));
                    }
                }
                if (BotRunner.Player?.broom.dust != null)
                {
                    dustTiles.Add(TileOf(BotRunner.Player.Position)); // the dust in the broom goes wherever the player goes
                }
            }
            foreach (var dt in dustTiles)
            {
                foreach (var o in new[] { new Point(0, 0), new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) })
                {
                    if (comp.TryGetValue(new Point(dt.X + o.X, dt.Y + o.Y), out int cid))
                    {
                        dc.Add(cid);
                    }
                }
            }
            _screenComps[(sx, sy)] = comp;
            _dustComps[(sx, sy)] = dc;
        }

        /// <summary>True if a dust can be reached on dry land from tile t within its screen (so a raft can be made there).</summary>
        public static bool DustReachableFrom(Point t)
        {
            int sx = t.X / 10, sy = t.Y / 10;
            BuildComps(sx, sy);
            return _screenComps[(sx, sy)].TryGetValue(t, out int cid) && _dustComps[(sx, sy)].Contains(cid);
        }

        public static bool ScreenHasDust(int sx, int sy) => false;

        public static void ResetDustCache()
        {
            _dustScreens = null;
            _screenComps = new();
            _dustComps = new();
        }

        /// <summary>
        /// Route that may cross water only on a raft: a raft can be made where the land tile you step off from lies on a screen with
        /// dust, and stays under you until you step back onto land. `wet` = already on a raft at `from`.
        /// </summary>
        public static List<Point> RaftPath(Point from, bool wet, Point goal, bool jump)
        {
            // Round the dash traps when there is any other way (walking past one in a corridor is a hit more often than not).
            var traps = new HashSet<Point>();
            foreach (var e in BotRunner.Entities())
            {
                if (e.exists && e.GetType().Name is "BounceDashTrap" or "OnSightDashTrap")
                {
                    var t = TileOf(e.Position + new Vector2(8, 8));
                    foreach (var o in new[] { new Point(0, 0), new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) })
                    {
                        var n = new Point(t.X + o.X, t.Y + o.Y);
                        if (n != from && n != goal)
                        {
                            traps.Add(n);
                        }
                    }
                }
            }
            return (traps.Count > 0 ? RaftPathCore(from, wet, goal, jump, traps) : null) ?? RaftPathCore(from, wet, goal, jump, null);
        }

        private static List<Point> RaftPathCore(Point from, bool wet, Point goal, bool jump, HashSet<Point> avoid)
        {
            RefreshGates();
            RefreshShooterTiles(); // a Four_Shooter in the river is solid, and rafting into it is sitting in its crossfire
            var haz = Hazards();
            if (avoid != null)
            {
                haz.UnionWith(avoid);
            }
            bool oldJ = AllowJump, oldW = AllowWater;
            AllowJump = jump;
            AllowWater = false;
            // State: tile, on a raft, and "the raft we just got off is lying here on this screen" (usable for the next crossing).
            var prev = new Dictionary<(Point, bool, bool), (Point, bool, bool)>();
            var q = new Queue<(Point, bool, bool)>();
            var start = (from, wet, !wet && BotRunner.Player?.broom.dust != null); // a dust in the broom is a raft to hand
            prev[start] = start;
            q.Enqueue(start);
            Point[] dirs = { new(1, 0), new(-1, 0), new(0, 1), new(0, -1) };
            (Point, bool, bool)? found = null;
            while (q.Count > 0 && found == null)
            {
                var (c, w, carry) = q.Dequeue();
                var cameFrom = prev[(c, w, carry)].Item1;
                foreach (var n in Steps(cameFrom, c))
                {
                    if (RaftBlocked.Contains((c, n)))
                    {
                        continue;
                    }
                    bool nw = IsConveyor(n) && !Wade && !(jump && NarrowWater(n) && !(RaftNarrow && w));
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
                            if (al != Touching.NONE || _gatesClosed.Contains(n) || _shooterTiles.Contains(n) || (!w && !carry && !DustReachableFrom(c)))
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

        /// <summary>Raft steps (tile to tile) found impassable while riding, e.g. a screen edge the current keeps pushing back over.</summary>
        public static readonly HashSet<(Point, Point)> RaftBlocked = new();

        /// <summary>Set while riding a dust raft: conveyor (river) tiles count as walkable then.</summary>
        public static bool AllowWater;
        /// <summary>Water counts as floor (wading: half speed, and Red Cave water drowns you after 3s, so only for short stretches).</summary>
        public static bool Wade;
        /// <summary>On a raft, keep rafting along 1-2 tile wide rivers (which are otherwise only jumped across, with the boots).</summary>
        public static bool RaftNarrow;

        /// <summary>Set once the boots are on: spike tiles count as walkable (they are jumped over).</summary>
        public static bool AllowJump;

        /// <summary>Water only one or two tiles across (in some straight line, with dry land on both sides): jumpable with the boots.</summary>
        public static bool NarrowWater(Point t)
        {
            bool Land(Point q)
            {
                if (_gatesOpen.Contains(q))
                {
                    return true;
                }
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

        public static bool IsJumpable(Point t) => IsSpike(t) || (IsConveyor(t) && NarrowWater(t)) || (IsHole(t) && NarrowHole(t));

        public static bool IsHole(Point t) => CurrentMap.GetTileClass(t).Item2 == CollisionEventType.HOLE;

        /// <summary>A pit one tile across (in some straight line, with floor on both sides): a jump with the boots clears it (32px).</summary>
        public static bool NarrowHole(Point t)
        {
            bool Floor(Point q)
            {
                if (_gatesOpen.Contains(q))
                {
                    return true;
                }
                var (al, ev) = CurrentMap.GetTileClass(q);
                return al == Touching.NONE && ev != CollisionEventType.CONVEYOR && ev != CollisionEventType.HOLE && ev != CollisionEventType.SPIKE;
            }
            return (Floor(new Point(t.X - 1, t.Y)) && Floor(new Point(t.X + 1, t.Y))) || (Floor(new Point(t.X, t.Y - 1)) && Floor(new Point(t.X, t.Y + 1)));
        }

        /// <summary>
        /// A path may only cross a pit in a straight line (the jump goes on the way it started): from a hole tile the next tile is the one
        /// straight on, and it must be floor on the far side.
        /// </summary>
        private static bool HoleStepOk(Point prev, Point c, Point n)
        {
            if (AllowJump && prev != c && IsSpike(c))
            {
                // A jump goes straight and clears two spike tiles at most: no turning inside a spike bed, no third tile of one.
                if (n.X - c.X != c.X - prev.X || n.Y - c.Y != c.Y - prev.Y)
                {
                    return false;
                }
                if (IsSpike(prev) && IsSpike(n))
                {
                    return false;
                }
            }
            if (!IsHole(c))
            {
                return true;
            }
            return n.X - c.X == c.X - prev.X && n.Y - c.Y == c.Y - prev.Y;
        }

        private static string _ledgeMap;
        private static Dictionary<Point, Point> _ledges = new();

        /// <summary>
        /// Ledge drops (JumpTrigger, Jump_Trigger with no type value): walking down onto one jumps the player 32/48/64px down, over the ledge
        /// face. From the map's presets, so the drops of screens not loaded yet are known too. Trigger tile -> landing tile.
        /// </summary>
        public static Dictionary<Point, Point> Ledges()
        {
            if (_ledgeMap == GlobalState.CURRENT_MAP_NAME)
            {
                return _ledges;
            }
            _ledgeMap = GlobalState.CURRENT_MAP_NAME;
            _ledges = new Dictionary<Point, Point>();
            foreach (var pr in AnodyneSharp.Entities.EntityManager.GetMapEntities(GlobalState.CURRENT_MAP_NAME))
            {
                if (pr.Type != typeof(AnodyneSharp.Entities.Gadget.JumpTrigger))
                {
                    continue;
                }
                int dist = pr.Frame == 1 ? 48 : pr.Frame == 2 ? 64 : 32;
                var t = new Point((int)Math.Floor(pr.Position.X / 16f), (int)Math.Floor(pr.Position.Y / 16f));
                _ledges[t] = new Point(t.X, t.Y + dist / 16);
            }
            return _ledges;
        }

        private static string _springMap;
        private static Dictionary<Point, Point> _springs = new();

        /// <summary>Spring pads (Jump_Trigger type 1): landing on one (a jump onto it) bounces the player 32/48/64px down. Tile -> landing tile.</summary>
        public static Dictionary<Point, Point> Springs()
        {
            if (_springMap == GlobalState.CURRENT_MAP_NAME)
            {
                return _springs;
            }
            _springMap = GlobalState.CURRENT_MAP_NAME;
            _springs = new Dictionary<Point, Point>();
            foreach (var pr in AnodyneSharp.Entities.EntityManager.GetMapEntities(GlobalState.CURRENT_MAP_NAME))
            {
                if (pr.Type != typeof(AnodyneSharp.Entities.Gadget.SpringPad))
                {
                    continue;
                }
                int dist = pr.Frame == 1 ? 48 : pr.Frame == 2 ? 64 : 32;
                var t = new Point((int)Math.Floor(pr.Position.X / 16f), (int)Math.Floor(pr.Position.Y / 16f));
                _springs[t] = new Point(t.X, t.Y + dist / 16);
            }
            return _springs;
        }

        /// <summary>The spring a jump from a to b bounces off (a spring right below a whose landing is b), if any.</summary>
        public static Point? SpringBetween(Point a, Point b)
        {
            var below = new Point(a.X, a.Y + 1);
            return Springs().TryGetValue(below, out var land) && land == b ? below : null;
        }

        /// <summary>The tiles a search may step to from c (reached from prev): the four neighbours, or a ledge's landing (entered going down, that is the only way on).</summary>
        private static IEnumerable<Point> Steps(Point prev, Point c)
        {
            var ledges = Ledges();
            if (ledges.TryGetValue(c, out var land))
            {
                yield return land;
                if (c.Y - prev.Y == 1)
                {
                    yield break; // walked down onto the trigger: the jump goes off
                }
            }
            foreach (var d in StepDirs)
            {
                var n = new Point(c.X + d.X, c.Y + d.Y);
                if (HoleStepOk(prev, c, n))
                {
                    yield return n;
                }
            }
            if (AllowJump && Springs().TryGetValue(new Point(c.X, c.Y + 1), out var bounce) && LeapFloor(c))
            {
                yield return bounce; // jump down onto the spring below: it throws us on down
            }
            if (_leaps && AllowJump && LeapFloor(c))
            {
                // Free jumps between islands of floor (stepping stones in a pit): a jump carries ~27px on each axis that has a key held, so
                // any floor up to two tiles away on each axis, with only pit, water or spikes in between (no wall), is one step.
                for (int dy = -2; dy <= 2; dy++)
                {
                    for (int dx = -2; dx <= 2; dx++)
                    {
                        if (Math.Abs(dx) + Math.Abs(dy) <= 1)
                        {
                            continue;
                        }
                        var n = new Point(c.X + dx, c.Y + dy);
                        if (LeapFloor(n) && LeapClear(c, n) && LeapFits(c, n))
                        {
                            yield return n;
                        }
                    }
                }
            }
        }

        private static bool _leaps;

        /// <summary>Firm floor to jump from or land on (not pit, water or spikes).</summary>
        private static bool LeapFloor(Point t)
        {
            if (_gatesOpen.Contains(t))
            {
                return true;
            }
            var (al, ev) = CurrentMap.GetTileClass(t);
            return al == Touching.NONE && ev != CollisionEventType.HOLE && ev != CollisionEventType.CONVEYOR && ev != CollisionEventType.SPIKE;
        }

        /// <summary>Nothing solid in the box between a and b, and something there that cannot be walked (else it is no jump, just a walk).</summary>
        private static bool LeapClear(Point a, Point b)
        {
            bool gap = false, floor = false;
            for (int y = Math.Min(a.Y, b.Y); y <= Math.Max(a.Y, b.Y); y++)
            {
                for (int x = Math.Min(a.X, b.X); x <= Math.Max(a.X, b.X); x++)
                {
                    var t = new Point(x, y);
                    if (t == a || t == b)
                    {
                        continue;
                    }
                    if (CurrentMap.GetTileClass(t).Item1 != Touching.NONE)
                    {
                        return false;
                    }
                    if (LeapFloor(t) && !_shooterTiles.Contains(t))
                    {
                        floor = true; // (a frog or plant on the tile between: jumped clean over, touching it hurts)
                    }
                    else
                    {
                        gap = true;
                    }
                }
            }
            return gap && !floor;
        }

        private static readonly Dictionary<(Point, Point), bool> _leapFits = new();
        private static string _leapFitsMap;

        /// <summary>A free spot to stand on (the player's 10x12 box): no wall, water or spikes under a corner, and clear of every pit's 6x4 core.</summary>
        public static bool BoxStands(Rectangle r)
        {
            foreach (var (x, y) in new[] { (r.Left, r.Top), (r.Right - 1, r.Top), (r.Left, r.Bottom - 1), (r.Right - 1, r.Bottom - 1) })
            {
                var t = new Point((int)Math.Floor(x / 16f), (int)Math.Floor(y / 16f));
                if (IsHole(t))
                {
                    if (new Rectangle(t.X * 16 + 5, t.Y * 16 + 5, 6, 4).Intersects(r))
                    {
                        return false;
                    }
                }
                else if (!_gatesOpen.Contains(t) && (!Walkable(t) || IsConveyor(t) || IsSpike(t)))
                {
                    return false;
                }
            }
            return true;
        }

        /// <summary>Px a jump carries with one direction key held (~29); with two, ~21 on each axis.</summary>
        public const float LeapReach = 29f, LeapReachDiag = 21f;

        /// <summary>
        /// Is there a take-off spot on a from which a jump (keys towards b) lands clear on b, give or take 2px of carry? The search the
        /// jump itself makes (Leap), done in the plan so it never offers a jump the physics cannot make.
        /// </summary>
        public static bool LeapFits(Point a, Point b)
        {
            if (_leapFitsMap != GlobalState.CURRENT_MAP_NAME)
            {
                _leapFits.Clear();
                _leapFitsMap = GlobalState.CURRENT_MAP_NAME;
            }
            if (_leapFits.TryGetValue((a, b), out var known))
            {
                return known;
            }
            int sx = Math.Sign(b.X - a.X), sy = Math.Sign(b.Y - a.Y);
            float reach = sx != 0 && sy != 0 ? LeapReachDiag : LeapReach;
            const int w = 10, h = 12;
            bool ok = false;
            for (int y = a.Y * 16 - 6; y <= a.Y * 16 + 16 - h + 6 && !ok; y++)
            {
                for (int x = a.X * 16 - 6; x <= a.X * 16 + 16 - w + 6 && !ok; x++)
                {
                    var s = new Rectangle(x, y, w, h);
                    if (new Point((x + w / 2) / 16, (y + h / 2) / 16) != a || !BoxStands(s))
                    {
                        continue;
                    }
                    ok = true;
                    foreach (float carry in new[] { reach - 2, reach, reach + 2 })
                    {
                        var l = new Rectangle((int)Math.Round(x + sx * carry), (int)Math.Round(y + sy * carry), w, h);
                        ok &= new Point((l.X + w / 2) / 16, (l.Y + h / 2) / 16) == b && BoxStands(l);
                    }
                }
            }
            _leapFits[(a, b)] = ok;
            return ok;
        }

        /// <summary>Consecutive path tiles that are not neighbours and not a ledge drop: a free jump (see Steps).</summary>
        public static bool IsLeap(Point a, Point b) => Math.Abs(a.X - b.X) + Math.Abs(a.Y - b.Y) > 1 && !(Ledges().TryGetValue(a, out var l) && l == b);

        private static readonly Point[] StepDirs = { new(1, 0), new(-1, 0), new(0, 1), new(0, -1) };

        /// <summary>A solid wall tile: it stops bullets (holes, water and spikes do not, they fly over them).</summary>
        public static bool IsBeamWall(Point t) => CurrentMap == null || CurrentMap.GetTileClass(t).Item1 == Touching.ANY;

        private static System.Reflection.FieldInfo _tileObjectsField;

        /// <summary>Which way the water on t flows (Touching.NONE for still water or no water).</summary>
        public static Touching FlowDir(Point t)
        {
            if (!IsConveyor(t))
            {
                return Touching.NONE;
            }
            _tileObjectsField ??= typeof(Map).GetField("_tileObjects", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            var tiles = _tileObjectsField?.GetValue(CurrentMap) as Tile[];
            int id = CurrentMap.GetTile(Layer.BG, t);
            var dir = tiles != null && id >= 0 && id < tiles.Length && tiles[id] != null ? tiles[id].direction : Touching.NONE;
            return dir is Touching.UP or Touching.DOWN or Touching.LEFT or Touching.RIGHT ? dir : Touching.NONE; // (still water says ANY)
        }

        /// <summary>The way a steam pipe on this screen blows over t (its lane: up to 4 tiles out in front of it), if one does.</summary>
        public static Touching? SteamFlow(Point t)
        {
            // from the map's presets, so screens not loaded yet count too (frame: 0 down, 1 right, 2 up, 3 left)
            foreach (var pr in AnodyneSharp.Entities.EntityManager.GetMapEntities(GlobalState.CURRENT_MAP_NAME))
            {
                if (pr.Type != typeof(AnodyneSharp.Entities.Enemy.Hotel.SteamPipe))
                {
                    continue;
                }
                var pt = new Point((int)(pr.Position.X + 8) / 16, (int)(pr.Position.Y + 8) / 16);
                var (dx, dy, dir) = pr.Frame switch { 1 => (1, 0, Touching.RIGHT), 2 => (0, -1, Touching.UP), 3 => (-1, 0, Touching.LEFT), _ => (0, 1, Touching.DOWN) };
                for (int k = 1; k <= 4; k++)
                {
                    if (t == new Point(pt.X + dx * k, pt.Y + dy * k))
                    {
                        return dir;
                    }
                }
            }
            return null;
        }

        /// <summary>
        /// Can a path's water be waded? Off a raft the player sinks for 3s and is then drowned back to the screen's entrance; walking in
        /// water is 35px/s, plus the current's 35px/s along it (minus against it). So every unbroken run of water on the path must take
        /// under ~2.4s (a run with the current is long, against it impossible).
        /// </summary>
        public static bool WadeOk(List<Point> path, Point from)
        {
            float secs = 0;
            var prev = from;
            foreach (var t in path)
            {
                if (!IsConveyor(t))
                {
                    secs = 0;
                    prev = t;
                    continue;
                }
                secs += WadeSecs(prev, t);
                if (secs > WadeLimit)
                {
                    return false;
                }
                prev = t;
            }
            return true;
        }

        private const float WadeLimit = 2.4f;

        /// <summary>Seconds to wade from `prev` into water tile `t` (infinite against a current we cannot make headway in).</summary>
        private static float WadeSecs(Point prev, Point t)
        {
            var flow = FlowDir(t);
            float push = 35f;
            if (flow == Touching.NONE && SteamFlow(t) is Touching steam)
            {
                flow = steam; // a steam pipe blowing across still water shoves us over it (50px/s)
                push = 50f;
            }
            int dx = t.X - prev.X, dy = t.Y - prev.Y;
            float along = flow switch { Touching.DOWN => dy, Touching.UP => -dy, Touching.RIGHT => dx, Touching.LEFT => -dx, _ => 0 };
            float speed = 35f + push * along;
            return speed < 10f ? float.PositiveInfinity : 16f / speed;
        }

        /// <summary>
        /// A route that wades only where each stretch of water is short enough to get out of before sinking: the shortest route may
        /// wade a long channel where going round by the banks crosses two short ones. Searches (tile, time already spent in this
        /// stretch of water); null when there is no such route.
        /// </summary>
        public static List<Point> WadePath(Point a, Func<Point, bool> isGoal, Point? forced = null)
        {
            bool water = AllowWater;
            AllowWater = true;
            try
            {
                RefreshGates();
                var hazards = Hazards();
                // (time quantised to 1/20s, so a tile reached a little later in its stretch of water is a different state)
                var best = new Dictionary<(Point, int), int>();
                var prev = new Dictionary<(Point, int), (Point, int)>();
                var pq = new PriorityQueue<(Point, int), int>();
                var start = (a, 0);
                best[start] = 0;
                prev[start] = start;
                pq.Enqueue(start, 0);
                while (pq.TryDequeue(out var st, out int cost))
                {
                    if (cost > best[st])
                    {
                        continue;
                    }
                    var (c, run) = st;
                    if (c != a && !IsConveyor(c) && isGoal(c))
                    {
                        var path = new List<Point>();
                        for (var s = st; s != start; s = prev[s])
                        {
                            path.Add(s.Item1);
                        }
                        path.Reverse();
                        return path;
                    }
                    var from = prev[st].Item1;
                    foreach (var n in Steps(from == c ? c : from, c))
                    {
                        if (n != forced && (hazards.Contains(n) || (Avoid != null && Avoid.Contains(n)) || !Walkable(n)))
                        {
                            continue;
                        }
                        int nrun = 0;
                        int step = 1;
                        if (IsConveyor(n))
                        {
                            float secs = run / 20f + WadeSecs(c, n);
                            if (secs > WadeLimit)
                            {
                                continue;
                            }
                            nrun = (int)Math.Ceiling(secs * 20f);
                            step = 3; // (wading is slow and every stretch a risk: a little way round on dry land is better)
                        }
                        var ns = (n, nrun);
                        int nc = cost + step;
                        if (!best.TryGetValue(ns, out int old) || nc < old)
                        {
                            best[ns] = nc;
                            prev[ns] = st;
                            pq.Enqueue(ns, nc);
                        }
                    }
                }
                return null;
            }
            finally
            {
                AllowWater = water;
            }
        }

        public static bool IsConveyor(Point t) => CurrentMap.GetTileClass(t).Item2 == CollisionEventType.CONVEYOR;

        public static bool IsSpike(Point t) => CurrentMap.GetTileClass(t).Item2 == CollisionEventType.SPIKE;

        public static string DebugGates() => string.Join(" ", System.Linq.Enumerable.Select(_gatesClosed, t => $"{t.X},{t.Y}"));

        private static bool _respectClosedGates = true;

        public static bool IsGateTile(Point p) => _gatesOpen.Contains(p) || _gatesClosed.Contains(p);

        private static HashSet<Point> _shooterTiles = new();
        private static long _shooterTilesFrame = -1;

        /// <summary>Tiles under living Burst_Plants and Frogs (not solid in the game, but standing there is the one sure way to be shot).</summary>
        public static void RefreshShooterTiles()
        {
            if (_shooterTilesFrame == BotRunner.Frame)
            {
                return;
            }
            _shooterTilesFrame = BotRunner.Frame;
            var set = new HashSet<Point>();
            foreach (var e in BotRunner.Entities())
            {
                if (e.exists && e.GetType().Name is "Burst_Plant" or "Frog" or "Red_Pillar" or "Chain" or "Four_Shooter")
                {
                    var r = e.Hitbox;
                    for (int ty = r.Top / 16; ty <= (r.Bottom - 1) / 16; ty++)
                    {
                        for (int tx = r.Left / 16; tx <= (r.Right - 1) / 16; tx++)
                        {
                            set.Add(new Point(tx, ty));
                        }
                    }
                }
            }
            _shooterTiles = set;
        }

        /// <summary>
        /// The current screen as text (cheaper to read than a screenshot): one char per tile ('#' wall, '.' floor, 'h' hole, '~' water or
        /// conveyor, '^' spikes, ',' puddle), each entity's letter on the tile under its centre and '@' for the player, then one line per entity
        /// with its exact hitbox, velocity and facing.
        /// </summary>
        /// <summary>
        /// Another screen of this map as text without going there: the tile grid as in RoomText, then the entities its presets would make
        /// (type, tile, preset frame and type value, and whether it is still alive), lettered on their tiles.
        /// </summary>
        public static string ScreenText(Point grid)
        {
            var map = CurrentMap;
            if (map == null)
            {
                return "SCREEN: no map";
            }
            var cells = new char[10, 10];
            for (int y = 0; y < 10; y++)
            {
                for (int x = 0; x < 10; x++)
                {
                    var (al, ev) = map.GetTileClass(new Point(grid.X * 10 + x, grid.Y * 10 + y));
                    cells[y, x] = ev switch
                    {
                        CollisionEventType.HOLE => 'h',
                        CollisionEventType.CONVEYOR => '~',
                        CollisionEventType.SPIKE => '^',
                        CollisionEventType.PUDDLE => ',',
                        _ => al == Touching.NONE ? '.' : '#',
                    };
                }
            }
            var lines = new List<string>();
            char next = 'A';
            foreach (var p in AnodyneSharp.Entities.EntityManager.GetGridEntities(GlobalState.CURRENT_MAP_NAME, grid))
            {
                var t = new Point((int)Math.Floor((p.Position.X + 8) / 16f), (int)Math.Floor((p.Position.Y + 8) / 16f));
                char ch = next <= 'Z' ? next++ : '*';
                int cx = t.X - grid.X * 10, cy = t.Y - grid.Y * 10;
                if (cx >= 0 && cx < 10 && cy >= 0 && cy < 10)
                {
                    cells[cy, cx] = ch;
                }
                lines.Add($"  {ch} {p.Type.Name} tile={t.X},{t.Y} f{p.Frame}" + (string.IsNullOrEmpty(p.TypeValue) ? "" : $" '{p.TypeValue}'") + (p.Alive ? "" : " DEAD"));
            }
            var sb = new System.Text.StringBuilder($"SCREEN {GlobalState.CURRENT_MAP_NAME} {grid.X},{grid.Y} (tiles {grid.X * 10},{grid.Y * 10} = top-left)\n");
            for (int y = 0; y < 10; y++)
            {
                sb.Append($"  {grid.Y * 10 + y,3} ");
                for (int x = 0; x < 10; x++)
                {
                    sb.Append(cells[y, x]);
                }
                sb.Append('\n');
            }
            foreach (var l in lines)
            {
                sb.Append(l).Append('\n');
            }
            return sb.ToString();
        }

        public static string RoomText()
        {
            var map = CurrentMap;
            var pl = BotRunner.Player;
            if (map == null || pl == null)
            {
                return "ROOM: no map";
            }
            var grid = GlobalState.CurrentMapGrid;
            var cells = new char[10, 10];
            for (int y = 0; y < 10; y++)
            {
                for (int x = 0; x < 10; x++)
                {
                    var t = new Point(grid.X * 10 + x, grid.Y * 10 + y);
                    var (al, ev) = map.GetTileClass(t);
                    cells[y, x] = ev switch
                    {
                        CollisionEventType.HOLE => 'h',
                        CollisionEventType.CONVEYOR => '~',
                        CollisionEventType.SPIKE => '^',
                        CollisionEventType.PUDDLE => ',',
                        _ => al == Touching.NONE ? '.' : '#',
                    };
                }
            }
            var sb = new System.Text.StringBuilder();
            var lines = new List<string>();
            var letters = new Dictionary<string, char>();
            foreach (var e in BotRunner.Entities())
            {
                if (!e.exists || e == pl || e is Player || !e.visible && e.width == 0)
                {
                    continue;
                }
                string name = e.GetType().Name;
                if (name is "Foot_Overlay" or "PlayerReflection" or "Lookahead" or "Entity")
                {
                    continue;
                }
                if (!letters.TryGetValue(name, out char ch))
                {
                    ch = (char)('A' + letters.Count % 26);
                    if (letters.Count >= 26)
                    {
                        ch = (char)('a' + (letters.Count - 26) % 26);
                    }
                    letters[name] = ch;
                }
                var hb = e.Hitbox;
                int cx = (int)Math.Floor(hb.Center.X / 16f) - grid.X * 10, cy = (int)Math.Floor(hb.Center.Y / 16f) - grid.Y * 10;
                if (cx >= 0 && cx < 10 && cy >= 0 && cy < 10)
                {
                    cells[cy, cx] = ch;
                }
                string v = e.velocity == Vector2.Zero ? "" : $" v=({e.velocity.X:0},{e.velocity.Y:0})";
                string frame = "";
                try
                {
                    frame = $" f{e.Frame}";
                }
                catch (NullReferenceException)
                {
                    // no sprite to have a frame
                }
                lines.Add($"  {ch} {name} box={hb.X},{hb.Y} {hb.Width}x{hb.Height}{v} {e.facing}{frame}" + (e.Solid ? " solid" : "")
                        + (e is AnodyneSharp.Entities.Enemy.Crowd.BaseSpikeRoller sr ? $" collide={sr.CollisionOn} visible={sr.visible} offY={sr.offset.Y:0}" : ""));
            }
            var pb = pl.Hitbox;
            int px = (int)Math.Floor(pb.Center.X / 16f) - grid.X * 10, py = (int)Math.Floor(pb.Center.Y / 16f) - grid.Y * 10;
            if (px >= 0 && px < 10 && py >= 0 && py < 10)
            {
                cells[py, px] = '@';
            }
            sb.Append($"ROOM {GlobalState.CURRENT_MAP_NAME} screen {grid.X},{grid.Y} (tiles {grid.X * 10},{grid.Y * 10} = top-left) player box={pb.X},{pb.Y} hp {GlobalState.CUR_HEALTH} {pl.state}\n");
            for (int y = 0; y < 10; y++)
            {
                sb.Append($"  {grid.Y * 10 + y,3} ");
                for (int x = 0; x < 10; x++)
                {
                    sb.Append(cells[y, x]);
                }
                sb.Append('\n');
            }
            foreach (var l in lines.Distinct())
            {
                sb.Append(l).Append('\n');
            }
            return sb.ToString();
        }

        public static bool Walkable(Point t)
        {
            if (HardAvoid != null && HardAvoid.Contains(t))
            {
                return false;
            }
            if (_shooterTiles.Contains(t))
            {
                return false; // a plant or frog we can walk through but must not (its bullets spawn on it)
            }
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
                    return AllowJump && allow == Touching.NONE && NarrowHole(t);
                case CollisionEventType.PUDDLE:
                    return allow == Touching.NONE; // shallow water: slows you down but can be walked
                case CollisionEventType.SPIKE:
                    return AllowJump && allow == Touching.NONE;
                case CollisionEventType.CONVEYOR:
                    return allow == Touching.NONE && (AllowWater || Wade || (AllowJump && NarrowWater(t)));
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
                if (name == "TreasureChest" || name == "Statue" || name == "Sign" || name.EndsWith("Quest") || (name == "CrackedTile" && !_crossCracked) || name == "Hole" || name == "Dust_Pit" || name == "SteamPipe" || name.EndsWith("Door") || name.EndsWith("Portal"))
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

        /// <summary>Tiles that count as walls for every search (set by a routine that must not cross them, e.g. the beams while carrying dust to an emitter).</summary>
        public static HashSet<Point> HardAvoid;

        /// <summary>Scale of the path cost of beam tiles (token y&lt;percent&gt;): where the only way on is down a beam, a long detour is not better.</summary>
        public static float LaneScale = 1f;

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
                    AnodyneSharp.Entities.Facing.RIGHT => new Point(1, 0),
                    AnodyneSharp.Entities.Facing.LEFT => new Point(-1, 0),
                    AnodyneSharp.Entities.Facing.UP => new Point(0, -1),
                    _ => new Point(0, 1)
                };
                // An emitter need not sit on the tile grid (a laser at y=38 fires along y 38..54, over two tile rows): the beam covers every
                // row (column) its 16px box touches. Pixel -> tile directly (TileOf would add a hitbox half-size).
                var starts = new List<Point>();
                int cx = (int)Math.Floor((e.Position.X + 8) / 16f), cy = (int)Math.Floor((e.Position.Y + 8) / 16f);
                if (d.X != 0)
                {
                    for (int ty = (int)Math.Floor(e.Position.Y / 16f); ty <= (int)Math.Floor((e.Position.Y + 15) / 16f); ty++)
                    {
                        starts.Add(new Point(cx, ty));
                    }
                }
                else
                {
                    for (int tx = (int)Math.Floor(e.Position.X / 16f); tx <= (int)Math.Floor((e.Position.X + 15) / 16f); tx++)
                    {
                        starts.Add(new Point(tx, cy));
                    }
                }
                // The bullets fly along the emitter's middle: a dust on that line stops the beam over every row (column) it touches.
                var dusts = System.Linq.Enumerable.ToList(System.Linq.Enumerable.Where(BotRunner.Entities(), x => x.exists && x.GetType().Name == "Dust"));
                int reach = 24;
                var mid = TileOf(e.Position + new Vector2(8, 8));
                for (int i = 1; i < 24; i++)
                {
                    mid = new Point(mid.X + d.X, mid.Y + d.Y);
                    if (System.Linq.Enumerable.Any(dusts, x => TileOf(x.Position + new Vector2(8, 8)) == mid))
                    {
                        reach = i;
                        break;
                    }
                }
                foreach (var start in starts)
                {
                    var t = start;
                    set[t] = d.X != 0 ? 0 : 1; // the emitter tile itself: every bullet is born there and dwells a while, it is no shortcut past the beam
                    for (int i = 1; i < reach; i++)
                    {
                        t = new Point(t.X + d.X, t.Y + d.Y);
                        if (IsBeamWall(t) || System.Linq.Enumerable.Any(BotRunner.Entities(), x => x.exists && x.GetType().Name == "Dust" && TileOf(x.Position + new Vector2(8, 8)) == t))
                        {
                            break;
                        }
                        set[t] = d.X != 0 ? 0 : 1;
                    }
                }
            }
            return set;
        }

        /// <summary>
        /// A Silverfish we are herding (SilverfishOnButton): while it sits, its row and column (and the tiles round it) are where it sees
        /// us and runs, so walks keep off them where they can and it only runs when we mean it to.
        /// </summary>
        public static Entity SpookFish;

        /// <summary>Extra path cost near stationary shooters (their bubbles always find a player walking past): 2 tiles = 12, 4 tiles = 5.</summary>
        public static Dictionary<Point, int> ShooterZones()
        {
            var zones = new Dictionary<Point, int>();
            foreach (var e in BotRunner.Entities())
            {
                if (e.exists && e.GetType().Name == "PewLaser")
                {
                    // A plugged beam still fires its bullets across the tiles between the emitter and the plug (a bullet spawns
                    // right at the muzzle and hits whoever stands there before it reaches the dust): walk round them if possible.
                    var d = e.facing switch { AnodyneSharp.Entities.Facing.RIGHT => new Point(1, 0), AnodyneSharp.Entities.Facing.LEFT => new Point(-1, 0), AnodyneSharp.Entities.Facing.UP => new Point(0, -1), _ => new Point(0, 1) };
                    var mt = TileOf(e.Position + new Vector2(8, 8));
                    var cells = new List<Point>();
                    for (int i = 1; i < 24; i++)
                    {
                        mt = new Point(mt.X + d.X, mt.Y + d.Y);
                        cells.Add(mt);
                        if (IsBeamWall(mt))
                        {
                            cells.Clear();
                            break;
                        }
                        if (System.Linq.Enumerable.Any(BotRunner.Entities(), x => x.exists && x.GetType().Name == "Dust" && TileOf(x.Position + new Vector2(8, 8)) == mt))
                        {
                            break;
                        }
                        if (i > 8)
                        {
                            cells.Clear();
                            break;
                        }
                    }
                    foreach (var cell in cells)
                    {
                        if (!LaneAxes().ContainsKey(cell))
                        {
                            zones[cell] = Math.Max(zones.TryGetValue(cell, out int oldZone) ? oldZone : 0, 30);
                        }
                    }
                    continue;
                }
                if (!e.exists || e.GetType().Name != "Burst_Plant")
                {
                    continue;
                }
                var c = TileOf(e.Position + new Vector2(8, 8));
                for (int dx = -4; dx <= 4; dx++)
                {
                    for (int dy = -4; dy <= 4; dy++)
                    {
                        int dist = Math.Max(Math.Abs(dx), Math.Abs(dy));
                        var t = new Point(c.X + dx, c.Y + dy);
                        zones[t] = Math.Max(zones.TryGetValue(t, out int old) ? old : 0, dist <= 2 ? 12 : 5);
                    }
                }
            }
            if (SpookFish != null && SpookFish.exists && SpookFish.velocity == Vector2.Zero)
            {
                var ft = TileOf(SpookFish.Position + new Vector2(8, 8));
                foreach (var d in new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) })
                {
                    for (int i = 1; i < 10; i++)
                    {
                        var t = new Point(ft.X + d.X * i, ft.Y + d.Y * i);
                        if (!Walkable(t))
                        {
                            break;
                        }
                        zones[t] = Math.Max(zones.TryGetValue(t, out int z) ? z : 0, 10);
                    }
                }
                for (int dy = -1; dy <= 1; dy++)
                {
                    for (int dx = -1; dx <= 1; dx++)
                    {
                        var t = new Point(ft.X + dx, ft.Y + dy);
                        zones[t] = Math.Max(zones.TryGetValue(t, out int z) ? z : 0, 10);
                    }
                }
            }
            // Red Cave steam jets: walking along or across one is a gamble with its 2s cycle, so the route keeps off them where it can
            foreach (var e in BotRunner.Entities())
            {
                if (!e.exists || e.GetType().Name != "OnOffLaser")
                {
                    continue;
                }
                foreach (var sub in e.SubEntities())
                {
                    for (int ty = (int)Math.Floor(sub.Position.Y / 16f); ty <= (int)Math.Floor((sub.Position.Y + sub.height - 1) / 16f); ty++)
                    {
                        for (int tx = (int)Math.Floor(sub.Position.X / 16f); tx <= (int)Math.Floor((sub.Position.X + sub.width - 1) / 16f); tx++)
                        {
                            zones[new Point(tx, ty)] = Math.Max(zones.TryGetValue(new Point(tx, ty), out int z0) ? z0 : 0, 40);
                        }
                    }
                }
            }
            // An idle on-sight dash trap charges whoever lines up with it at 80px/s: crossing its line far from it is safe (we are across
            // before it arrives), next to it is a hit. Close-by tiles of its lines cost more, so the route crosses where it is far.
            foreach (var e in BotRunner.Entities())
            {
                if (!e.exists || e.GetType().Name != "OnSightDashTrap" || e.velocity != Vector2.Zero)
                {
                    continue;
                }
                var t0 = new Point((int)Math.Floor(e.Hitbox.Center.X / 16f), (int)Math.Floor(e.Hitbox.Center.Y / 16f));
                foreach (var d in new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) })
                {
                    var t = t0;
                    for (int i = 1; i < 10; i++)
                    {
                        t = new Point(t.X + d.X, t.Y + d.Y);
                        if (!Walkable(t))
                        {
                            break;
                        }
                        zones[t] = (zones.TryGetValue(t, out int z1) ? z1 : 0) + Math.Max(2, 13 - 3 * i);
                    }
                }
            }
            return zones;
        }

        /// <summary>Tiles within `margin` px of any living enemy except `except`.</summary>
        /// <summary>
        /// Black and White Town's killers: not enemies (no health, nothing kills them) but a touch costs 6 hearts. One stands still
        /// until the player comes within 36px, then creeps after them at 30px/s for good: a berth that wide leaves it asleep.
        /// </summary>
        public static bool IsSuburbKiller(Entity e) => e.GetType().Name == "SuburbKiller";
        public const int KillerWake = 30;

        public static HashSet<Point> NearEnemies(int margin, Entity except = null, bool stationaryOnly = false)
        {
            var set = new HashSet<Point>();
            foreach (var e in BotRunner.Entities())
            {
                bool killer = IsSuburbKiller(e);
                if (!e.exists || e == except || !(killer || e.GetType().IsDefined(typeof(AnodyneSharp.Entities.Enemy.EnemyAttribute), false)) || (stationaryOnly && e.velocity != Vector2.Zero))
                {
                    continue;
                }
                var r = e.Hitbox;
                int m = killer ? margin + KillerWake : margin;
                r.Inflate(m, m);
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
            if (p == null && !strict && AllowJump)
            {
                // Stepping stones: jump from island to island (cracked ones too: they hold for a second).
                _leaps = true;
                _crossCracked = true;
                p = FindPathCore(a, isGoal, forced);
                if (p == null)
                {
                    _respectClosedGates = false;
                    p = FindPathCore(a, isGoal, forced);
                    _respectClosedGates = true;
                }
                _crossCracked = false;
                _leaps = false;
            }
            // (with the boots, the caller's search with jumps comes next: a leap clean over it beats walking into it)
            if (p == null && !strict && _shooterTiles.Count > 0 && (AllowJump || !GlobalState.inventory.CanJump))
            {
                // A plant or frog sits in the only way through: they are not solid, so it is walked over (its bullets are dodged, or
                // the walker strikes it as a blocker) rather than no way at all.
                var shooters = _shooterTiles;
                _shooterTiles = new HashSet<Point>();
                try
                {
                    p = FindPathCore(a, isGoal, forced);
                    if (p == null)
                    {
                        _respectClosedGates = false;
                        p = FindPathCore(a, isGoal, forced);
                        _respectClosedGates = true;
                    }
                }
                finally
                {
                    _shooterTiles = shooters;
                }
            }
            return p;
        }

        /// <summary>
        /// Dijkstra variant used on screens with laser beams: walking along a beam is costly (every bullet meets you), crossing
        /// it sideways is cheap, so the route crosses beams at right angles where it can.
        /// </summary>
        private static List<Point> FindPathWeighted(Point a, Func<Point, bool> isGoal, Point? forced, HashSet<Point> hazards, Dictionary<Point, int> lanes, Dictionary<Point, int> zones)
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
                foreach (var n in Steps(prev[c], c))
                {
                    var d = new Point(Math.Sign(n.X - c.X), Math.Sign(n.Y - c.Y));
                    if (n != forced && (hazards.Contains(n) || (Avoid != null && Avoid.Contains(n)) || !Walkable(n)))
                    {
                        continue;
                    }
                    int step = 1;
                    if (lanes.TryGetValue(n, out int axis) && ((axis == 0) == (d.Y == 0)))
                    {
                        step += (int)(40 * LaneScale); // moving along the beam
                    }
                    else if (lanes.ContainsKey(n))
                    {
                        step += (int)(30 * LaneScale); // crossing it (a fast emitter has no gap a walker can use: prefer any detour)
                    }
                    if (zones.TryGetValue(n, out int zc))
                    {
                        step += zc;
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
            var zones = ShooterZones();
            if ((lanes.Count > 0 || zones.Count > 0) && Environment.GetEnvironmentVariable("ANODYNE_NO_WEIGHT") == null)
            {
                return FindPathWeighted(a, isGoal, forced, hazards, lanes, zones);
            }
            while (q.Count > 0)
            {
                var c = q.Dequeue();
                foreach (var n in Steps(prev[c], c))
                {
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
