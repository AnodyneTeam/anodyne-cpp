using AnodyneSharp.Entities;
using AnodyneSharp.Entities.Enemy;
using AnodyneSharp.Input;
using AnodyneSharp.Registry;
using Microsoft.Xna.Framework;
using System;
using System.Collections.Generic;
using System.Linq;

namespace AnodyneSharp.Recording
{
    public abstract partial class BotScript
    {

        /// <summary>
        /// Runs a route written as space-separated tokens: U/D/L/R&lt;frames&gt; hold, W&lt;frames&gt; wait, A&lt;n&gt;/C&lt;n&gt; tap Accept/Cancel,
        /// T read dialogue, G&lt;x&gt;,&lt;y&gt; straight walk, P&lt;x&gt;,&lt;y&gt; pathfind, Qn/Qe/Qs/Qw head to the neighbouring screen,
        /// F fight, I&lt;x&gt;,&lt;y&gt;,&lt;l|r|u|d&gt; interact with the object at x,y from that side; M/E/S&lt;n&gt; are debug dumps.
        /// </summary>
        protected static IEnumerable<object> Exec(string tokens)
        {
            foreach (var tok in tokens.Split(' ', StringSplitOptions.RemoveEmptyEntries))
            {
                int n = tok.Length > 1 && int.TryParse(tok.Substring(1), out int v) ? v : 1;
                switch (tok[0])
                {
                    case 'U': yield return Hold(n, KeyFunctions.Up); break;
                    case 'D': yield return Hold(n, KeyFunctions.Down); break;
                    case 'L': yield return Hold(n, KeyFunctions.Left); break;
                    case 'R': yield return Hold(n, KeyFunctions.Right); break;
                    case 'W': yield return Wait(n); break;
                    case 'T': yield return ReadDialogue(); break;
                    case 'Q': yield return GoScreen(tok[1]); break;
                    case 'F': yield return Fight(); break;
                    case 'I':
                    {
                        // Ix,y,side e.g. I768,672,l
                        var a = tok.Substring(1).Split(',');
                        yield return InteractAt(float.Parse(a[0]), float.Parse(a[1]), a[2][0]);
                        break;
                    }
                    case 'E':
                    {
                        var sb = new System.Text.StringBuilder($"ENTITIES grid {Grid} player {Pos}\n");
                        foreach (var e in BotRunner.Entities())
                        {
                            sb.Append($"  {e.GetType().Name} pos={e.Position} size={e.width}x{e.height} exists={e.exists} solid={e.Solid}\n");
                        }
                        System.Console.Error.WriteLine(sb.ToString());
                        break;
                    }
                    case 'H':
                    {
                        var a = tok.Substring(1).Split(',');
                        yield return GoScreenTo(int.Parse(a[0]), int.Parse(a[1]));
                        break;
                    }
                    case 'V': yield return SaveHere(); break;
                    case '%': yield return BlockLasers(); break;
                    case 'B':
                    {
                        var a = tok.Substring(1).Split(',');
                        yield return StandUntilGateOpens(float.Parse(a[0]), float.Parse(a[1]));
                        break;
                    }
                    case 'O': DodgeEnabled = tok[1] == '1'; break;
                    case '^':
                    {
                        // Dev only (experiments, never in a recorded route): put the player at x,y.
                        var a = tok.Substring(1).Split(',');
                        Player.Position = new Vector2(float.Parse(a[0]), float.Parse(a[1]));
                        yield return Wait(5);
                        break;
                    }
                    case '!':
                        // Dev only: remove every living enemy on screen (to see what their death unlocks).
                        foreach (var e in Enemies())
                        {
                            e.exists = false;
                        }
                        break;
                    case '*': GlobalState.CUR_HEALTH = int.Parse(tok.Substring(1)); break;
                    case '@': LaserMargin = int.Parse(tok.Substring(1)); break;
                    case 'X': AutoFight = tok[1] == '1'; break;
                    case 'Y':
                    {
                        var a = tok.Substring(1).Split(',');
                        var goal = new Point(int.Parse(a[0]), int.Parse(a[1]));
                        var p = BotNav.FindPath(BotNav.TileOf(Pos), goal);
                        System.Console.Error.WriteLine($"PATH {BotNav.TileOf(Pos)} -> {goal}: " + (p == null ? "none" : $"{p.Count} tiles: " + string.Join(">", p.Select(q => $"{q.X},{q.Y}"))));
                        break;
                    }
                    case 'J':
                    {
                        // Jx,y,dir: stand on tile x,y, face dir (u/d/l/r) and press Accept (pick up / place dust, press a switch...)
                        var a = tok.Substring(1).Split(',');
                        yield return StandFaceAccept(int.Parse(a[0]), int.Parse(a[1]), a[2][0]);
                        break;
                    }
                    case 'K': yield return TempleLaserDust(); break;
                    case 'k': yield return FillHoles(); break;
                    case '~':
                    {
                        // ~x,y: ride a dust raft over the river to the tile containing world point x,y
                        var xy = tok.Substring(1).Split(',');
                        yield return RaftTo(float.Parse(xy[0]), float.Parse(xy[1]));
                        break;
                    }
                    case '#': yield return TempleButtonRoom(); break;
                    case 'N':
                        System.Console.Error.WriteLine("BLOCKED: " + string.Join(" ", BotNav.Blocked.Select(q => $"{q.X},{q.Y}")));
                        for (int r = 20; r <= 28; r++) { System.Console.Error.WriteLine(BotNav.DebugRow(r, 14, 30)); }
                        break;
                    case '+': yield return OpenChests(); break;
                    case '>':
                    {
                        // >x,y: walk to world point x,y across screens (fighting, rafting and jumping as needed)
                        var xy = tok.Substring(1).Split(',');
                        yield return GoGlobal(float.Parse(xy[0]), float.Parse(xy[1]));
                        break;
                    }
                    case 'r':
                    {
                        // Debug: the most permissive route (rafts, jumps, shut gates ignored) to world point x,y, as screens and obstacles.
                        var a = tok.Substring(1).Split(',');
                        var goalT = BotNav.TileOf(new Vector2(float.Parse(a[0]), float.Parse(a[1])));
                        BotNav.AllowWater = true;
                        BotNav.AllowJump = true;
                        var rp = a.Length > 2 ? BotNav.RaftPath(BotNav.TileOf(Pos), OnConveyor(BotNav.TileOf(Pos)), goalT, GlobalState.inventory.CanJump) : BotNav.FindPath(BotNav.TileOf(Pos), t => t == goalT, goalT);
                        BotNav.AllowWater = false;
                        BotNav.AllowJump = false;
                        if (rp == null)
                        {
                            System.Console.Error.WriteLine($"ROUTE to {goalT}: none (CanJump={GlobalState.inventory.CanJump})");
                            break;
                        }
                        var sbr = new System.Text.StringBuilder($"ROUTE to {goalT} (CanJump={GlobalState.inventory.CanJump}): {rp.Count} tiles: ");
                        string lastKind = "", lastScr = "";
                        foreach (var t in rp)
                        {
                            string scr = $"({t.X / 10},{t.Y / 10})";
                            string kind = OnConveyor(t) ? "~" : BotNav.IsSpike(t) ? "^" : BotNav.IsGateTile(t) ? "G" : "";
                            if (scr != lastScr)
                            {
                                sbr.Append(" " + scr);
                                lastScr = scr;
                                lastKind = "";
                            }
                            if (kind != "" && kind != lastKind)
                            {
                                sbr.Append(kind + $"{t.X},{t.Y}");
                            }
                            lastKind = kind;
                        }
                        System.Console.Error.WriteLine(sbr.ToString());
                        break;
                    }
                    case 'j':
                    {
                        var a = tok.Substring(1).Split(',');
                        var gt = BotNav.TileOf(new Vector2(float.Parse(a[0]), float.Parse(a[1])));
                        BotNav.AllowJump = true;
                        var jp = BotNav.FindPath(BotNav.TileOf(Pos), t => t == gt, gt);
                        BotNav.AllowJump = false;
                        System.Console.Error.WriteLine("JUMPPATH " + (jp == null ? "none" : string.Join(">", jp.Select(q => $"{q.X},{q.Y}{(BotNav.IsJumpable(q) ? "^" : "")}"))) + $" narrow(76,81)={BotNav.NarrowWater(new Point(76, 81))} narrow(76,82)={BotNav.NarrowWater(new Point(76, 82))}");
                        break;
                    }
                    case 'h':
                    {
                        // h<sx>,<sy>,<ox>,<oy>: farm hearts by re-entering screen (sx,sy) (enemies respawn) from (ox,oy) until HP is full
                        var a = tok.Substring(1).Split(',').Select(int.Parse).ToArray();
                        for (int i = 0; i < 10 && GlobalState.CUR_HEALTH < GlobalState.MAX_HEALTH; i++)
                        {
                            yield return GoScreenTo(a[0], a[1]);
                            yield return CollectHearts();
                            yield return GoScreenTo(a[2], a[3]);
                        }
                        break;
                    }
                    case 'n':
                    {
                        // n<tab>: pause menu -> tab number <tab> (the Map tab) -> "return to the Nexus pad" -> yes
                        yield return MenuReturnToPad(n);
                        break;
                    }
                    case 'u':
                    {
                        // dev: single-frame taps: u<k> with k = 1 Up, 2 Down, 3 Left, 4 Right, 5 Accept, 6 Cancel, 7 Pause
                        yield return Tap((KeyFunctions)n);
                        yield return Wait(8);
                        break;
                    }
                    case 'q': System.Console.Error.WriteLine($"CARRYING {Player.broom.dust != null} raft={HasRaft()} grid={Grid} pos={Pos}"); break;
                    case '?':
                    {
                        // Debug: tile class of tiles x,y[,x2,y2...]
                        var a = tok.Substring(1).Split(',').Select(int.Parse).ToArray();
                        for (int i = 0; i + 1 < a.Length; i += 2)
                        {
                            var (al, ev) = BotNav.CurrentMap.GetTileClass(new Point(a[i], a[i + 1]));
                            System.Console.Error.WriteLine($"TILE {a[i]},{a[i + 1]}: {al} {ev} walkable={BotNav.Walkable(new Point(a[i], a[i + 1]))}");
                        }
                        break;
                    }
                    case 'z':
                    {
                        var d = BotNav.DistMap(BotNav.TileOf(Pos), true);
                        var scr = new SortedSet<(int, int)>(d.Keys.Select(t => (t.X / 10, t.Y / 10)));
                        System.Console.Error.WriteLine("REACHABLE(water) screens: " + string.Join(" ", scr) + $"  tiles {d.Count}");
                        break;
                    }
                    case 'Z':
                        System.Console.Error.WriteLine("REACHABLE screens: " + string.Join(" ", BotNav.ReachableScreens(BotNav.TileOf(Pos))));
                        break;
                    case 'M':
                    {
                        // Debug: dump tile classes around the player ('.' free, '#' solid, else collision/event info).
                        var c = BotNav.TileOf(Pos);
                        var map = BotNav.CurrentMap;
                        var sb = new System.Text.StringBuilder($"MAP @tile {c.X},{c.Y} pos {Pos}\n");
                        for (int ty = Grid.Y * 10 - 1; ty <= Grid.Y * 10 + 10; ty++)
                        {
                            for (int tx = Grid.X * 10 - 2; tx <= Grid.X * 10 + 11; tx++)
                            {
                                var (al, ev) = map.GetTileClass(new Point(tx, ty));
                                char ch = (tx == c.X && ty == c.Y) ? '@' : al == Touching.ANY ? '#' : al == Touching.NONE && ev == AnodyneSharp.MapData.Tiles.CollisionEventType.NONE ? '.' : ev != AnodyneSharp.MapData.Tiles.CollisionEventType.NONE ? ev.ToString()[0] : 'o';
                                sb.Append(ch);
                            }
                            sb.Append('\n');
                        }
                        System.Console.Error.WriteLine(sb.ToString());
                        break;
                    }
                    case '$':
                    {
                        // Debug: every entity preset of the current map whose type name contains the filter (empty = all), by screen.
                        string f = tok.Substring(1);
                        var sb = new System.Text.StringBuilder("PRESETS\n");
                        foreach (var pr in AnodyneSharp.Entities.EntityManager.GetMapEntities(GlobalState.CURRENT_MAP_NAME))
                        {
                            if (pr.Type.Name.Contains(f))
                            {
                                sb.Append($"  {pr.Type.Name} screen {(int)pr.Position.X / 160},{(int)pr.Position.Y / 160} pos {(int)pr.Position.X},{(int)pr.Position.Y} frame {pr.Frame} tv {pr.TypeValue}\n");
                            }
                        }
                        System.Console.Error.WriteLine(sb);
                        break;
                    }
                    case '&':
                    {
                        // Debug: walkability dump of screens sx,sy..ex,ey ('.' free, '#' solid, letters = tile events, 'E' entity).
                        var a = tok.Substring(1).Split(',');
                        int sx = int.Parse(a[0]), sy = int.Parse(a[1]), ex = int.Parse(a[2]), ey = int.Parse(a[3]);
                        var map = BotNav.CurrentMap;
                        var ents = new Dictionary<Point, char>();
                        foreach (var e in BotRunner.Entities())
                        {
                            if (e.exists && e.width > 0)
                            {
                                ents[new Point((int)(e.Position.X + 8) / 16, (int)(e.Position.Y + 8) / 16)] = e.GetType().Name[0];
                            }
                        }
                        var sb = new System.Text.StringBuilder();
                        for (int ty = sy * 10; ty < ey * 10 + 10; ty++)
                        {
                            if (ty % 10 == 0) { sb.Append('\n'); }
                            for (int tx = sx * 10; tx < ex * 10 + 10; tx++)
                            {
                                var (al, ev) = map.GetTileClass(new Point(tx, ty));
                                char ch = ents.TryGetValue(new Point(tx, ty), out char ec) ? char.ToLower(ec) : al == Touching.ANY ? '#' : al == Touching.NONE && ev == AnodyneSharp.MapData.Tiles.CollisionEventType.NONE ? '.' : ev != AnodyneSharp.MapData.Tiles.CollisionEventType.NONE ? ev.ToString()[0] : 'o';
                                sb.Append(ch);
                                if (tx % 10 == 9) { sb.Append(' '); }
                            }
                            sb.Append('\n');
                        }
                        System.Console.Error.WriteLine("MAPDUMP" + sb);
                        break;
                    }
                    case 'P':
                    {
                        var xy = tok.Substring(1).Split(',');
                        yield return GoTo(float.Parse(xy[0]), float.Parse(xy[1]));
                        break;
                    }
                    case 'G':
                    {
                        var xy = tok.Substring(1).Split(',');
                        yield return WalkTo(float.Parse(xy[0]), float.Parse(xy[1]), yFirst: xy.Length > 2);
                        break;
                    }
                    case 'S': yield return Wait(2); AnodyneSharp.Drawing.SpriteDrawer.SaveRender($"/tmp/bot/shot{n}.png"); break;
                    case 'A':
                        for (int i = 0; i < n; i++) { yield return Tap(KeyFunctions.Accept); yield return Wait(8); }
                        break;
                    case 'C':
                        for (int i = 0; i < n; i++) { yield return Tap(KeyFunctions.Cancel); yield return Wait(8); }
                        break;
                    default: throw new ArgumentException("bad token " + tok);
                }
            }
        }

        /// <summary>Head-to-screen moves clear the screen's enemies on arrival (soft: gives up after a while).</summary>
        protected static bool AutoFight = true;

        private static int _lastHp;
        protected const int SwingCooldown = 18;
        protected const int AttackReach = 26;

        /// <summary>Frame to press while a dialogue/cutscene/menu is open: mash Accept, but choose "Yes" on the death menu first.</summary>
        public static KeyFunctions[] AdvanceKeys(int i)
        {
            if (BotRunner.InDeathMenu)
            {
                return i % 12 == 0 ? new[] { KeyFunctions.Up } : i % 12 == 6 ? new[] { KeyFunctions.Accept } : new KeyFunctions[0];
            }
            return i % 4 == 0 ? new[] { KeyFunctions.Accept } : new KeyFunctions[0];
        }

        private static Point? _intended;
        private static string _intendedMap = "";

        public static void ResetIntended()
        {
            _intended = null;
        }

        /// <summary>A block of steps that is replayed from the top after every death.</summary>
        protected static IEnumerable<object> Section(string name, Func<IEnumerable<object>> body, Func<IEnumerable<object>> prereq = null)
        {
            yield return new SectionEnumerator(name, body, prereq);
        }

        /// <summary>Stand on the checkpoint on this screen and press Accept to save (death then respawns here).</summary>
        protected static IEnumerable<object> SaveHere()
        {
            var cp = BotRunner.Entities().FirstOrDefault(e => e.exists && e.GetType().Name == "Checkpoint");
            if (cp == null)
            {
                throw new InvalidOperationException($"bot: no checkpoint on {Map} screen {Grid}");
            }
            yield return GoTo(cp.Position.X - 1, cp.Position.Y - 3);
            yield return Wait(10);
            yield return Tap(KeyFunctions.Accept);
            yield return Wait(30);
            yield return ReadDialogue();
        }

        protected static Point Grid => GlobalState.CurrentMapGrid;

        /// <summary>"Head north/east/south/west": walk to the nearest tile of the neighbouring 160x160 screen, then let the scroll finish.</summary>
        protected static IEnumerable<object> GoScreen(char dir)
        {
            // Relative to the screen the route intends to be on, not wherever a fight happened to drift to.
            var g = (_intendedMap == Map && _intended.HasValue) ? _intended.Value : Grid;
            int dx = dir == 'e' ? 1 : dir == 'w' ? -1 : 0;
            int dy = dir == 's' ? 1 : dir == 'n' ? -1 : 0;
            int tx = g.X + dx, ty = g.Y + dy;
            _intended = new Point(tx, ty);
            _intendedMap = Map;
            BotRunner.Status = $"head {dir}: screen {g.X},{g.Y} -> {tx},{ty}";
            return GoScreenTo(tx, ty);
        }

        protected static IEnumerable<object> GoScreenTo(int tx, int ty)
        {
            yield return GoToWhere(t => t.X * 16 >= tx * 160 && t.X * 16 < tx * 160 + 160 && t.Y * 16 >= ty * 160 && t.Y * 16 < ty * 160 + 160,
                $"screen {tx},{ty}");
            yield return Until(() => !GlobalState.ScreenTransition && Grid == new Point(tx, ty), 600, $"arrive on screen {tx},{ty}");
            yield return Wait(10);
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
            {
                System.Console.Error.WriteLine($"  -> screen {tx},{ty} on {Map}, hp {GlobalState.CUR_HEALTH}");
                var interesting = BotRunner.Entities().Where(e => e.exists && !(e is Player) &&
                    System.Text.RegularExpressions.Regex.IsMatch(e.GetType().Name, "Chest|Gate|Button|Key|Switch|Dust|Door|Pillar|Statue|Console|NPC|Sage|Card|Treasure|Lever|Block|Portal|Checkpoint|Boss|Seer") ||
                    (e.exists && e.GetType().IsDefined(typeof(AnodyneSharp.Entities.Enemy.EnemyAttribute), false)))
                    .Select(e => $"{e.GetType().Name}@{(int)e.Position.X},{(int)e.Position.Y}");
                System.Console.Error.WriteLine("     " + string.Join(" ", interesting));
            }
            if (AutoFight && GlobalState.inventory.HasBroom && Enemies().Count > 0)
            {
                int closed() => BotRunner.Entities().Count(e => e.exists && e.GetType().Name == "Gate" && e.Solid);
                int before = closed();
                yield return Fight(1500, soft: true);
                // Clearing a room usually opens its gates: give them a moment so the route planner does not see them shut.
                for (int i = 0; i < 240 && before > 0 && closed() >= before && Enemies().Count == 0; i++)
                {
                    yield return Talking ? AdvanceKeys(i) : K();
                }
                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    System.Console.Error.WriteLine($"  gates closed {before} -> {closed()}, enemies {Enemies().Count}");
                }
            }
        }

        /// <summary>
        /// Walks next to the interactable at (x,y) (16x16) on the given side ('l','r','u','d' = which side of it to stand),
        /// faces it and presses Accept, then reads whatever dialogue results.
        /// </summary>
        protected static IEnumerable<object> InteractAt(float x, float y, char side, int presses = 2)
        {
            // Hitbox top-left for standing flush against the object (hitbox is 10x12, object 16x16).
            float sx = side == 'l' ? x - 11 : side == 'r' ? x + 17 : x + 3;
            float sy = side == 'u' ? y - 13 : side == 'd' ? y + 17 : y + 2;
            yield return GoTo(sx, sy);
            yield return WalkTo(sx, sy);
            KeyFunctions face = side == 'l' ? KeyFunctions.Right : side == 'r' ? KeyFunctions.Left : side == 'u' ? KeyFunctions.Down : KeyFunctions.Up;
            yield return Hold(8, face);
            for (int i = 0; i < presses; i++)
            {
                yield return Tap(KeyFunctions.Accept);
                yield return Wait(8);
            }
            yield return ReadDialogue();
        }

        /// <summary>Walks onto (x,y) and stands there until some gate on the screen opens (e.g. a second button pressed by an enemy).</summary>
        protected static IEnumerable<object> StandUntilGateOpens(float x, float y, int timeoutFrames = 3000)
        {
            yield return GoTo(x, y);
            BotRunner.Status = "standing on a button until a gate opens";
            bool Open() => BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "Gate" && !e.Solid);
            for (int i = 0; i < timeoutFrames && !Open(); i++)
            {
                if (Talking)
                {
                    yield return AdvanceKeys(i);
                    continue;
                }
                if (i % 100 == 0 && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    System.Console.Error.WriteLine("  buttons: " + string.Join(" ", BotRunner.Entities().Where(e => e.exists && (e.GetType().Name == "Shieldy" || e.GetType().Name == "Button")).Select(e => $"{e.GetType().Name}@{e.Hitbox.X},{e.Hitbox.Y}({e.Hitbox.Width}x{e.Hitbox.Height}) v={e.velocity.X:0},{e.velocity.Y:0}")) + $" hp={GlobalState.CUR_HEALTH} solved={GlobalState.PUZZLES_SOLVED}");
                }
                // Stay on the button: nudge back if something pushed us off.
                var c = BotNav.CentreOf(BotNav.TileOf(new Vector2(x, y)));
                float dx = c.X - Pos.X, dy = c.Y - Pos.Y;
                yield return Math.Abs(dx) > 3 ? K(dx < 0 ? KeyFunctions.Left : KeyFunctions.Right)
                           : Math.Abs(dy) > 3 ? K(dy < 0 ? KeyFunctions.Up : KeyFunctions.Down) : K();
            }
            if (!Open())
            {
                throw new TimeoutException($"bot: gate never opened on {Map} screen {Grid}");
            }
            yield return Wait(30);
        }

        // ---- danger avoidance ----
        /// <summary>The last SafeMove changed the wanted key (waiting for a gap / sidestepping): not a sign of being stuck.</summary>
        public static bool Dodged;

        /// <summary>Off by default so the already-verified early route is unchanged; parts that need it switch it on (token O1).</summary>
        public static bool DodgeEnabled;

        /// <summary>Extra px kept around beam bullets (they leave 12px gaps for a 10px player, so 0 is needed to slip through a dense row).</summary>
        public static int LaserMargin = 2;
        private static int _dodgeStreak;

        private const float WalkPxPerFrame = 70f / 60f;

        private static readonly System.Text.RegularExpressions.Regex ProjectileName =
            new("Laser|Goo|Bullet|Fire|Spit|Shot|Bolt|Slash|Burst|Spike|Wave", System.Text.RegularExpressions.RegexOptions.Compiled);

        /// <summary>Things that hurt on contact: living enemies and their projectiles (not `except`).</summary>
        protected static List<Entity> Dangers(Entity except = null)
        {
            return BotRunner.Entities().Where(e => e.exists && e != except && !(e is Player) && e.width > 0 && e.height > 0 &&
                (e.GetType().IsDefined(typeof(AnodyneSharp.Entities.Enemy.EnemyAttribute), false) || ProjectileName.IsMatch(e.GetType().Name))).ToList();
        }

        private static Vector2 DirVector(KeyFunctions k) => k switch
        {
            KeyFunctions.Up => new Vector2(0, -1),
            KeyFunctions.Down => new Vector2(0, 1),
            KeyFunctions.Left => new Vector2(-1, 0),
            KeyFunctions.Right => new Vector2(1, 0),
            _ => Vector2.Zero
        };

        /// <summary>True if walking in `dir` for the next few frames would put the player's hitbox into a danger (moving ones extrapolated).</summary>
        protected static bool WouldCollide(KeyFunctions dir, List<Entity> dangers, int lookahead = 8)
        {
            var move = DirVector(dir) * WalkPxPerFrame;
            var pb = Player.Hitbox;
            for (int f = 2; f <= 28; f += 2)
            {
                var mine = new Rectangle((int)(pb.X + move.X * f), (int)(pb.Y + move.Y * f), pb.Width, pb.Height);
                foreach (var d in dangers)
                {
                    // Enemies are only worried about a few frames ahead; bolts get the whole crossing (a 16px beam takes ~22 frames).
                    if (f > lookahead && !d.GetType().Name.Contains("Laser"))
                    {
                        continue;
                    }
                    var r = d.Hitbox;
                    r.Offset((int)(d.velocity.X * f / 60f), (int)(d.velocity.Y * f / 60f));
                    // The beam's bullets are 8px apart-gaps of 12px for a 10px-wide player: no extra margin or it never fits.
                    bool bolt = d.GetType().Name.Contains("Laser");
                    int m = bolt ? LaserMargin : 2;
                    r.Inflate(m, m);
                    if (mine.Intersects(r))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        /// <summary>
        /// Turns a wanted movement key into a safe frame: the same key if the next few frames are clear, else a perpendicular
        /// sidestep or standing still. `except` = an enemy we are deliberately closing in on.
        /// </summary>
        protected static KeyFunctions[] SafeMove(KeyFunctions wanted, Entity except = null)
        {
            Dodged = false;
            if (!DodgeEnabled)
            {
                return K(wanted);
            }
            var dangers = Dangers(except);
            if (dangers.Count == 0 || !WouldCollide(wanted, dangers))
            {
                if (_dodgeStreak > 0 && _dodgeStreak <= 45)
                {
                    _dodgeStreak = Math.Max(0, _dodgeStreak - 2);
                }
                return K(wanted);
            }
            // Waiting for a gap that never comes would stall the route: after a while accept the risk and go.
            if (++_dodgeStreak > 45)
            {
                if (_dodgeStreak > 90)
                {
                    _dodgeStreak = 0;
                }
                Dodged = false;
                return K(wanted);
            }
            Dodged = true;
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null && BotRunner.Frame % 60 == 0)
            {
                System.Console.Error.WriteLine($"  dodge: wanted {wanted} at {Player.Hitbox} dangers: " + string.Join(" ", dangers.Select(d => $"{d.GetType().Name}{d.Hitbox}")));
            }
            KeyFunctions[] sides = (wanted == KeyFunctions.Up || wanted == KeyFunctions.Down)
                ? new[] { KeyFunctions.Left, KeyFunctions.Right }
                : new[] { KeyFunctions.Up, KeyFunctions.Down };
            foreach (var s in sides)
            {
                if (!WouldCollide(s, dangers))
                {
                    return K(s);
                }
            }
            // Cornered by the wanted direction: back off if that is clear, else hold still.
            var back = wanted switch { KeyFunctions.Up => KeyFunctions.Down, KeyFunctions.Down => KeyFunctions.Up, KeyFunctions.Left => KeyFunctions.Right, _ => KeyFunctions.Left };
            return !WouldCollide(back, dangers) ? K(back) : K();
        }

        /// <summary>True if a wall is right beside the enemy's tile on one side only (it plugs part of a narrow passage).</summary>
        private static bool InCorridor(Entity e)
        {
            var t = BotNav.TileOf(new Vector2(e.Hitbox.Center.X - 5, e.Hitbox.Center.Y - 6));
            return BotNav.Walkable(new Point(t.X + 1, t.Y)) != BotNav.Walkable(new Point(t.X - 1, t.Y));
        }

        /// <summary>
        /// The key to press for a tile step (px,py away): horizontal first normally, but if that would run into `solid` (an enemy
        /// we are walking around) go the other way first.
        /// </summary>
        protected static KeyFunctions StepKey(float px, float py, Entity solid)
        {
            KeyFunctions h = px < 0 ? KeyFunctions.Left : KeyFunctions.Right;
            KeyFunctions v = py < 0 ? KeyFunctions.Up : KeyFunctions.Down;
            bool wantH = Math.Abs(px) > 2f;
            bool wantV = Math.Abs(py) > 2f;
            var one = new List<Entity> { solid };
            if (Environment.GetEnvironmentVariable("ANODYNE_NO_STEPKEY") == null && wantH && wantV && WouldCollide(h, one, 6) && !WouldCollide(v, one, 6))
            {
                return v;
            }
            if (wantH)
            {
                return h;
            }
            return v;
        }

        /// <summary>Stand centred on tile (tx,ty), turn to face `face` (one-frame tap; the nudge is ~1px), then press Accept once.</summary>
        protected static IEnumerable<object> StandFaceAccept(int tx, int ty, char face)
        {
            var c = BotNav.CentreOf(new Point(tx, ty));
            yield return GoTo(c.X, c.Y);
            yield return WalkTo(c.X, c.Y, tol: 1f);
            var key = face == 'u' ? KeyFunctions.Up : face == 'd' ? KeyFunctions.Down : face == 'l' ? KeyFunctions.Left : KeyFunctions.Right;
            yield return Hold(1, key);
            yield return Wait(3);
            yield return Tap(KeyFunctions.Accept);
            yield return Wait(12);
        }

        private static Point FacingStep(Facing f) => f switch
        {
            Facing.RIGHT => new Point(1, 0),
            Facing.LEFT => new Point(-1, 0),
            Facing.UP => new Point(0, -1),
            _ => new Point(0, 1)
        };

        /// <summary>True if the dust sits right at the end of a beam (picking it up there means loitering in the line of fire).</summary>
        private static bool NextToLane(Entity dust, HashSet<Point> lanes)
        {
            var t = BotNav.TileOf(dust.Position + new Vector2(8, 8));
            return lanes.Contains(new Point(t.X + 1, t.Y)) || lanes.Contains(new Point(t.X - 1, t.Y)) ||
                   lanes.Contains(new Point(t.X, t.Y + 1)) || lanes.Contains(new Point(t.X, t.Y - 1));
        }

        private static char FaceChar(Point toward) => toward.Y < 0 ? 'u' : toward.Y > 0 ? 'd' : toward.X < 0 ? 'l' : 'r';

        /// <summary>
        /// Plugs every PewLaser on the screen with loose dust: pick a dust up by sweeping it with the broom, then stand next to
        /// the tile in front of the emitter, face it and press Accept to set the dust down there.
        /// </summary>
        protected static IEnumerable<object> BlockLasers()
        {
            var emitters = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "PewLaser").ToList();
            foreach (var em in emitters)
            {
                var step = FacingStep(em.facing);
                var et = BotNav.TileOf(em.Position + new Vector2(8, 8));
                var target = new Point(et.X + step.X, et.Y + step.Y);
                bool plugged() => BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "Dust" && BotNav.TileOf(e.Position + new Vector2(8, 8)) == target);
                if (plugged())
                {
                    continue;
                }
                var lanes = BotNav.LaneTiles();
                var dust = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Dust" && !lanes.Contains(BotNav.TileOf(e.Position + new Vector2(8, 8))))
                    .OrderBy(e => NextToLane(e, lanes) ? 1 : 0).ThenBy(e => (e.Position - em.Position).LengthSquared()).FirstOrDefault();
                if (dust == null)
                {
                    continue;
                }
                var dt = BotNav.TileOf(dust.Position + new Vector2(8, 8));
                BotRunner.Status = $"plugging laser at {et.X},{et.Y}";
                Point? pickFrom = null;
                foreach (var o in new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) })
                {
                    var t = new Point(dt.X + o.X, dt.Y + o.Y);
                    if (BotNav.Walkable(t) && !lanes.Contains(t))
                    {
                        pickFrom = t;
                        yield return StandFaceAccept(t.X, t.Y, FaceChar(new Point(-o.X, -o.Y)));
                        break;
                    }
                }
                if (pickFrom == null)
                {
                    continue;
                }
                yield return Wait(30);
                foreach (var o in new[] { new Point(0, 1), new Point(0, -1), new Point(1, 0), new Point(-1, 0) })
                {
                    var t = new Point(target.X + o.X, target.Y + o.Y);
                    if (t != et && BotNav.Walkable(t) && !lanes.Contains(t))
                    {
                        yield return StandFaceAccept(t.X, t.Y, FaceChar(new Point(-o.X, -o.Y)));
                        break;
                    }
                }
                yield return Wait(30);
                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    System.Console.Error.WriteLine($"  laser {et.X},{et.Y} plugged: {plugged()}");
                }
            }
        }

        /// <summary>Walks screen by screen along the most permissive route (rafts, jumps) to the tile containing (gx,gy).</summary>
        protected static IEnumerable<object> GoGlobal(float gx, float gy)
        {
            var goal = BotNav.TileOf(new Vector2(gx, gy));
            bool jump = GlobalState.inventory.CanJump;
            Point lastHere = new(-1, -1);
            List<Point> plan = null;
            for (int hop = 0; hop < 80; hop++)
            {
                BotNav.ResetDustCache();
                var here = BotNav.TileOf(Pos);
                bool stalled = here == lastHere;
                lastHere = here;
                var gs = new Point(goal.X / 10, goal.Y / 10);
                if (Grid == gs)
                {
                    yield return RaftTo(gx, gy);
                    yield break;
                }
                // Stick to the plan made earlier while we are on it (re-planning every hop can flip-flop when the dust supply changes).
                List<Point> route = null;
                if (plan != null)
                {
                    int at = plan.IndexOf(here);
                    if (at >= 0)
                    {
                        route = plan.Skip(at + 1).ToList();
                    }
                }
                if (route == null || route.Count == 0)
                {
                    route = BotNav.RaftPath(here, OnConveyor(here), goal, jump);
                    plan = route == null ? null : new List<Point>(route.Prepend(here));
                }
                if (route == null)
                {
                    throw new InvalidOperationException($"bot: no route from {here} to {goal} on {Map}");
                }
                // Waypoint: the first tile on land that lies on another screen (or the end of the water run that leads there).
                Point way = route[route.Count - 1];
                for (int i = 0; i < route.Count; i++)
                {
                    var t = route[i];
                    if (new Point(t.X / 10, t.Y / 10) != Grid && !OnConveyor(t))
                    {
                        way = t;
                        break;
                    }
                }
                BotRunner.Status = $"on to {way.X},{way.Y}";
                RaftLog($"hop {hop}: from {here} to {way} (route {route.Count}) pos {Pos} grid {Grid} trans {GlobalState.ScreenTransition}");
                var wc = BotNav.CentreOf(way);
                if (stalled && new Point(way.X / 10, way.Y / 10) != Grid)
                {
                    // Standing right at a screen edge without crossing: push on in that direction.
                    var edge = way.X > here.X ? KeyFunctions.Right : way.X < here.X ? KeyFunctions.Left : way.Y > here.Y ? KeyFunctions.Down : KeyFunctions.Up;
                    yield return Hold(25, edge);
                    continue;
                }
                int wayIdx = route.IndexOf(way);
                bool wet = route.Take(wayIdx + 1).Any(t => OnConveyor(t) || BotNav.IsSpike(t));
                if (false)
                {
                }
                else
                {
                    yield return RaftTo(wc.X - BotNav.HalfW, wc.Y - BotNav.HalfH);
                    yield return Until(() => !GlobalState.ScreenTransition, 600, "screen transition");
                    yield return Wait(10);
                    if (AutoFight && GlobalState.inventory.HasBroom && Enemies().Count > 0)
                    {
                        yield return Fight(1500, soft: true);
                    }
                }
            }
            throw new TimeoutException($"bot: GoGlobal({gx},{gy}) gave up on {Map}");
        }

        /// <summary>Opens every treasure chest of the current map that is reachable (nearest first), crossing screens, rivers and spikes.</summary>
        private static readonly HashSet<System.Guid> _failedChests = new();
        private static readonly HashSet<System.Guid> _pressedButtons = new();

        protected static IEnumerable<object> OpenChests()
        {
            for (int n = 0; n < 30; n++)
            {
                var chests = AnodyneSharp.Entities.EntityManager.GetMapEntities(GlobalState.CURRENT_MAP_NAME)
                    .Where(pr => pr.Type != null && pr.Type.Name.EndsWith("TreasureChest") && !pr.Activated && pr.Frame == 2 && !_failedChests.Contains(pr.EntityID)).ToList();
                BotNav.ResetDustCache();
                bool jumpOk = GlobalState.inventory.CanJump;
                var hereT = BotNav.TileOf(Pos);
                // Buttons whose gates are still shut come first.
                var btn = AnodyneSharp.Entities.EntityManager.GetMapEntities(GlobalState.CURRENT_MAP_NAME)
                    .Where(pr => pr.Type != null && pr.Type.Name == "Button" && !_pressedButtons.Contains(pr.EntityID))
                    .Select(pr => (pr, rp: BotNav.RaftPath(hereT, OnConveyor(hereT), BotNav.TileOf(new Vector2(pr.Position.X + 3, pr.Position.Y + 2)), jumpOk)))
                    .Where(x => x.rp != null).OrderBy(x => x.rp.Count).Select(x => x.pr).FirstOrDefault();
                if (btn != null)
                {
                    _pressedButtons.Add(btn.EntityID);
                    BotRunner.Status = $"to the button at {btn.Position.X:0},{btn.Position.Y:0}";
                    yield return GoGlobal(btn.Position.X + 3, btn.Position.Y + 2);
                    yield return Wait(90);
                    continue;
                }
                (AnodyneSharp.Entities.EntityPreset pr, Point t)? best = null;
                int bestLen = int.MaxValue;
                foreach (var pr in chests)
                {
                    var t = BotNav.TileOf(new Vector2(pr.Position.X + 3, pr.Position.Y + 17));
                    var rp = BotNav.RaftPath(hereT, OnConveyor(hereT), t, jumpOk);
                    if (rp != null && rp.Count < bestLen)
                    {
                        bestLen = rp.Count;
                        best = (pr, t);
                    }
                }
                if (best == null)
                {
                    yield break;
                }
                var chest = best.Value.pr;
                BotRunner.Status = $"to the chest at {chest.Position.X:0},{chest.Position.Y:0}";
                yield return GoGlobal(chest.Position.X + 3, chest.Position.Y + 17);
                yield return InteractAt(chest.Position.X, chest.Position.Y, 'd');
                yield return Wait(150);
                yield return ReadDialogue();
                if (!chest.Activated)
                {
                    yield break; // could not open it; do not loop forever
                }
            }
        }

        /// <summary>Opens the pause menu, goes to the Map tab and takes the "return to this area's Nexus pad" warp.</summary>
        protected static IEnumerable<object> MenuReturnToPad(int tab)
        {
            yield return Tap(KeyFunctions.Pause);
            yield return Wait(20);
            for (int i = 0; i < 8; i++)
            {
                yield return Tap(KeyFunctions.Up);
                yield return Wait(4);
            }
            for (int i = 0; i < tab; i++)
            {
                yield return Tap(KeyFunctions.Down);
                yield return Wait(4);
            }
            yield return Tap(KeyFunctions.Accept);
            yield return Wait(10);
            yield return Tap(KeyFunctions.Accept);
            yield return Wait(10);
            yield return Tap(KeyFunctions.Left);
            yield return Wait(6);
            yield return Tap(KeyFunctions.Accept);
            yield return Wait(120);
        }

        private static bool OnConveyor(Point t) => BotNav.CurrentMap.GetTileClass(t).Item2 == AnodyneSharp.MapData.Tiles.CollisionEventType.CONVEYOR;

        private static bool HasRaft()
        {
            var f = typeof(Player).GetField("raft", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public);
            return f != null && f.GetValue(Player) != null;
        }

        private static bool ReachableWithJump(Point t)
        {
            BotNav.AllowJump = GlobalState.inventory.CanJump;
            var p = BotNav.FindPath(BotNav.TileOf(Pos), q => q == t, t);
            BotNav.AllowJump = false;
            return p != null;
        }

        /// <summary>Sweeps up the nearest loose Dust that is on dry land and reachable (the broom then carries it).</summary>
        protected static IEnumerable<object> PickUpDust(bool optional = false)
        {
            var o4 = new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) };
            // Loose dust on a river drifts with the current, so keep looking for a while.
            for (int attempt = 0; attempt < (optional ? 1 : 40); attempt++)
            {
                var dusts = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Dust").ToList();
                var dustTiles = new HashSet<Point>(dusts.Select(d => BotNav.TileOf(d.Position + new Vector2(8, 8))));
                foreach (var d in dusts.OrderBy(d => (d.Position - Pos).LengthSquared()))
                {
                    var dt = BotNav.TileOf(d.Position + new Vector2(8, 8));
                    if (dt == BotNav.TileOf(Pos))
                    {
                        // The raft is on the very tile we stand on (a one-tile islet): sweep it from where we are, in each direction.
                        foreach (var face in new[] { KeyFunctions.Down, KeyFunctions.Up, KeyFunctions.Left, KeyFunctions.Right })
                        {
                            yield return Hold(1, face);
                            yield return Wait(3);
                            yield return Tap(KeyFunctions.Accept);
                            yield return Wait(30);
                            if (Player.broom.dust != null)
                            {
                                yield break;
                            }
                        }
                    }
                    BotNav.Avoid = dustTiles;
                    foreach (var o in o4)
                    {
                        var t = new Point(dt.X + o.X, dt.Y + o.Y);
                        if (attempt == 0) { RaftLog($"pick candidate dust {dt} from {t}: walkable={BotNav.Walkable(t)} reach={ReachableWithJump(t)}"); }
                        if (BotNav.Walkable(t) && !dustTiles.Contains(t) && ReachableWithJump(t))
                        {
                            BotRunner.Status = $"sweeping dust at {dt.X},{dt.Y}";
                            try
                            {
                                yield return StandFaceAccept(t.X, t.Y, FaceChar(new Point(-o.X, -o.Y)));
                            }
                            finally
                            {
                                BotNav.Avoid = null;
                            }
                            yield return Wait(30);
                            yield break;
                        }
                    }
                    BotNav.Avoid = null;
                }
                yield return Wait(30);
            }
            if (!optional)
            {
                throw new InvalidOperationException("bot: no dust to pick up on " + Map + " (player " + Pos + " carrying " + (Player.broom.dust != null) + "; dust: " + string.Join(" ", BotRunner.Entities().Where(e => e.GetType().Name == "Dust").Select(e => $"{(int)e.Position.X},{(int)e.Position.Y}{(e.exists ? "" : "x")}")) + ")");
            }
        }

        /// <summary>
        /// Walks to the tile containing (gx,gy), crossing rivers on a dust raft: the dust is set down on the first water tile of the
        /// route and stepped onto; the raft then follows the player over the water (the current drifts it, so the route is replanned
        /// every step) until land is reached again.
        /// </summary>
        private static void RaftLog(string m)
        {
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
            {
                System.Console.Error.WriteLine("  raft " + m);
            }
        }

        protected static bool InRaft;

        protected static IEnumerable<object> RaftTo(float gx, float gy)
        {
            if (InRaft)
            {
                throw new InvalidOperationException("bot: nested raft request");
            }
            InRaft = true;
            try
            {
                yield return RaftToInner(gx, gy);
            }
            finally
            {
                InRaft = false;
            }
        }

        private static IEnumerable<object> RaftToInner(float gx, float gy)
        {
            var goal = BotNav.TileOf(new Vector2(gx, gy));
            bool jump = GlobalState.inventory.CanJump;
            BotNav.ResetDustCache();
            int budget = 6000;
            for (int round = 0; round < 20; round++)
            {
                var here = BotNav.TileOf(Pos);
                if (here == goal)
                {
                    yield break;
                }
                if (budget <= 0)
                {
                    throw new TimeoutException($"bot: RaftTo({gx},{gy}) made no headway from {here} on {Map}");
                }
                bool wetNow = OnConveyor(here);
                var path = BotNav.RaftPath(here, wetNow, goal, jump);
                if (path == null)
                {
                    throw new InvalidOperationException($"bot: no raft route from {here} to {goal} on {Map}");
                }
                int wi = path.FindIndex(t => OnConveyor(t) && !(jump && BotNav.NarrowWater(t)));
                if (wi < 0)
                {
                    yield return GoTo(gx, gy);
                    yield break;
                }
                RaftLog($"round {round}: from {here} (wet={wetNow}) first water {path[wi]} goal {goal}, {path.Count} tiles");
                if (!wetNow)
                {
                    var s = wi == 0 ? here : path[wi - 1];
                    var wt = path[wi];
                    if (s != here)
                    {
                        var sc = BotNav.CentreOf(s);
                        yield return GoTo(sc.X - BotNav.HalfW, sc.Y - BotNav.HalfH);
                        if (BotNav.TileOf(Pos) != s && BotNav.TileOf(Pos) != here)
                        {
                            continue; // crossed a screen or got pushed: plan again from here
                        }
                    }
                    BotRunner.Status = $"raft: dust onto the water at {wt.X},{wt.Y}";
                    if (Player.broom.dust == null)
                    {
                        yield return PickUpDust();
                    }
                    yield return StandFaceAccept(s.X, s.Y, FaceChar(new Point(wt.X - s.X, wt.Y - s.Y)));
                    yield return Wait(15);
                    var wc = BotNav.CentreOf(wt);
                    for (int i = 0; i < 90 && !(OnConveyor(BotNav.TileOf(Pos)) && HasRaft()); i++)
                    {
                        float dx = wc.X - Pos.X, dy = wc.Y - Pos.Y;
                        yield return K(Math.Abs(dx) > 2f ? (dx < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (dy < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                    }
                }
                // Ride / walk the water route one step at a time until back on land.
                bool wasWet = false;
                for (int i = 0; i < 1500; i++)
                {
                    budget--;
                    if (Talking)
                    {
                        yield return BotScript.AdvanceKeys(i);
                        continue;
                    }
                    if (GlobalState.ScreenTransition)
                    {
                        yield return K();
                        continue;
                    }
                    var cur = BotNav.TileOf(Pos);
                    if (cur == goal)
                    {
                        yield break;
                    }
                    bool wet = OnConveyor(cur);
                    wasWet |= wet;
                    if (!wet && wasWet)
                    {
                        RaftLog($"ride ends on land at {cur}");
                        if (Player.broom.dust == null && BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "Dust" && (e.Position - Pos).Length() < 40))
                        {
                            yield return PickUpDust(true);
                        }
                        break;
                    }
                    var p2 = BotNav.RaftPath(cur, wet, goal, jump);
                    if (p2 == null || p2.Count == 0)
                    {
                        RaftLog($"ride ends at {cur}: no route (wet={wet})");
                        break;
                    }
                    var c = BotNav.CentreOf(p2[0]);
                    float ddx = c.X - Pos.X, ddy = c.Y - Pos.Y;
                    if (i % 20 == 0 && i < 200)
                    {
                        RaftLog($"step i={i} cur={cur} wet={wet} raft={HasRaft()} next={p2[0]} centre={c} pos={Pos}");
                    }
                    yield return K(Math.Abs(ddx) > 2f ? (ddx < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (ddy < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                    if (!wasWet && !wet && i > 120)
                    {
                        break;
                    }
                }
            }
            if (BotNav.TileOf(Pos) != goal)
            {
                yield return GoTo(gx, gy);
            }
        }

        /// <summary>
        /// Sweeps every loose Dust on the screen into the hole(s) on it: pick a dust up from a free side, then stand beside a hole,
        /// face it and set the dust down so it falls in.
        /// </summary>
        protected static IEnumerable<object> FillHoles()
        {
            var map = BotNav.CurrentMap;
            for (int iter = 0; iter < 40; iter++)
            {
                var dusts = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Dust").ToList();
                var holes = new List<Point>();
                for (int ty = Grid.Y * 10; ty < Grid.Y * 10 + 10; ty++)
                {
                    for (int tx = Grid.X * 10; tx < Grid.X * 10 + 10; tx++)
                    {
                        if (map.GetTileClass(new Point(tx, ty)).Item2 == AnodyneSharp.MapData.Tiles.CollisionEventType.HOLE)
                        {
                            holes.Add(new Point(tx, ty));
                        }
                    }
                }
                if (dusts.Count == 0 || holes.Count == 0)
                {
                    yield break;
                }
                var dustTiles = new HashSet<Point>(dusts.Select(d => BotNav.TileOf(d.Position + new Vector2(8, 8))));
                foreach (var e in BotRunner.Entities().Where(e => e.exists && System.Text.RegularExpressions.Regex.IsMatch(e.GetType().Name, "Monster|Chest|Statue|NPC")))
                {
                    dustTiles.Add(BotNav.TileOf(e.Position + new Vector2(8, 8)));
                }
                var o4 = new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) };
                Entity chosen = null;
                Point from = default;
                foreach (var d in dusts.OrderBy(d => holes.Min(h => Math.Abs(h.X * 16 - d.Position.X) + Math.Abs(h.Y * 16 - d.Position.Y))))
                {
                    var dt = BotNav.TileOf(d.Position + new Vector2(8, 8));
                    BotNav.Avoid = dustTiles;
                    foreach (var o in o4)
                    {
                        var t = new Point(dt.X + o.X, dt.Y + o.Y);
                        if (BotNav.Walkable(t) && !dustTiles.Contains(t) && !holes.Contains(t) && BotNav.FindPath(BotNav.TileOf(Pos), q => q == t, t) != null)
                        {
                            chosen = d;
                            from = t;
                            break;
                        }
                    }
                    BotNav.Avoid = null;
                    if (chosen != null)
                    {
                        break;
                    }
                }
                if (chosen == null)
                {
                    yield break;
                }
                var ct = BotNav.TileOf(chosen.Position + new Vector2(8, 8));
                BotRunner.Status = $"sweeping dust at {ct.X},{ct.Y}";
                BotNav.Avoid = dustTiles;
                try
                {
                    yield return StandFaceAccept(from.X, from.Y, FaceChar(new Point(ct.X - from.X, ct.Y - from.Y)));
                }
                finally
                {
                    BotNav.Avoid = null;
                }
                yield return Wait(30);
                if (chosen.exists && BotNav.TileOf(chosen.Position + new Vector2(8, 8)) == ct && chosen.GetType().Name == "Dust" && BotRunner.Entities().Count(e => e.exists && e.GetType().Name == "Dust") >= dusts.Count)
                {
                    continue; // not picked up; try again (maybe another dust next time)
                }
                var otherDust = new HashSet<Point>(BotRunner.Entities().Where(e => e.exists && System.Text.RegularExpressions.Regex.IsMatch(e.GetType().Name, "^Dust$|Monster|Chest|Statue|NPC")).Select(d => BotNav.TileOf(d.Position + new Vector2(8, 8))));
                bool placed = false;
                foreach (var h in holes.OrderBy(h => Math.Abs(h.X - from.X) + Math.Abs(h.Y - from.Y)))
                {
                    foreach (var o in o4)
                    {
                        var t = new Point(h.X + o.X, h.Y + o.Y);
                        BotNav.Avoid = otherDust;
                        bool ok = BotNav.Walkable(t) && !otherDust.Contains(t) && !holes.Contains(t) && BotNav.FindPath(BotNav.TileOf(Pos), q => q == t, t) != null;
                        if (!ok)
                        {
                            BotNav.Avoid = null;
                            continue;
                        }
                        BotRunner.Status = $"dust into the hole at {h.X},{h.Y}";
                        try
                        {
                            yield return StandFaceAccept(t.X, t.Y, FaceChar(new Point(-o.X, -o.Y)));
                        }
                        finally
                        {
                            BotNav.Avoid = null;
                        }
                        yield return Wait(60);
                        placed = true;
                        break;
                    }
                    if (placed)
                    {
                        break;
                    }
                }
                if (!placed)
                {
                    yield break;
                }
            }
        }

        /// <summary>
        /// Temple screen (1,2): blocks the left laser column with dust unless it is already blocked (rooms reset on a death,
        /// and again whenever the screen is reloaded).
        /// </summary>
        protected static IEnumerable<object> TempleLaserDust()
        {
            yield return GoScreenTo(1, 2);
            bool placed() => BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "Dust" && Math.Abs(e.Position.X - 176) < 10 && Math.Abs(e.Position.Y - 336) < 10);
            if (!placed())
            {
                yield return StandFaceAccept(13, 21, 'l');
                yield return StandFaceAccept(12, 21, 'l');
                yield return Wait(200);
            }
        }

        /// <summary>
        /// Temple screen (0,1): a Shieldy patrols the row under the upper button without touching it. A broom hit from the
        /// front shoves a Shieldy 8px, so hit it from below as it passes under the button (pushing it onto the button), then
        /// hurry to the lower button; the gate opens for good once both are pressed at the same time.
        /// </summary>
        protected static IEnumerable<object> TempleButtonRoom()
        {
            yield return GoScreenTo(0, 1);
            if (!BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "Gate"))
            {
                yield break; // solved on an earlier visit: opened gates stay gone
            }
            yield return GoTo(70, 240);
            yield return Hold(1, KeyFunctions.Up);
            int lastSwing = -100;
            bool pushed = false;
            for (int i = 0; i < 2400 && !pushed; i++)
            {
                var patroller = BotRunner.Entities().FirstOrDefault(e => e.exists && e.GetType().Name == "Shieldy" && e.Hitbox.Y >= 205 && e.Hitbox.Y <= 236 && e.velocity.X != 0);
                if (Player.facing != Facing.UP)
                {
                    yield return K(KeyFunctions.Up);
                    continue;
                }
                if (patroller != null)
                {
                    int sx = patroller.Hitbox.X;
                    if (sx >= 56 && sx <= 80 && i - lastSwing > 20)
                    {
                        lastSwing = i;
                        int y0 = patroller.Hitbox.Y;
                        yield return K(KeyFunctions.Accept);
                        yield return Wait(8);
                        pushed = patroller.Hitbox.Y < y0 - 4;
                        continue;
                    }
                }
                yield return K();
            }
            BotRunner.Status = "on the lower button";
            yield return StandUntilGateOpens(112, 272);
        }

        protected static List<Entity> Enemies() =>
            BotRunner.Entities().Where(e => e.exists && e.GetType().IsDefined(typeof(EnemyAttribute), false)).ToList();

        /// <summary>Walks over any dropped hearts on the screen while health is not full (they time out, so be quick).</summary>
        protected static IEnumerable<object> CollectHearts()
        {
            for (int n = 0; n < 4; n++)
            {
                if (GlobalState.CUR_HEALTH >= GlobalState.MAX_HEALTH)
                {
                    yield break;
                }
                var heart = BotRunner.Entities().FirstOrDefault(e => e.exists && e.GetType().Name.Contains("HealthPickup"));
                if (heart == null)
                {
                    yield break;
                }
                BotRunner.Status = "collect heart";
                var ht = BotNav.TileOf(new Vector2(heart.Hitbox.Center.X - 5, heart.Hitbox.Center.Y - 6));
                for (int i = 0; i < 240 && heart.exists; i++)
                {
                    var hp = BotNav.FindPath(BotNav.TileOf(Pos), t => t == ht, ht);
                    if (hp == null || hp.Count == 0)
                    {
                        break;
                    }
                    var nc = BotNav.CentreOf(hp[0]);
                    float px = nc.X - Pos.X, py = nc.Y - Pos.Y;
                    yield return SafeMove(Math.Abs(px) > 2f ? (px < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (py < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                }
                yield return Wait(2);
            }
        }

        /// <summary>Kills every enemy on the screen with the broom (approach, face, tap Accept).</summary>
        /// <summary>
        /// The Seer (temple boss): two orbs circle it and it touches for damage, but any broom hit counts and it then flickers
        /// (immune) for 1.5s. So: hit it from below when it is vulnerable, back off while it flickers, repeat (7 hits).
        /// </summary>
        protected static IEnumerable<object> SeerFight(int timeoutFrames = 9000)
        {
            var healthField = typeof(AnodyneSharp.Entities.Enemy.Bedroom.Seer).GetField("_health", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            int lastSwing = -100;
            for (int i = 0; i < timeoutFrames; i++)
            {
                if (Talking || GlobalState.ScreenTransition)
                {
                    yield return AdvanceKeys(i);
                    continue;
                }
                var seer = BotRunner.Entities().FirstOrDefault(e => e.exists && e.GetType().Name == "Seer");
                if (seer == null)
                {
                    yield break;
                }
                int hp = healthField != null ? (int)healthField.GetValue(seer) : -1;
                BotRunner.Status = $"Seer fight: boss hp {hp}";
                var sb = seer.Hitbox;
                var pb = Player.Hitbox;
                float dx = sb.Center.X - pb.Center.X;
                int gap = pb.Top - sb.Bottom;
                bool vulnerable = !seer._flickering && seer.opacity >= 1f;
                if (seer.opacity < 1f)
                {
                    // Not in the fight yet: walk up until it notices us (it starts once we are close), then let the dialogue run.
                    yield return Player.Position.Y > seer.Position.Y + 44 ? K(KeyFunctions.Up) : K();
                    continue;
                }
                if (!vulnerable)
                {
                    // Immune: keep clear (below it) and line up for the next strike.
                    if (gap < 36 && BotNav.Walkable(BotNav.TileOf(Pos + new Vector2(0, 10))))
                    {
                        yield return K(KeyFunctions.Down);
                    }
                    else
                    {
                        yield return Math.Abs(dx) > 8 ? K(dx < 0 ? KeyFunctions.Left : KeyFunctions.Right) : K();
                    }
                    continue;
                }
                if (Math.Abs(dx) > 5)
                {
                    yield return K(dx < 0 ? KeyFunctions.Left : KeyFunctions.Right);
                    continue;
                }
                if (gap > 11)
                {
                    yield return K(KeyFunctions.Up);
                    continue;
                }
                if (Player.facing != Facing.UP)
                {
                    yield return K(KeyFunctions.Up);
                    continue;
                }
                if (i - lastSwing >= SwingCooldown)
                {
                    lastSwing = i;
                    yield return K(KeyFunctions.Accept);
                    continue;
                }
                yield return K();
            }
            throw new TimeoutException("bot: the Seer fight timed out");
        }

        protected static IEnumerable<object> Fight(int timeoutFrames = 2400, bool soft = false)
        {
            if (Enemies().Any(e => e.GetType().Name == "Seer"))
            {
                yield return SeerFight();
                yield break;
            }
            int swing = 0, lastSwing = -1000;
            var startGrid = Grid;
            Vector2 lastPos = Pos;
            int stillFrames = 0;
            int unstick = 0;
            KeyFunctions unstickKey = KeyFunctions.Down;
            Point? lastStep = null; // the next path tile we were walking towards (blamed when we stop moving)
            for (int i = 0; i < timeoutFrames; i++)
            {
                if (Talking || GlobalState.ScreenTransition)
                {
                    yield return AdvanceKeys(i);
                    continue;
                }
                if (Grid != startGrid)
                {
                    // Knocked (or walked) onto another screen: that room's enemies are not this fight's business.
                    yield break;
                }
                var enemies = Enemies();
                if (enemies.Count == 0)
                {
                    yield return CollectHearts();
                    yield break;
                }
                if (GlobalState.CUR_HEALTH <= 3)
                {
                    // Dropped hearts are worth a detour when low.
                    var heart = BotRunner.Entities().FirstOrDefault(e => e.exists && e.GetType().Name.Contains("HealthPickup"));
                    if (heart != null)
                    {
                        var ht = BotNav.TileOf(new Vector2(heart.Hitbox.Center.X - 5, heart.Hitbox.Center.Y - 6));
                        var hp = BotNav.FindPath(BotNav.TileOf(Pos), t => t == ht, ht);
                        if (hp != null && hp.Count > 0)
                        {
                            var nc = BotNav.CentreOf(hp[0]);
                            float px = nc.X - Pos.X, py = nc.Y - Pos.Y;
                            yield return SafeMove(Math.Abs(px) > 2f ? (px < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (py < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                            continue;
                        }
                    }
                }
                var me = Pos + new Vector2(5, 6);
                stillFrames = (Pos == lastPos && !Dodged) ? stillFrames + 1 : 0;
                lastPos = Pos;
                if (stillFrames > 150)
                {
                    // Wedged for no reason we know: shake loose by walking a few frames in a changing direction.
                    stillFrames = 0;
                    unstick = 24;
                    unstickKey = new[] { KeyFunctions.Down, KeyFunctions.Left, KeyFunctions.Up, KeyFunctions.Right }[(i / 150) % 4];
                }
                if (unstick > 0)
                {
                    unstick--;
                    yield return K(unstickKey);
                    continue;
                }
                if (stillFrames > 50 && lastStep.HasValue)
                {
                    BotNav.Blocked.Add(lastStep.Value);
                    if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                    {
                        System.Console.Error.WriteLine($"  fight: stuck, blocking tile {lastStep.Value} at {Pos}");
                    }
                    stillFrames = 0;
                }
                lastStep = null;
                if (GlobalState.CUR_HEALTH != _lastHp)
                {
                    if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                    {
                        string near = string.Join(" ", enemies.Select(e => $"{e.GetType().Name}@{e.Hitbox.X},{e.Hitbox.Y}({e.Hitbox.Width}x{e.Hitbox.Height})"));
                        System.Console.Error.WriteLine($"  HP {_lastHp}->{GlobalState.CUR_HEALTH} f{i} playerBox={Player.Hitbox} face={Player.facing} lanes={BotNav.LaneTiles().Count} enemies: {near} dangers: " + string.Join(" ", Dangers().Select(d => $"{d.GetType().Name}{d.Hitbox}")));
                    }
                    _lastHp = GlobalState.CUR_HEALTH;
                }
                var target = enemies.OrderBy(e => (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - me).LengthSquared()).First();
                var c = new Vector2(target.Hitbox.Center.X, target.Hitbox.Center.Y);
                float dx = c.X - me.X, dy = c.Y - me.Y;
                BotRunner.Status = $"fight {target.GetType().Name} ({enemies.Count} left)";
                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_SHOTS") is string shotDir && i % 10 == 0 && i >= int.Parse(Environment.GetEnvironmentVariable("ANODYNE_BOT_SHOTS_FROM") ?? "0") && i < int.Parse(Environment.GetEnvironmentVariable("ANODYNE_BOT_SHOTS_FROM") ?? "0") + 300)
                {
                    AnodyneSharp.Drawing.SpriteDrawer.SaveRender($"{shotDir}/fight_{i:0000}.png");
                }
                if (i % 120 == 0 && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    System.Console.Error.WriteLine($"fight {target.GetType().Name} f{i} me={me} target={c} d=({dx:0},{dy:0}) hp={GlobalState.CUR_HEALTH} enemies={enemies.Count}");
                }
                KeyFunctions dir;
                bool attack = false;
                if (target.GetType().Name == "Shieldy")
                {
                    // Only a downward swing hurts it (the broom reaches ~16px below the player). Get to a standoff two
                    // tiles above, line up in x, walk down until the gap is small, then swing facing down.
                    var tb = target.Hitbox;
                    var pb = Player.Hitbox;
                    int gap = tb.Top - pb.Bottom;
                    bool aligned = Math.Abs(dx) <= 4;
                    if (aligned && gap >= -2 && gap <= 40 && pb.Bottom <= tb.Top + 2)
                    {
                        if (gap > 10)
                        {
                            yield return K(KeyFunctions.Down);
                            continue;
                        }
                        if (Player.facing != Facing.DOWN)
                        {
                            yield return gap >= 1 ? K(KeyFunctions.Down) : K(KeyFunctions.Up);
                            continue;
                        }
                        swing++;
                        if (swing - lastSwing >= SwingCooldown)
                        {
                            lastSwing = swing;
                            yield return K(KeyFunctions.Accept);
                            continue;
                        }
                        yield return K();
                        continue;
                    }
                    if (pb.Bottom <= tb.Top - (target.velocity == Vector2.Zero && InCorridor(target) ? 0 : 2) && Math.Abs(dx) > 4 && Math.Abs(dx) < 40)
{
                        // Standing clear above it: slide sideways to line up.
                        yield return K(dx < 0 ? KeyFunctions.Left : KeyFunctions.Right);
                        continue;
                    }
                    if (target.velocity == Vector2.Zero && InCorridor(target) && pb.Top > tb.Bottom && pb.Top - tb.Bottom < 28 && pb.Right > tb.Left - 3 && pb.Left < tb.Right + 3)
                    {
                        // Approaching from below and lined up with it (a Shieldy in a corridor): slide to the free side first, since
                        // walking past its flank on the way up is what gets us hit.
                        var here = BotNav.TileOf(Pos);
                        var side = (tb.Center.X - pb.Center.X >= -6) ? KeyFunctions.Left : KeyFunctions.Right;
                        var sideTile = new Point(here.X + (side == KeyFunctions.Left ? -1 : 1), here.Y);
                        if (!BotNav.Walkable(sideTile))
                        {
                            side = side == KeyFunctions.Left ? KeyFunctions.Right : KeyFunctions.Left;
                        }
                        yield return K(side);
                        continue;
                    }
                    var et = BotNav.TileOf(new Vector2(c.X - 5, c.Y - 6));
                    List<Point> sp = null;
                    var lanes = BotNav.LaneTiles();
                    bool acted = false;
                    foreach (var pd in new[] { new Point(0, -1), new Point(-1, 0), new Point(1, 0) })
                    {
                        // A beam runs along the tile next to it: shove it into the beam from the far side (a front hit moves it 8px).
                        if (!lanes.Contains(new Point(et.X + pd.X, et.Y + pd.Y)) || lanes.Contains(new Point(et.X - pd.X, et.Y - pd.Y)) ||
                            !BotNav.Walkable(new Point(et.X - 2 * pd.X, et.Y - 2 * pd.Y)))
                        {
                            continue;
                        }
                        bool vert = pd.X == 0;
                        var key = pd.Y < 0 ? KeyFunctions.Up : pd.Y > 0 ? KeyFunctions.Down : pd.X < 0 ? KeyFunctions.Left : KeyFunctions.Right;
                        var face = pd.Y < 0 ? Facing.UP : pd.Y > 0 ? Facing.DOWN : pd.X < 0 ? Facing.LEFT : Facing.RIGHT;
                        // Signed gap between us and it along the push axis, measured from the far side (positive = clear of it).
                        int gapb = pd.Y < 0 ? pb.Top - tb.Bottom : pd.Y > 0 ? tb.Top - pb.Bottom : pd.X < 0 ? pb.Left - tb.Right : tb.Left - pb.Right;
                        float across = vert ? dx : dy;
                        if (Math.Abs(across) <= 4 && gapb >= -2 && gapb <= 40)
                        {
                            if (gapb > 10 || Player.facing != face)
                            {
                                yield return K(key);
                                acted = true;
                                break;
                            }
                            swing++;
                            if (swing - lastSwing >= SwingCooldown)
                            {
                                lastSwing = swing;
                                yield return K(KeyFunctions.Accept);
                                acted = true;
                                break;
                            }
                            yield return K();
                            acted = true;
                                break;
                        }
                        var behind = new Point(et.X - 2 * pd.X, et.Y - 2 * pd.Y);
                        List<Point> bp = null;
                        foreach (int margin in new[] { 20, 12, 5 })
                        {
                            BotNav.Avoid = BotNav.NearEnemies(margin, target);
                            bp = BotNav.FindPath(BotNav.TileOf(Pos), t => t == behind, behind, strict: true);
                            BotNav.Avoid = null;
                            if (bp != null)
                            {
                                break;
                            }
                        }
                        if (bp != null && bp.Count > 0)
                        {
                            lastStep = bp[0];
                            var nc = BotNav.CentreOf(bp[0]) + new Vector2(5, 6);
                            float px = nc.X - me.X, py = nc.Y - me.Y;
                            yield return SafeMove(StepKey(px, py, target), target);
                            acted = true;
                            break;
                        }
                    }
                    if (acted)
                    {
                        continue;
                    }
                    foreach (int up in new[] { 2, 1 })
                    {
                        var standoff = new Point(et.X, et.Y - up);
                        if (lanes.Contains(standoff))
                        {
                            continue; // a laser sweeps that row: let it do the work instead
                        }
                        // Widest berth first, but not at the price of a huge detour (a Shieldy in a corridor: squeeze past it).
                        var cands = new List<List<Point>>();
                        foreach (int margin in new[] { 5, 1, -3 })
                        {
                            BotNav.Avoid = BotNav.NearEnemies(margin);
                            var cp = BotNav.FindPath(BotNav.TileOf(Pos), t => t == standoff, standoff, strict: true);
                            BotNav.Avoid = null;
                            if (cp != null)
                            {
                                cands.Add(cp);
                            }
                        }
                        if (cands.Count > 0)
                        {
                            int shortest = cands.Min(x => x.Count);
                            sp = Environment.GetEnvironmentVariable("ANODYNE_NO_CHOOSER") == null ? cands.First(x => x.Count <= shortest + 25) : cands[0];
                        }
                        if (sp != null)
                        {
                            break;
                        }
                    }
                    if (sp != null && sp.Count > 0)
                    {
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null && i % 120 == 0)
                        {
                            System.Console.Error.WriteLine($"  shieldy plan: me {BotNav.TileOf(Pos)} enemy {et} -> {sp.Count} tiles: {string.Join(">", sp.Take(10).Select(q => $"{q.X},{q.Y}"))}");
                        }
                        lastStep = sp[0];
                        var nc = BotNav.CentreOf(sp[0]) + new Vector2(5, 6);
                        float px = nc.X - me.X, py = nc.Y - me.Y;
                        yield return SafeMove(StepKey(px, py, target), target);
                        continue;
                    }
                    if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null && i % 120 == 0)
                    {
                        System.Console.Error.WriteLine($"  shieldy: no standoff path: me tile {BotNav.TileOf(Pos)} enemy tile {et} walkable above1={BotNav.Walkable(new Point(et.X, et.Y - 1))} above2={BotNav.Walkable(new Point(et.X, et.Y - 2))} dx={dx:0} dy={dy:0}");
                    }
                    if (lanes.Count > 0)
                    {
                        // No attack spot outside the beams: step out of any lane and let the lasers kill it.
                        var here = BotNav.TileOf(Pos);
                        if (lanes.Contains(here))
                        {
                            BotNav.Avoid = new HashSet<Point>(lanes);
                            var op = BotNav.FindPath(here, t => !lanes.Contains(t), here);
                            BotNav.Avoid = null;
                            if (op != null && op.Count > 0)
                            {
                                var nc = BotNav.CentreOf(op[0]) + new Vector2(5, 6);
                                float px = nc.X - me.X, py = nc.Y - me.Y;
                                yield return K(Math.Abs(px) > 2f ? (px < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (py < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                                continue;
                            }
                        }
                        yield return K();
                        continue;
                    }
                    yield return K(KeyFunctions.Up);
                    continue;
                }
                if (dx * dx + dy * dy > 30 * 30)
                {
                    // Far away: follow the tile path (walls between us), re-planned every few frames.
                    var tt = BotNav.TileOf(new Vector2(c.X - 5, c.Y - 6));
                    var path = BotNav.FindPath(BotNav.TileOf(Pos), t => t == tt, tt);
                    if (path != null && path.Count > 0)
                    {
                        lastStep = path[0];
                        var nc = BotNav.CentreOf(path[0]) + new Vector2(5, 6);
                        float px = nc.X - me.X, py = nc.Y - me.Y;
                        dir = Math.Abs(px) > 2f ? (px < 0 ? KeyFunctions.Left : KeyFunctions.Right)
                                                : (py < 0 ? KeyFunctions.Up : KeyFunctions.Down);
                        if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && i % 10 == 0)
                        {
                            System.Console.Error.WriteLine($"  far f{i} me={me} step={path[0]} px={px:0.0} py={py:0.0} dir={dir} path={path.Count}");
                        }
                        yield return K(dir);
                        continue;
                    }
                }
                if (target.GetType().Name == "Annoyer")
                {
                    // Flies over walls and darts about: no path, just face it along the dominant axis and swing when close
                    // (the broom is wide enough to tolerate a few px of misalignment).
                    bool horiz = Math.Abs(dx) >= Math.Abs(dy);
                    float along = horiz ? dx : dy, across = horiz ? dy : dx;
                    var face = horiz ? (dx < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (dy < 0 ? KeyFunctions.Up : KeyFunctions.Down);
                    swing++;
                    if (Math.Abs(along) < 22 && Math.Abs(across) < 11 && swing - lastSwing >= SwingCooldown)
                    {
                        lastSwing = swing;
                        yield return K(face, KeyFunctions.Accept);
                    }
                    else if (Math.Abs(across) > 6 && Math.Abs(along) < 40)
                    {
                        yield return K(horiz ? (dy < 0 ? KeyFunctions.Up : KeyFunctions.Down) : (dx < 0 ? KeyFunctions.Left : KeyFunctions.Right));
                    }
                    else
                    {
                        yield return K(face);
                    }
                    continue;
                }
                if (Math.Abs(dx) >= Math.Abs(dy))
                {
                    // Line up vertically first, then close in horizontally.
                    if (Math.Abs(dy) > 5)
                    {
                        dir = dy < 0 ? KeyFunctions.Up : KeyFunctions.Down;
                    }
                    else
                    {
                        dir = dx < 0 ? KeyFunctions.Left : KeyFunctions.Right;
                        attack = Math.Abs(dx) < AttackReach;
                    }
                }
                else
                {
                    if (Math.Abs(dx) > 5)
                    {
                        dir = dx < 0 ? KeyFunctions.Left : KeyFunctions.Right;
                    }
                    else
                    {
                        dir = dy < 0 ? KeyFunctions.Up : KeyFunctions.Down;
                        attack = Math.Abs(dy) < AttackReach;
                    }
                }
                swing++;
                if (attack && swing - lastSwing >= SwingCooldown)
                {
                    lastSwing = swing;
                    yield return K(dir, KeyFunctions.Accept);
                }
                else
                {
                    yield return K(dir);
                }
            }
            if (!soft)
            {
                throw new TimeoutException($"bot: Fight timed out on {Map} screen {Grid}");
            }
        }
    }
}
