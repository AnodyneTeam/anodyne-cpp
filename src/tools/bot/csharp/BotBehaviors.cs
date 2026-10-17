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
                    case 'l':
                    {
                        // l<col>,<rowStart>,<rowEnd>: walk UP a beam column towards its emitter, leapfrogging two dusts (see LeapfrogUp)
                        var a = tok.Substring(1).Split(',');
                        yield return LeapfrogUp(int.Parse(a[0]), int.Parse(a[1]), int.Parse(a[2]));
                        break;
                    }
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
                    case 'w':
                    {
                        // dev: w<MAP>:<x>,<y> warps the player straight into a map position (quick tests of a problem screen from a copied save)
                        var a = tok.Substring(1).Split(':');
                        var xy = a[1].Split(',');
                        GlobalState.NEXT_MAP_NAME = a[0];
                        GlobalState.PLAYER_WARP_TARGET = new Vector2(float.Parse(xy[0]), float.Parse(xy[1]));
                        GlobalState.WARP = true;
                        yield return Wait(200);
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
            InRaft = false; // a death or failure mid-raft abandons the iterator without running its finally
            Plugging = false;
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
                yield return Fight(3600, soft: true);
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
        public static int LaserMargin = 1;
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

        // ---- space-time danger prediction ----
        /// <summary>Frames of the future that are predicted (the plans below look this far ahead).</summary>
        private const int Horizon = 90;

        /// <summary>Where a danger will be, frame by frame (null = gone / not a danger at that frame).</summary>
        private sealed class Threat
        {
            public Rectangle?[] At = new Rectangle?[Horizon + 1];
            public int Margin = 2;
            public string Name = "";
            public Entity Source;
            public bool Stationary;
        }

        private static readonly System.Reflection.FieldInfo PewTimerField =
            typeof(AnodyneSharp.Entities.Enemy.Bedroom.PewLaser).GetField("_bulletTimer", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        private static readonly System.Reflection.FieldInfo PewPoolField =
            typeof(AnodyneSharp.Entities.Enemy.Bedroom.PewLaser).GetField("lasers", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        private static readonly System.Reflection.FieldInfo LaserFastField =
            typeof(AnodyneSharp.Entities.Enemy.Bedroom.PewLaser.Laser).GetField("_isFast", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);

        /// <summary>True if the floor above a vertically patrolling Shieldy is free of beams (we float up there).</summary>
        private static bool Target_ColumnAboveClear(Entity e)
        {
            var lanes = BotNav.LaneTiles();
            var t = BotNav.TileOf(new Vector2(e.Hitbox.Center.X, e.Hitbox.Top));
            for (int k = 1; k <= 4; k++)
            {
                if (lanes.Contains(new Point(t.X, t.Y - k)))
                {
                    return false;
                }
            }
            return true;
        }

        /// <summary>The y at which the player's hitbox floats 3px above a sideways-patrolling Shieldy.</summary>
        private static int? ShieldyHoverY(Entity shieldy)
        {
            int y = shieldy.Hitbox.Top - 15;
            var lanes = BotNav.LaneTiles();
            if (lanes.Count > 0)
            {
                // A beam sweeps the floor above its line: hovering there is no option (the beam does the work, see the shove code).
                for (int x = shieldy.Hitbox.Left - 40; x <= shieldy.Hitbox.Right + 40; x += 8)
                {
                    if (lanes.Contains(BotNav.TileOf(new Vector2(x, y))) || lanes.Contains(BotNav.TileOf(new Vector2(x, y + 11))))
                    {
                        return null;
                    }
                }
            }
            return y;
        }

        private static bool CornersWalkable(Rectangle r)
        {
            // Pixel -> tile directly (BotNav.TileOf expects a hitbox top-left and adds half the hitbox size, which is not what a corner is).
            static Point Px(float x, float y) => new((int)Math.Floor(x / 16f), (int)Math.Floor(y / 16f));
            return BotNav.Walkable(Px(r.Left, r.Top)) && BotNav.Walkable(Px(r.Right - 1, r.Top)) &&
                   BotNav.Walkable(Px(r.Left, r.Bottom - 1)) && BotNav.Walkable(Px(r.Right - 1, r.Bottom - 1));
        }

        /// <summary>A Shieldy walks 30px/s in a straight line and turns round when it touches a wall: replay that.</summary>
        private static Threat PredictPatroller(Entity e)
        {
            var t = new Threat { Name = e.GetType().Name, Source = e, Stationary = e.velocity == Vector2.Zero, Margin = e.velocity == Vector2.Zero ? 0 : 2 };
            var r = e.Hitbox;
            var v = e.velocity;
            float fx = 0, fy = 0; // sub-pixel remainder
            for (int f = 0; f <= Horizon; f++)
            {
                t.At[f] = r;
                if (v == Vector2.Zero)
                {
                    continue;
                }
                fx += v.X / 60f;
                fy += v.Y / 60f;
                int mx = (int)fx, my = (int)fy;
                fx -= mx;
                fy -= my;
                var n = r;
                n.Offset(mx, my);
                if (!CornersWalkable(n))
                {
                    v = -v;
                    fx = fy = 0;
                }
                else
                {
                    r = n;
                }
            }
            return t;
        }

        /// <summary>The bullets an emitter has not fired yet: it shoots every 31 frames, from the timer's real phase.</summary>
        /// <summary>True if the point is on an open beam tile or on an emitter's own tile (a bullet spends its first frames there).</summary>
        private static bool BeamTile(Dictionary<Point, int> lanes, Vector2 p)
        {
            var t = BotNav.TileOf(p);
            if (lanes.ContainsKey(t))
            {
                return true;
            }
            foreach (var e in BotRunner.Entities())
            {
                if (e.exists && e.GetType().Name == "PewLaser" && BotNav.TileOf(e.Position + new Vector2(8, 8)) == t)
                {
                    return true;
                }
            }
            return false;
        }

        private static IEnumerable<Threat> PredictEmitter(Entity em, Dictionary<Point, int> lanes)
        {
            var emThreat = new Threat { Name = "PewLaser", Margin = 1 };
            for (int f = 0; f <= Horizon; f++)
            {
                emThreat.At[f] = em.Hitbox;
            }
            yield return emThreat;
            if (PewTimerField == null || PewPoolField == null)
            {
                yield break;
            }
            var pool = (PewPoolField.GetValue(em) as System.Collections.Generic.IEnumerable<Entity>)
                ?? (PewPoolField.GetValue(em)?.GetType().GetProperty("Entities")?.GetValue(PewPoolField.GetValue(em)) as System.Collections.Generic.IEnumerable<Entity>);
            var sample = pool?.FirstOrDefault();
            if (sample == null)
            {
                yield break;
            }
            bool fast = LaserFastField != null && LaserFastField.GetValue(sample) is bool b && b;
            float speed = fast ? 70f : 40f;
            float timer = (float)PewTimerField.GetValue(em);
            int first = (int)Math.Floor(Math.Max(timer, 0f) * 60f) + 1;
            var dir = em.facing switch { Facing.RIGHT => new Vector2(1, 0), Facing.LEFT => new Vector2(-1, 0), Facing.UP => new Vector2(0, -1), _ => new Vector2(0, 1) };
            var spawnOffset = em.facing switch
            {
                Facing.DOWN => new Vector2(0, 11),
                Facing.UP => new Vector2(0, 8),
                Facing.LEFT => new Vector2(8, 0),
                _ => Vector2.Zero
            };
            var spawnPos = em.Position + spawnOffset;
            var box = sample.Hitbox;
            var boxAtSpawn = new Rectangle((int)(box.X + (spawnPos.X - sample.Position.X)), (int)(box.Y + (spawnPos.Y - sample.Position.Y)), box.Width, box.Height);
            for (int s = first; s <= Horizon; s += 31)
            {
                var bt = new Threat { Name = "Laser(future)", Margin = LaserMargin };
                for (int f = s; f <= Horizon; f++)
                {
                    var off = dir * speed * (f - s) / 60f;
                    var r = boxAtSpawn;
                    r.Offset((int)off.X, (int)off.Y);
                    // The bullet is gone once it reaches a wall (or a dust plug): lanes only holds the open beam tiles.
                    if (lanes.Count > 0 && !BeamTile(lanes, new Vector2(r.Center.X - 5, r.Center.Y - 6)))
                    {
                        break;
                    }
                    bt.At[f] = r;
                }
                yield return bt;
            }
        }

        private static List<Threat> PredictThreats(Entity except)
        {
            var result = new List<Threat>();
            var lanes = BotNav.LaneAxes();
            foreach (var d in Dangers(except))
            {
                string name = d.GetType().Name;
                if (name == "PewLaser")
                {
                    result.AddRange(PredictEmitter(d, lanes));
                    continue;
                }
                if (name == "Shieldy")
                {
                    result.Add(PredictPatroller(d));
                    continue;
                }
                bool bolt = name == "Laser";
                if (bolt && d.velocity == Vector2.Zero)
                {
                    continue; // a bullet that has hit something is poofing out: harmless (and standing still, it would look like a wall)
                }
                bool still = d.velocity == Vector2.Zero && d.GetType().IsDefined(typeof(AnodyneSharp.Entities.Enemy.EnemyAttribute), false);
                var t = new Threat { Name = name, Margin = bolt ? LaserMargin : (still ? 0 : 2), Source = d, Stationary = still };
                for (int f = 0; f <= Horizon; f++)
                {
                    var r = d.Hitbox;
                    r.Offset((int)(d.velocity.X * f / 60f), (int)(d.velocity.Y * f / 60f));
                    if (bolt && lanes.Count > 0 && d.GetType().DeclaringType == typeof(AnodyneSharp.Entities.Enemy.Bedroom.PewLaser) &&
                        !BeamTile(lanes, new Vector2(r.Center.X - 5, r.Center.Y - 6)))
                    {
                        break;
                    }
                    t.At[f] = r;
                }
                result.Add(t);
            }
            return result;
        }

        /// <summary>
        /// First frame (1..) at which the plan runs the player into a predicted danger, or int.MaxValue if it is clear.
        /// The plan: hold `first` for `firstFrames`, then `then` for `thenFrames`.
        /// </summary>
        private static Threat _lastHitThreat;

        private static int PlanHitFrame(KeyFunctions first, int firstFrames, KeyFunctions then, int thenFrames, List<Threat> threats)
        {
            _lastHitThreat = null;
            var pos = new Vector2(Player.Hitbox.X, Player.Hitbox.Y);
            var pb = Player.Hitbox;
            var v1 = DirVector(first) * WalkPxPerFrame;
            var v2 = DirVector(then) * WalkPxPerFrame;
            int total = Math.Min(firstFrames + thenFrames, Horizon);
            for (int f = 1; f <= total; f++)
            {
                pos += f <= firstFrames ? v1 : v2;
                var mine = new Rectangle((int)pos.X, (int)pos.Y, pb.Width, pb.Height);
                foreach (var th in threats)
                {
                    var r = th.At[f];
                    if (r == null)
                    {
                        continue;
                    }
                    var rr = r.Value;
                    rr.Inflate(th.Margin, th.Margin);
                    if (mine.Intersects(rr))
                    {
                        _lastHitThreat = th;
                        return f;
                    }
                }
            }
            return int.MaxValue;
        }

        private static bool WalkableAhead(KeyFunctions dir, int frames)
        {
            var v = DirVector(dir) * WalkPxPerFrame * frames;
            var pb = Player.Hitbox;
            var r = new Rectangle((int)(pb.X + v.X), (int)(pb.Y + v.Y), pb.Width, pb.Height);
            return CornersWalkable(r);
        }

        /// <summary>
        /// Turns a wanted movement key into a safe frame. Looks ~1.5s ahead at where every danger will be (moving bullets, the
        /// shots an emitter has yet to fire, Shieldy patrols) and takes the first plan that gets through: go now, stand and wait
        /// for the gap, or sidestep first. `except` = an enemy we are deliberately closing in on; `dist` = how far (px) the step goes.
        /// </summary>
        /// <summary>Set by SafeMove when the wanted step runs into an enemy that does not move: waiting never helps, the caller must go round or kill it.</summary>
        public static Entity BlockedBy;

        /// <summary>The first frame at which walking the waypoints (hitbox top-left targets, one axis at a time) meets a predicted danger.</summary>
        private static int PathPlanHit(List<Vector2> waypoints, List<Threat> threats)
        {
            var pb = Player.Hitbox;
            var pos = new Vector2(pb.X, pb.Y);
            int f = 0;
            foreach (var wp in waypoints)
            {
                for (int guard = 0; guard < 80 && f < Horizon && (Math.Abs(wp.X - pos.X) > 1f || Math.Abs(wp.Y - pos.Y) > 1f); guard++)
                {
                    f++;
                    var d = Math.Abs(wp.X - pos.X) > 1f ? new Vector2(Math.Sign(wp.X - pos.X), 0) : new Vector2(0, Math.Sign(wp.Y - pos.Y));
                    pos += d * Math.Min(WalkPxPerFrame, Math.Abs(wp.X - pos.X) > 1f ? Math.Abs(wp.X - pos.X) : Math.Abs(wp.Y - pos.Y));
                    var mine = new Rectangle((int)pos.X, (int)pos.Y, pb.Width, pb.Height);
                    foreach (var th in threats)
                    {
                        var r = th.At[f];
                        if (r == null)
                        {
                            continue;
                        }
                        var rr = r.Value;
                        rr.Inflate(th.Margin, th.Margin);
                        if (mine.Intersects(rr))
                        {
                            return f;
                        }
                    }
                }
            }
            return int.MaxValue;
        }

        private static int _stDumps;

        /// <summary>Writes the planner's inputs (player, goal, predicted dangers, nearby floor) so the case can be replayed in planner_test.</summary>
        private static void DumpSpaceTimeFixture(string dir, Vector2 goal, List<Threat> threats)
        {
            System.IO.Directory.CreateDirectory(dir);
            var sb = new System.Text.StringBuilder();
            var pb = Player.Hitbox;
            sb.AppendLine($"player {pb.X} {pb.Y} {pb.Width} {pb.Height}");
            sb.AppendLine($"goal {goal.X} {goal.Y}");
            sb.AppendLine($"horizon {Horizon}");
            foreach (var t in threats)
            {
                sb.AppendLine($"threat {t.Name} {t.Margin} {(t.Stationary ? 1 : 0)}");
                for (int f = 0; f <= Horizon; f++)
                {
                    var r = t.At[f];
                    sb.AppendLine(r == null ? "-" : $"{r.Value.X} {r.Value.Y} {r.Value.Width} {r.Value.Height}");
                }
            }
            var here = BotNav.TileOf(Pos);
            sb.AppendLine($"floor {here.X - 15} {here.Y - 15} 31 31");
            for (int ty = here.Y - 15; ty <= here.Y + 15; ty++)
            {
                for (int tx = here.X - 15; tx <= here.X + 15; tx++)
                {
                    sb.Append(BotNav.Walkable(new Point(tx, ty)) ? '.' : '#');
                }
                sb.AppendLine();
            }
            System.IO.File.WriteAllText(System.IO.Path.Combine(dir, $"st_{BotRunner.Frame}_{_stDumps++}.txt"), sb.ToString());
        }

        private static int _stCache = -1, _stCacheFrame = -1;
        private static Vector2 _stCacheGoal;

        private static readonly KeyFunctions[][] StActions =
        {
            new KeyFunctions[0],
            new[] { KeyFunctions.Up }, new[] { KeyFunctions.Down }, new[] { KeyFunctions.Left }, new[] { KeyFunctions.Right },
            new[] { KeyFunctions.Up, KeyFunctions.Right }, new[] { KeyFunctions.Up, KeyFunctions.Left },
            new[] { KeyFunctions.Down, KeyFunctions.Right }, new[] { KeyFunctions.Down, KeyFunctions.Left },
        };

        /// <summary>Keys for the next frame of a plan to `goal` (hitbox top-left) that never meets a predicted danger; null if none (see SpaceTime).</summary>
        private static KeyFunctions[] SpaceTimeMove(Vector2 goal, List<Threat> threats)
        {
            var st = new List<StThreat>(threats.Count);
            foreach (var t in threats)
            {
                st.Add(new StThreat { At = t.At, Margin = t.Margin, Stationary = t.Stationary });
            }
            // The plan is good for a few frames: re-search every 3rd frame only (a search costs milliseconds).
            if (_stCacheFrame >= 0 && BotRunner.Frame - _stCacheFrame < 3 && BotRunner.Frame - _stCacheFrame >= 0 && (goal - _stCacheGoal).Length() < 40f)
            {
                return _stCache < 0 ? null : StActions[_stCache];
            }
            int a = SpaceTime.Plan(Player.Hitbox, goal, st, CornersWalkable, Horizon);
            if (a < 0)
            {
                // Doomed with the comfortable margins: settle for exact geometry (a bullet is exactly its rectangle).
                foreach (var t in st)
                {
                    t.Margin = 0;
                }
                a = SpaceTime.Plan(Player.Hitbox, goal, st, CornersWalkable, Horizon);
            }
            if (Environment.GetEnvironmentVariable("ANODYNE_ST_RANGE") is string rng && Environment.GetEnvironmentVariable("ANODYNE_ST_DUMP") is string rdir)
            {
                var parts = rng.Split('-');
                if (BotRunner.Frame >= long.Parse(parts[0]) && BotRunner.Frame <= long.Parse(parts[1]))
                {
                    System.Console.Error.WriteLine($"  spacetime f{BotRunner.Frame} -> action {a} at {Player.Hitbox} goal {goal}");
                    DumpSpaceTimeFixture(rdir, goal, threats);
                }
            }
            if (a < 0 && Environment.GetEnvironmentVariable("ANODYNE_ST_DUMP") is string dumpDir && _stDumps < 60 && BotRunner.Frame % 15 == 0)
            {
                DumpSpaceTimeFixture(dumpDir, goal, threats);
            }
            _stCache = a;
            _stCacheFrame = (int)BotRunner.Frame;
            _stCacheGoal = goal;
            return a < 0 ? null : StActions[a];
        }

        /// <summary>Waiting in place is only fine if no danger will reach us: otherwise step to a neighbouring spot where none will.</summary>
        protected static KeyFunctions[] StandSafe(Entity except)
        {
            var threats = PredictThreats(except);
            if (threats.Count == 0 || PlanHitFrameStanding(Horizon, KeyFunctions.Right, 0, threats) == int.MaxValue)
            {
                return K();
            }
            foreach (var k in new[] { KeyFunctions.Left, KeyFunctions.Right, KeyFunctions.Up, KeyFunctions.Down })
            {
                if (WalkableAhead(k, 8) && PlanHitFrame(k, 8, k, 0, threats) == int.MaxValue)
                {
                    return K(k);
                }
            }
            return K();
        }

        protected static KeyFunctions[] SafeMove(KeyFunctions wanted, Entity except = null, float dist = 40f, bool stopForBlockers = false, List<Vector2> look = null)
        {
            Dodged = false;
            BlockedBy = null;
            if (!DodgeEnabled)
            {
                return K(wanted);
            }
            var dangers = Dangers(except);
            if (dangers.Count == 0)
            {
                _dodgeStreak = Math.Max(0, _dodgeStreak - 2);
                return K(wanted);
            }
            var threats = PredictThreats(except);
            if (look != null && look.Count > 0 && Environment.GetEnvironmentVariable("ANODYNE_NO_SPACETIME") == null && PathPlanHit(look, threats) != int.MaxValue)
            {
                // Walking the planned route as it is would meet something that moves (a patroller in a one-tile corridor, a beam):
                // plan the dash through space and time instead of just the next step.
                var stl = SpaceTimeMove(look[look.Count - 1], threats);
                if (Environment.GetEnvironmentVariable("ANODYNE_ST_TRACE") != null)
                {
                    System.Console.Error.WriteLine($"  spacetime from {Player.Hitbox} to {look[look.Count - 1]} wanted={wanted}: " + (stl == null ? "none" : stl.Length == 0 ? "wait" : string.Join("+", stl)));
                }
                if (stl != null)
                {
                    Dodged = stl.Length == 0 || stl.Length > 1 || stl[0] != wanted;
                    return stl.Length == 0 ? K() : K(stl);
                }
            }
            // Frames of walking after the wait: only as far as the step we are taking (the next waypoint), not past it.
            int Run = Math.Clamp((int)Math.Ceiling(dist / WalkPxPerFrame) + 3, 6, 40);
            if (PlanHitFrame(wanted, 0, wanted, Run, threats) == int.MaxValue)
            {
                _dodgeStreak = Math.Max(0, _dodgeStreak - 2);
                return K(wanted);
            }
            if (_lastHitThreat != null && !_lastHitThreat.Stationary && Environment.GetEnvironmentVariable("ANODYNE_NO_SPACETIME") == null)
            {
                // The straight way would be hit by something that moves: search for a way through (diagonals, waiting, going round).
                var pbx = Player.Hitbox;
                var goalTL = new Vector2(pbx.X, pbx.Y) + DirVector(wanted) * Math.Max(dist, 20f);
                var st = SpaceTimeMove(goalTL, threats);
                if (st != null)
                {
                    Dodged = st.Length == 0 || st[0] != wanted || st.Length > 1;
                    return st.Length == 0 ? K() : K(st);
                }
            }
            if (stopForBlockers && _lastHitThreat != null && _lastHitThreat.Stationary && _lastHitThreat.Source != null && BlockerBudget > 0)
            {
                // An enemy that stands still blocks the way: no gap will ever open. Stop and let the walker replan or kill it.
                BlockedBy = _lastHitThreat.Source;
                Dodged = true;
                return K();
            }
            // Waiting forever for a gap that never comes would stall the route: after a long while accept the risk and go.
            if (++_dodgeStreak > 240)
            {
                if (_dodgeStreak > 300)
                {
                    _dodgeStreak = 0;
                }
                return K(wanted);
            }
            Dodged = true;
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null && _dodgeStreak % 40 == 1)
            {
                System.Console.Error.WriteLine($"  dodge(streak {_dodgeStreak}): wanted {wanted} at {Player.Hitbox} threats: " + string.Join(" ", threats.Where(t => t.At[0] != null).Select(t => $"{t.Name}{t.At[0]}")));
            }
            KeyFunctions[] sides = (wanted == KeyFunctions.Up || wanted == KeyFunctions.Down)
                ? new[] { KeyFunctions.Left, KeyFunctions.Right }
                : new[] { KeyFunctions.Up, KeyFunctions.Down };
            var back = wanted switch { KeyFunctions.Up => KeyFunctions.Down, KeyFunctions.Down => KeyFunctions.Up, KeyFunctions.Left => KeyFunctions.Right, _ => KeyFunctions.Left };
            // (a) stand still for w frames, then go: the gap-waiting plan (standing must itself be safe).
            // (b) sidestep for k frames, then go: slip into a neighbouring lane.
            // (c) back off a little, then go.
            int bestFrame = -1;
            KeyFunctions bestKey = KeyFunctions.Up;
            bool haveBest = false;
            void Consider(KeyFunctions first, int ff, bool stand)
            {
                if (!stand && !WalkableAhead(first, ff))
                {
                    return;
                }
                int hit = stand ? PlanHitFrameStanding(ff, wanted, Run, threats) : PlanHitFrame(first, ff, wanted, Run, threats);
                if (hit == int.MaxValue)
                {
                    bestFrame = int.MaxValue;
                    bestKey = stand ? KeyFunctions.Up : first;
                    haveBest = true;
                    _standing = stand;
                    return;
                }
                if (!haveBest || hit > bestFrame)
                {
                    bestFrame = hit;
                    bestKey = stand ? KeyFunctions.Up : first;
                    haveBest = true;
                    _standing = stand;
                }
            }
            // Standing and waiting only makes sense for things that move: a stationary enemy never leaves a gap.
            bool hitStationary = _lastHitThreat != null && _lastHitThreat.Stationary;
            for (int w = 4; w <= 60 && !hitStationary && bestFrame != int.MaxValue; w += 4)
            {
                Consider(KeyFunctions.Up, w, true);
            }
            foreach (var sd in sides)
            {
                for (int k = 6; k <= 18 && bestFrame != int.MaxValue; k += 6)
                {
                    Consider(sd, k, false);
                }
            }
            if (bestFrame != int.MaxValue)
            {
                for (int k = 6; k <= 18 && bestFrame != int.MaxValue; k += 6)
                {
                    Consider(back, k, false);
                }
            }
            if (!haveBest)
            {
                return K();
            }
            return _standing ? K() : K(bestKey);
        }

        private static bool _standing;

        /// <summary>How many times the walker may still stop for a stationary blocker (then it takes its chances, as before).</summary>
        public static int BlockerBudget = 6;

        private static int PlanHitFrameStanding(int waitFrames, KeyFunctions then, int thenFrames, List<Threat> threats)
        {
            var pb = Player.Hitbox;
            var pos = new Vector2(pb.X, pb.Y);
            var v2 = DirVector(then) * WalkPxPerFrame;
            int total = Math.Min(waitFrames + thenFrames, Horizon);
            for (int f = 1; f <= total; f++)
            {
                if (f > waitFrames)
                {
                    pos += v2;
                }
                var mine = new Rectangle((int)pos.X, (int)pos.Y, pb.Width, pb.Height);
                foreach (var th in threats)
                {
                    var r = th.At[f];
                    if (r == null)
                    {
                        continue;
                    }
                    var rr = r.Value;
                    rr.Inflate(th.Margin, th.Margin);
                    if (mine.Intersects(rr))
                    {
                        return f;
                    }
                }
            }
            return int.MaxValue;
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
        protected static IEnumerable<object> BlockLasers(Func<Entity, bool> only = null)
        {
            var emitters = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "PewLaser" && (only == null || only(e))).ToList();
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
                BotRunner.Status = $"plugging laser at {et.X},{et.Y}";
                // Stand on a tile next to the muzzle tile that can be reached without crossing an open beam; when there is none
                // (the corridor beyond the muzzle is only reachable across the beam), first plug the next tile of the beam from a
                // safe side tile, then stand on that plug (a dust is not solid, and its bullets die on it) to plug the muzzle tile.
                var lanes0 = BotNav.LaneTiles();
                var stand = SafeStand(target, lanes0, et);
                if (stand.HasValue)
                {
                    yield return PickUpLooseDust(null);
                    if (!_pickedDust)
                    {
                        continue;
                    }
                    yield return StandFaceAccept(stand.Value.X, stand.Value.Y, FaceChar(new Point(target.X - stand.Value.X, target.Y - stand.Value.Y)));
                    yield return Wait(30);
                }
                else
                {
                    var t1 = new Point(target.X + step.X, target.Y + step.Y);
                    var stand1 = SafeStand(t1, lanes0, target);
                    if (!stand1.HasValue)
                    {
                        continue;
                    }
                    yield return PickUpLooseDust(null);
                    if (!_pickedDust)
                    {
                        continue;
                    }
                    yield return StandFaceAccept(stand1.Value.X, stand1.Value.Y, FaceChar(new Point(t1.X - stand1.Value.X, t1.Y - stand1.Value.Y)));
                    yield return Wait(30);
                    yield return PickUpLooseDust(t1);
                    if (!_pickedDust)
                    {
                        continue;
                    }
                    yield return StandFaceAccept(t1.X, t1.Y, FaceChar(new Point(-step.X, -step.Y)));
                    yield return Wait(30);
                }
                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    System.Console.Error.WriteLine($"  laser {et.X},{et.Y} plugged: {plugged()}");
                }
            }
        }


        private static bool _pickedDust;

        /// <summary>A tile next to `tile` (not the emitter's) that is walkable, off every open beam and reachable without crossing one.</summary>
        private static Point? SafeStand(Point tile, HashSet<Point> lanes, Point emitter)
        {
            foreach (var o in new[] { new Point(0, 1), new Point(0, -1), new Point(1, 0), new Point(-1, 0) })
            {
                var t = new Point(tile.X + o.X, tile.Y + o.Y);
                if (t == emitter || !BotNav.Walkable(t) || lanes.Contains(t))
                {
                    continue;
                }
                var saved = BotNav.Avoid;
                BotNav.Avoid = new HashSet<Point>(lanes);
                bool ok = t == BotNav.TileOf(Pos) || BotNav.FindPath(BotNav.TileOf(Pos), x => x == t, t, strict: true) != null;
                BotNav.Avoid = saved;
                if (ok)
                {
                    return t;
                }
            }
            return null;
        }

        /// <summary>Sweeps up a loose dust that is off the beams (not the one on tile `except`), so the broom carries it. Sets _pickedDust.</summary>
        private static IEnumerable<object> PickUpLooseDust(Point? except)
        {
            _pickedDust = false;
            if (Player.broom.dust != null)
            {
                _pickedDust = true;
                yield break;
            }
            var lanes = BotNav.LaneTiles();
            var dust = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Dust" && !lanes.Contains(BotNav.TileOf(e.Position + new Vector2(8, 8)))
                    && BotNav.TileOf(e.Position + new Vector2(8, 8)) != except)
                .OrderBy(e => NextToLane(e, lanes) ? 1 : 0).ThenBy(e => (e.Position - Pos).LengthSquared()).FirstOrDefault();
            if (dust == null)
            {
                yield break;
            }
            var dt = BotNav.TileOf(dust.Position + new Vector2(8, 8));
            foreach (var o in new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) })
            {
                var t = new Point(dt.X + o.X, dt.Y + o.Y);
                if (BotNav.Walkable(t) && !lanes.Contains(t))
                {
                    yield return StandFaceAccept(t.X, t.Y, FaceChar(new Point(-o.X, -o.Y)));
                    yield return Wait(30);
                    _pickedDust = Player.broom.dust != null;
                    yield break;
                }
            }
        }

        /// <summary>The emitters whose open beam passes over any tile of the path.</summary>
        protected static List<Entity> EmittersCrossing(List<Point> path)
        {
            var lanes = BotNav.LaneAxes();
            var onPath = new HashSet<Point>(path);
            var result = new List<Entity>();
            foreach (var em in BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "PewLaser"))
            {
                var step = FacingStep(em.facing);
                var t = BotNav.TileOf(em.Position + new Vector2(8, 8));
                for (int i = 0; i < 24; i++)
                {
                    t = new Point(t.X + step.X, t.Y + step.Y);
                    if (!lanes.ContainsKey(t))
                    {
                        break;
                    }
                    if (onPath.Contains(t))
                    {
                        result.Add(em);
                        break;
                    }
                }
            }
            return result;
        }

        /// <summary>True if there is loose dust on the screen that the broom could carry to an emitter.</summary>
        protected static bool HaveLooseDust()
        {
            var lanes = BotNav.LaneTiles();
            return Player.broom.dust == null && BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "Dust" && !lanes.Contains(BotNav.TileOf(e.Position + new Vector2(8, 8))));
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
                        yield return Fight(3600, soft: true);
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

        /// <summary>True while the automatic emitter plugging runs (its own walking must not trigger more plugging).</summary>
        protected static bool Plugging;

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
        /// Walks up a one-tile column that a PewLaser at its top end fires down, using two loose dusts that take turns being the shield:
        /// stand on a tile, place the carried dust on the tile ahead (bullets die on it, so everything behind it is safe), step onto it,
        /// sweep the dust left behind us and place that one ahead in turn. The first plug is placed from the first tile of the column
        /// at a moment when no bullet is near; from then on every step is in a shadow.
        /// </summary>
        protected static IEnumerable<object> LeapfrogUp(int col, int rowStart, int rowEnd)
        {
            BotRunner.Status = $"leapfrog up column {col} from {rowStart} to {rowEnd}";
            // Carry one dust (the one off the beam), the other stays where it is (it is behind us, below the column, in the shadow).
            yield return PickUpLooseDust(null);
            if (!_pickedDust)
            {
                throw new InvalidOperationException("bot: leapfrog needs a loose dust");
            }
            // The shield dust sits on (col,rowStart), where the beam ends: that tile and the ones below it are in its shadow. Stand on it
            // (low in the tile, so a bullet dying on the dust above never touches us), face up and set the carried dust on the tile ahead.
            var below = BotNav.CentreOf(new Point(col, rowStart + 1));
            yield return GoTo(below.X, below.Y);
            var onShield = BotNav.CentreOf(new Point(col, rowStart)) + new Vector2(0, 3);
            yield return WalkTo(onShield.X, onShield.Y, tol: 1f);
            yield return Hold(1, KeyFunctions.Up);
            yield return Wait(2);
            yield return Tap(KeyFunctions.Accept);
            yield return Wait(14);
            // Now every tile up to the plug is in its shadow: leapfrog.
            int row = rowStart;
            bool haveInHand = false;
            while (row > rowEnd)
            {
                // Standing on `row`; a dust sits on row-1 (just placed). Step onto it, then fetch the dust left behind and place it ahead.
                row--;
                var here = BotNav.CentreOf(new Point(col, row)) + new Vector2(0, 3); // low in the tile: a bullet dying on the dust above must not touch us
                yield return WalkTo(here.X, here.Y, tol: 1f);
                if (row <= rowEnd)
                {
                    break;
                }
                // sweep the dust on row+1 (behind us: face down)
                yield return Hold(1, KeyFunctions.Down);
                yield return Wait(2);
                yield return Tap(KeyFunctions.Accept);
                yield return Wait(14);
                haveInHand = Player.broom.dust != null;
                // place it ahead (row-1)
                yield return WalkTo(here.X, here.Y, tol: 1f);
                yield return Hold(1, KeyFunctions.Up);
                yield return Wait(2);
                yield return Tap(KeyFunctions.Accept);
                yield return Wait(14);
            }
            if (!haveInHand && row > rowEnd)
            {
                throw new InvalidOperationException("bot: leapfrog lost its dust");
            }
        }

        /// <summary>The first frame at which running a key script (key, frames) from here meets a predicted danger (standing for Accept).</summary>
        private static int ScriptHits((KeyFunctions key, int frames)[] script, List<Threat> threats, int extraMargin = 0)
        {
            var pb = Player.Hitbox;
            var pos = new Vector2(pb.X, pb.Y);
            int f = 0;
            foreach (var (key, frames) in script)
            {
                var v = (key == KeyFunctions.Accept ? Vector2.Zero : DirVector(key)) * WalkPxPerFrame;
                for (int i = 0; i < frames && f < Horizon; i++)
                {
                    f++;
                    pos += v;
                    var mine = new Rectangle((int)pos.X, (int)pos.Y, pb.Width, pb.Height);
                    foreach (var th in threats)
                    {
                        var r = th.At[f];
                        if (r == null) { continue; }
                        var rr = r.Value;
                        rr.Inflate(th.Margin + extraMargin, th.Margin + extraMargin);
                        if (mine.Intersects(rr))
                        {
                            return f;
                        }
                    }
                }
            }
            return int.MaxValue;
        }

        /// <summary>
        /// Shoves a Shieldy that blocks the way: a broom hit from the front moves it 8px in the direction we face (left, right or up; a
        /// downward swing hurts it instead). Stand level with it a few px away, face the way we want to go and swing, `swings` times.
        /// </summary>
        protected static IEnumerable<object> PushBlocker(Entity b, KeyFunctions dir, int swings = 8)
        {
            var face = dir == KeyFunctions.Left ? Facing.LEFT : dir == KeyFunctions.Right ? Facing.RIGHT : dir == KeyFunctions.Up ? Facing.UP : Facing.DOWN;
            var back = dir == KeyFunctions.Left ? KeyFunctions.Right : dir == KeyFunctions.Right ? KeyFunctions.Left : dir == KeyFunctions.Up ? KeyFunctions.Down : KeyFunctions.Up;
            bool horiz = dir == KeyFunctions.Left || dir == KeyFunctions.Right;
            int lastSwing = -100, done = 0;
            for (int i = 0; i < 500 && done < swings && b.exists; i++)
            {
                if (Talking || GlobalState.ScreenTransition)
                {
                    yield return AdvanceKeys(i);
                    continue;
                }
                BotRunner.Status = $"shove the {b.GetType().Name} {dir}";
                var pb = Player.Hitbox;
                var bb = b.Hitbox;
                float cross = horiz ? pb.Center.Y - bb.Center.Y : pb.Center.X - bb.Center.X;
                int gap = dir == KeyFunctions.Right ? bb.Left - pb.Right : dir == KeyFunctions.Left ? pb.Left - bb.Right : dir == KeyFunctions.Up ? pb.Top - bb.Bottom : bb.Top - pb.Bottom;
                if (gap < -2)
                {
                    yield break; // we are past it already
                }
                if (Math.Abs(cross) > 3)
                {
                    var side = horiz ? (cross > 0 ? KeyFunctions.Up : KeyFunctions.Down) : (cross > 0 ? KeyFunctions.Left : KeyFunctions.Right);
                    yield return gap < 6 ? K(back) : K(side); // line up level with it, not touching
                    continue;
                }
                if (Player.facing != face)
                {
                    yield return gap >= 4 ? K(dir) : K(back);
                    continue;
                }
                if (gap < 3)
                {
                    yield return K(back);
                    continue;
                }
                if (gap > 9)
                {
                    yield return K(dir);
                    continue;
                }
                if (i - lastSwing > 26)
                {
                    lastSwing = i;
                    done++;
                    yield return K(KeyFunctions.Accept);
                    continue;
                }
                yield return K();
            }
        }

        /// <summary>
        /// Temple (0,1): the way into the main room is a one-tile corridor (row y=288) that a Shieldy patrols. A broom hit from the front
        /// shoves it 8px along the way we face, so walk in facing east, shove it back each time it comes at us, and gain ground
        /// whenever it is far; at the gap column (x~100) turn up into the room.
        /// </summary>
        protected static IEnumerable<object> PushShieldyAlongCorridor()
        {
            int lastSwing = -100;
            for (int i = 0; i < 2400; i++)
            {
                if (Talking || GlobalState.ScreenTransition)
                {
                    yield return AdvanceKeys(i);
                    continue;
                }
                var pb = Player.Hitbox;
                if (pb.Center.X >= 98 && pb.Bottom <= 284)
                {
                    yield break; // in the room above the corridor
                }
                var sh = BotRunner.Entities().FirstOrDefault(e => e.exists && e.GetType().Name == "Shieldy" && e.Hitbox.Y >= 280 && e.Hitbox.Y <= 300);
                if (sh == null)
                {
                    yield break;
                }
                BotRunner.Status = "shove the corridor Shieldy east";
                var sb = sh.Hitbox;
                if (i % 30 == 0 && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    System.Console.Error.WriteLine($"  corridor f{i} me={pb} shieldy={sb} v={sh.velocity.X:0} facing={Player.facing}");
                }
                float rowOff = pb.Center.Y - sb.Center.Y;
                if (Math.Abs(rowOff) > 3 && sb.Left < 116)
                {
                    yield return K(rowOff > 0 ? KeyFunctions.Up : KeyFunctions.Down); // line up with the corridor row
                    continue;
                }
                if (sb.Left >= 116)
                {
                    // Shoved far enough east: walk along to the gap column and up into the room.
                    yield return pb.Center.X < 102 ? K(KeyFunctions.Right) : K(KeyFunctions.Up);
                    continue;
                }
                int gap = sb.Left - pb.Right;
                if (Player.facing != Facing.RIGHT)
                {
                    yield return K(KeyFunctions.Right);
                    continue;
                }
                if (gap < 3)
                {
                    yield return K(KeyFunctions.Left); // never touch it
                    continue;
                }
                if (gap > 9)
                {
                    yield return K(KeyFunctions.Right);
                    continue;
                }
                if (i - lastSwing > 26)
                {
                    lastSwing = i;
                    yield return K(KeyFunctions.Accept); // a front hit shoves it 8px east
                    continue;
                }
                yield return K();
            }
            throw new TimeoutException("bot: could not get past the corridor Shieldy");
        }

        /// <summary>
        /// Temple screen (0,1): a Shieldy patrols the row under the upper button without touching it. A broom hit from the
        /// front shoves a Shieldy 8px, so hit it from below as it passes under the button (pushing it onto the button), then
        /// hurry to the lower button; the gate opens for good once both are pressed at the same time.
        /// </summary>
        protected static IEnumerable<object> TempleButtonRoom()
        {
            if (Grid != new Point(0, 1))
            {
                // Step onto the screen at its bottom exit (the main room is entered through a one-tile corridor with a Shieldy in it).
                yield return GoScreenTo(0, 1);
            }
            if (!BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "Gate"))
            {
                yield break; // solved on an earlier visit: opened gates stay gone
            }
            yield return PushShieldyAlongCorridor();
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

        public static string DebugEnemies() => string.Join(" ", Enemies().Select(e => $"{e.GetType().Name}@{e.Hitbox.X},{e.Hitbox.Y}v{e.velocity.X:0},{e.velocity.Y:0}"));

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

        /// <summary>Stationary ranged enemies: they never come to us, and their volleys are what makes walking past them costly.</summary>
        protected static bool IsShooter(Entity e) => e.GetType().Name is "Frog" or "Burst_Plant";

        /// <summary>The nearest living shooter within `radius` px of the player, or null.</summary>
        protected static Entity SlimeNear(float radius)
        {
            return Enemies().Where(e => e.GetType().Name == "Slime")
                .Where(e => (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - (Pos + new Vector2(5, 6))).Length() < radius)
                .OrderBy(e => (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - (Pos + new Vector2(5, 6))).LengthSquared()).FirstOrDefault();
        }

        protected static Entity ShooterNear(float radius)
        {
            return Enemies().Where(IsShooter)
                .Where(e => (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - (Pos + new Vector2(5, 6))).Length() < radius)
                .OrderBy(e => (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - (Pos + new Vector2(5, 6))).LengthSquared()).FirstOrDefault();
        }

        /// <summary>Health we are willing to spend on charging a shooter (a volley is at most 3 bullets, and it cannot fire again until they are gone).</summary>
        protected const int TankMinHp = 4;

        private static PCtx PolicyCtx(Entity target) => new PCtx
        {
            P = Player.Hitbox,
            Facing = Player.facing switch { Facing.UP => PFacing.Up, Facing.DOWN => PFacing.Down, Facing.LEFT => PFacing.Left, _ => PFacing.Right },
            T = target.Hitbox,
            TV = target.velocity,
            Walk = CornersWalkable,
            Others = Enemies().Where(e => e != target).Select(e => e.Hitbox).ToList()
        };

        /// <summary>A policy's keys as a frame: a plain step goes through the danger-aware SafeMove, anything else (swings, standing) is sent as is.</summary>
        private static KeyFunctions[] PolicyFrame(PKey[] keys, Entity target)
        {
            if (keys.Length == 0)
            {
                return K();
            }
            KeyFunctions Map(PKey k) => k switch { PKey.Up => KeyFunctions.Up, PKey.Down => KeyFunctions.Down, PKey.Left => KeyFunctions.Left, PKey.Right => KeyFunctions.Right, _ => KeyFunctions.Accept };
            return K(keys.Select(Map).ToArray()); // the policies do their own avoiding: SafeMove would second-guess a retreat and stand still
        }

        protected static IEnumerable<object> Fight(int timeoutFrames = 2400, bool soft = false, Func<Entity, bool> only = null)
        {
            if (Enemies().Any(e => e.GetType().Name == "Seer"))
            {
                yield return SeerFight();
                yield break;
            }
            if (!Plugging && HaveLooseDust() && Player.broom != null)
            {
                // A beam that sweeps the floor around a Shieldy would hit us while we line up beside/above it: plug it first.
                var lanesF = BotNav.LaneAxes();
                var shieldies = Enemies().Where(e => e.GetType().Name == "Shieldy" && (only == null || only(e))).ToList();
                var emittersNear = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "PewLaser").Where(em =>
                {
                    var step = FacingStep(em.facing);
                    var t = BotNav.TileOf(em.Position + new Vector2(8, 8));
                    for (int i = 0; i < 24; i++)
                    {
                        t = new Point(t.X + step.X, t.Y + step.Y);
                        if (!lanesF.ContainsKey(t))
                        {
                            return false;
                        }
                        foreach (var sh in shieldies)
                        {
                            var st = BotNav.TileOf(new Vector2(sh.Hitbox.Center.X - 5, sh.Hitbox.Center.Y - 6));
                            if (Math.Abs(st.X - t.X) <= 2 && Math.Abs(st.Y - t.Y) <= 2)
                            {
                                return true;
                            }
                        }
                    }
                    return false;
                }).ToList();
                if (emittersNear.Count > 0)
                {
                    Plugging = true;
                    try
                    {
                        yield return BlockLasers(e => emittersNear.Contains(e));
                    }
                    finally
                    {
                        Plugging = false;
                    }
                }
            }
            int swing = 0, lastSwing = -1000;
            var startGrid = Grid;
            Vector2 lastPos = Pos;
            int stillFrames = 0;
            int unstick = 0;
            KeyFunctions unstickKey = KeyFunctions.Down;
            Point? lastStep = null; // the next path tile we were walking towards (blamed when we stop moving)
            // Watchdog: if no enemy dies for a while, try something else (pseudo-random but seeded, so a run is reproducible).
            var rng = new Random(unchecked((int)(BotRunner.Frame * 7919 + 13)));
            int lastCount = -1, noKill = 0;
            Entity forced = null;
            Entity committed = null; // the target we are working on: switching to whichever is nearest every frame means never finishing an approach
            int forcedFrames = 0, aggroFrames = 0, relocFrames = 0;
            Point? reloc = null;
            List<Point> heldPlan = null; // the standoff route being followed (re-searching every frame flips between clearance margins)
            int heldAge = 0;
            int planHold = 0; // frames during which a chosen route around a Shieldy is followed without re-deciding (no flip-flopping)
            float bestDist = float.MaxValue; // closest we have got to the current target (hits knock us about, so "still" is not enough)
            int noProgress = 0;
            Entity lastTarget = null;
            Entity ambushFor = null;
            var pmem = new PolicyMem();
            bool ambushChase = false; // lining up sideways under a patroller: keep going until close (turning to face down loses ground)
            var ambushCols = new Dictionary<int, int>();
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
                if (planHold > 0)
                {
                    planHold--;
                }
                var enemies = Enemies();
                if (only != null)
                {
                    enemies = enemies.Where(only).ToList();
                }
                if (enemies.Count == 0)
                {
                    yield return CollectHearts();
                    yield break;
                }
                if (enemies.Count != lastCount)
                {
                    lastCount = enemies.Count;
                    noKill = 0;
                    forced = null;
                    reloc = null;
                    relocFrames = 0;
                    aggroFrames = 0;
                }
                else
                {
                    noKill++;
                }
                if (forcedFrames > 0 && --forcedFrames == 0)
                {
                    forced = null;
                }
                if (aggroFrames > 0)
                {
                    aggroFrames--;
                }
                if (noKill > 0 && noKill % 300 == 0)
                {
                    committed = null;
                    BotNav.Blocked.Clear(); // learned "blocked" tiles may have been wrong
                    int strategy = rng.Next(3);
                    heldPlan = null;
                    if (strategy == 0 && enemies.Count > 1)
                    {
                        forced = enemies[rng.Next(enemies.Count)];
                        forcedFrames = 240;
                    }
                    else if (strategy == 1)
                    {
                        // Wander off to a random reachable tile away from the enemies, then try again from there.
                        var here1 = BotNav.TileOf(Pos);
                        var near = BotNav.NearEnemies(12);
                        var seen = new HashSet<Point> { here1 };
                        var frontier = new List<Point> { here1 };
                        var options = new List<Point>();
                        for (int depth = 0; depth < 8; depth++)
                        {
                            var next = new List<Point>();
                            foreach (var t in frontier)
                            {
                                foreach (var d in new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) })
                                {
                                    var n = new Point(t.X + d.X, t.Y + d.Y);
                                    if (seen.Add(n) && BotNav.Walkable(n))
                                    {
                                        next.Add(n);
                                        if (depth >= 2 && !near.Contains(n))
                                        {
                                            options.Add(n);
                                        }
                                    }
                                }
                            }
                            frontier = next;
                        }
                        if (options.Count > 0)
                        {
                            reloc = options[rng.Next(options.Count)];
                            relocFrames = 100;
                        }
                    }
                    else
                    {
                        aggroFrames = 240;
                    }
                    if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                    {
                        System.Console.Error.WriteLine($"  fight watchdog: no kill for {noKill} frames at {Pos}; strategy {strategy} (forced={forced?.GetType().Name} reloc={reloc} aggro={aggroFrames})");
                    }
                }
                if (relocFrames > 0 && reloc.HasValue)
                {
                    relocFrames--;
                    var hereR = BotNav.TileOf(Pos);
                    if (hereR == reloc.Value)
                    {
                        relocFrames = 0;
                    }
                    else
                    {
                        BotNav.Avoid = BotNav.NearEnemies(6);
                        var rp = BotNav.FindPath(hereR, t => t == reloc.Value, reloc.Value);
                        BotNav.Avoid = null;
                        rp ??= BotNav.FindPath(hereR, t => t == reloc.Value, reloc.Value);
                        if (rp != null && rp.Count > 0)
                        {
                            BotRunner.Status = "fight/relocate";
                            var ncR = BotNav.CentreOf(rp[0]) + new Vector2(5, 6);
                            float pxR = ncR.X - (Pos.X + 5), pyR = ncR.Y - (Pos.Y + 6);
                            yield return SafeMove(Math.Abs(pxR) > 2f ? (pxR < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (pyR < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                            continue;
                        }
                        relocFrames = 0;
                    }
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
                if (committed == null || !committed.exists || !enemies.Contains(committed))
                {
                    committed = enemies.OrderBy(e => (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - me).LengthSquared()).First();
                }
                var target = (forced != null && forced.exists && enemies.Contains(forced)) ? forced : committed;
                if (i % 10 == 0 && Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                {
                    System.Console.Error.WriteLine($"  enemies f{i} me={me} " + string.Join(" ", enemies.Select(e => $"{e.GetType().Name}@{e.Hitbox.Center.X},{e.Hitbox.Center.Y}")) + $" target={target.GetType().Name} status=[{BotRunner.Status}]");
                }
                if (enemies.FirstOrDefault(e => e.GetType().Name == "Annoyer" && (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - me).Length() < (forced == null ? 80 : 40)) is Entity nearAnnoyer)
                {
                    target = nearAnnoyer; // one hit kills it and it is the one that comes at us: deal with it before a patroller
                }
                if (target.GetType().Name == "Slime" && forced == null)
                {
                    // Slimes drift: whichever is nearest is the one that can touch us, so it is always the one to deal with (not the one chosen earlier).
                    var meBox = Player.Hitbox;
                    target = enemies.Where(e => e.GetType().Name == "Slime")
                        .OrderBy(e => Math.Max(Math.Max(e.Hitbox.Left - meBox.Right, meBox.Left - e.Hitbox.Right), Math.Max(e.Hitbox.Top - meBox.Bottom, meBox.Top - e.Hitbox.Bottom))).First();
                }
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
                if (target.GetType().Name == "Shieldy" && target.velocity == Vector2.Zero && FightPolicies.BoomStationary(PolicyCtx(target), pmem) is PKey[] boomKeys)
                {
                    BotRunner.Status = "fight/boom";
                    yield return PolicyFrame(boomKeys, target);
                    continue;
                }
                if (target.GetType().Name == "Shieldy" && target.velocity.Y != 0 && target.velocity.X == 0 && aggroFrames <= 0 && Target_ColumnAboveClear(target) &&
                    FightPolicies.VerticalShieldy(PolicyCtx(target), pmem) is PKey[] vKeys)
                {
                    BotRunner.Status = "fight/ambush vertical";
                    if (noKill > 0 && noKill < 1200)
                    {
                        noKill = 1; // waiting for a patroller to come by is not "no progress"
                    }
                    yield return PolicyFrame(vKeys, target);
                    continue;
                }
                if (target.GetType().Name == "Shieldy" && target.velocity.X != 0 && target.velocity.Y == 0 && aggroFrames <= 0 && ShieldyHoverY(target) is int hoverY)
                {
                    // A Shieldy patrolling sideways: float 3px above its line (no contact possible), follow it in x and swing down.
                    var tb = target.Hitbox;
                    var pb = Player.Hitbox;
                    int gap = tb.Top - pb.Bottom;
                    bool inBand = gap >= 1 && gap <= 24 && Math.Abs(pb.Center.X - tb.Center.X) < 60 && CornersWalkable(new Rectangle(pb.X, pb.Y + 1, pb.Width, Math.Min(gap, 6) + pb.Height));
                    if (i % 20 == 0 && Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                    {
                        System.Console.Error.WriteLine($"  ambush f{i} gap={gap} inBand={inBand} pb={pb} tb={tb} v={target.velocity}");
                    }
                    if (!inBand)
                    {
                        // Get to a floor tile above the line, in the column of it that is nearest to us.
                        BotRunner.Status = "fight/ambush route";
                        int row = (hoverY + 6) / 16;
                        var here0 = BotNav.TileOf(Pos);
                        Point? goalTile = null;
                        int bestCost = int.MaxValue;
                        for (int col = tb.Center.X / 16 - 12; col <= tb.Center.X / 16 + 12; col++)
                        {
                            var rect = new Rectangle(col * 16 + 3, hoverY, 10, 12);
                            if (CornersWalkable(rect) && BotNav.Walkable(new Point(col, tb.Top / 16)) && Math.Abs(col - here0.X) + Math.Abs(row - here0.Y) < bestCost)
                            {
                                bestCost = Math.Abs(col - here0.X) + Math.Abs(row - here0.Y);
                                goalTile = new Point(col, row);
                            }
                        }
                        if (goalTile.HasValue)
                        {
                            List<Point> hp = null;
                            foreach (int margin in new[] { 6, 2, -2 })
                            {
                                BotNav.Avoid = BotNav.NearEnemies(margin);
                                hp = BotNav.FindPath(BotNav.TileOf(Pos), t => t == goalTile.Value, goalTile.Value, strict: true);
                                BotNav.Avoid = null;
                                if (hp != null)
                                {
                                    break;
                                }
                            }
                            if (hp != null && hp.Count > 0)
                            {
                                lastStep = hp[0];
                                var nc = BotNav.CentreOf(hp[0]) + new Vector2(5, 6);
                                yield return SafeMove(StepKey(nc.X - me.X, nc.Y - me.Y, target), target);
                                continue;
                            }
                        }
                    }
                    else
                    {
                        BotRunner.Status = "fight/ambush";
                        if (noKill > 0 && noKill < 1200)
                        {
                            noKill = 1; // waiting for a patroller to come by is not "no progress"
                        }
                        float adx = tb.Center.X - pb.Center.X;
                        // Facing down already: stay put unless it got too close or too far. Not facing down: turning costs a pixel of
                        // gap (the Down press also walks), so make sure there are 4+ before pressing it.
                        bool facingDown = Player.facing == Facing.DOWN;
                        if (facingDown ? gap < 2 : gap < 4)
                        {
                            yield return K(KeyFunctions.Up);
                            continue;
                        }
                        if (gap > 8)
                        {
                            yield return K(KeyFunctions.Down);
                            continue;
                        }
                        if (Math.Abs(adx) > 10)
                        {
                            ambushChase = true;
                        }
                        else if (Math.Abs(adx) <= 3)
                        {
                            ambushChase = false;
                        }
                        if (Math.Abs(adx) <= 10 && !ambushChase)
                        {
                            if (Player.facing != Facing.DOWN)
                            {
                                yield return K(KeyFunctions.Down); // turns us (and nudges ~1px towards it: the gap is >= 3)
                                continue;
                            }
                            swing++;
                            if (swing - lastSwing >= SwingCooldown + 8)
                            {
                                lastSwing = swing;
                                if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                                {
                                    var hf = target.GetType().GetField("_health", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
                                    System.Console.Error.WriteLine($"  ambush SWING f{i} adx={adx:0.0} gap={gap} health={hf?.GetValue(target)} facing={Player.facing}");
                                }
                                yield return K(KeyFunctions.Accept);
                                continue;
                            }
                            yield return K();
                            continue;
                        }
                        var side = adx < 0 ? KeyFunctions.Left : KeyFunctions.Right;
                        var moved = pb;
                        moved.Offset(adx < 0 ? -2 : 2, 0);
                        yield return CornersWalkable(moved) ? K(side) : K();
                        continue;
                    }
                }
                if (target.GetType().Name == "Shieldy" && aggroFrames <= 0)
                {
                    // Only a downward swing hurts it (the broom reaches ~16px below the player). Get to a standoff two
                    // tiles above, line up in x, walk down until the gap is small, then swing facing down.
                    var tb = target.Hitbox;
                    var pb = Player.Hitbox;
                    int gap = tb.Top - pb.Bottom;
                    bool aligned = Math.Abs(dx) <= 4;
                    if (aligned && gap >= -4 && gap <= 40 && pb.Bottom <= tb.Top + 4)
                    {
                        if (gap > 10)
                        {
                            yield return K(KeyFunctions.Down);
                            continue;
                        }
                        if (Player.facing != Facing.DOWN)
                        {
                            // Turning to face down means pressing Down, which also moves us into it: back off to a clear gap first,
                            // then one frame of Down turns us with ~5px to spare (the broom reaches ~16px, so no need to touch it).
                            yield return gap >= 6 ? K(KeyFunctions.Down) : K(KeyFunctions.Up);
                            continue;
                        }
                        if (gap < 3)
                        {
                            // Facing down but too close (overlap = contact damage): step back, re-turn on the way in.
                            yield return K(KeyFunctions.Up);
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
                    var slideRect = pb;
                    slideRect.Offset(dx < 0 ? -3 : 3, 0);
                    if (planHold <= 0 && CornersWalkable(slideRect) && pb.Bottom <= tb.Top - (target.velocity == Vector2.Zero && InCorridor(target) ? 0 : 2) && Math.Abs(dx) > 4 && Math.Abs(dx) < 40)
{
                        if (target.velocity != Vector2.Zero)
                        {
                            // A patroller comes to us: wait just above its line, facing down (chasing it sideways ends in contact).
                            BotRunner.Status = "fight/wait-above";
                            yield return gap > 10 ? K(KeyFunctions.Down) : (Player.facing != Facing.DOWN && gap >= 6 ? K(KeyFunctions.Down) : K());
                            continue;
                        }
                        // Standing clear above a stationary one: slide sideways to line up.
                        BotRunner.Status = "fight/slide";
                        yield return K(dx < 0 ? KeyFunctions.Left : KeyFunctions.Right);
                        continue;
                    }
                    if (planHold <= 0 && target.velocity == Vector2.Zero && InCorridor(target) && pb.Top > tb.Bottom && pb.Top - tb.Bottom < 28 && pb.Right > tb.Left - 3 && pb.Left < tb.Right + 3)
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
                    if (ambushFor != target)
                    {
                        ambushFor = target;
                        ambushCols.Clear();
                    }
                    foreach (int up in new[] { 2, 1 })
                    {
                        int standX = et.X;
                        if (target.velocity.X != 0 && target.velocity.Y == 0)
                        {
                            // A sideways patroller: its x keeps changing, so a standoff that follows it never settles. Ambush it instead: pick
                            // (once) the column of its line, with clear floor above, that is nearest to where we are, and wait there.
                            if (!ambushCols.TryGetValue(up, out standX))
                            {
                                var here1 = BotNav.TileOf(Pos);
                                int best = int.MaxValue;
                                standX = et.X;
                                var lanesNow = BotNav.LaneTiles();
                                for (int dirX = -1; dirX <= 1; dirX += 2)
                                {
                                    for (int k = 0; k < 14; k++)
                                    {
                                        int col = et.X + dirX * k;
                                        if (!BotNav.Walkable(new Point(col, et.Y)))
                                        {
                                            break;
                                        }
                                        var st = new Point(col, et.Y - up);
                                        if (BotNav.Walkable(st) && !lanesNow.Contains(st) && Math.Abs(col - here1.X) + Math.Abs(st.Y - here1.Y) < best)
                                        {
                                            best = Math.Abs(col - here1.X) + Math.Abs(st.Y - here1.Y);
                                            standX = col;
                                        }
                                    }
                                }
                                ambushCols[up] = standX;
                            }
                        }
                        var standoff = new Point(standX, et.Y - up);
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
                    {
                        // Keep following the route chosen a moment ago while we are on it: the margin-5 and margin-1 searches differ by a
                        // pixel of position, and flipping between them every frame made the bot jitter on the spot.
                        var here0 = BotNav.TileOf(Pos);
                        if (heldPlan != null && heldAge++ < 45)
                        {
                            int at = heldPlan.IndexOf(here0);
                            if (at >= 0 && at < heldPlan.Count - 1)
                            {
                                sp = heldPlan.Skip(at + 1).ToList();
                            }
                            else
                            {
                                heldPlan = null;
                            }
                        }
                        else
                        {
                            heldPlan = null;
                        }
                        if (heldPlan == null && sp != null && sp.Count > 0)
                        {
                            heldPlan = new List<Point>(sp.Prepend(here0));
                            heldAge = 0;
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
                        planHold = 30;
                        var skey = SafeMove(StepKey(px, py, target), target);
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null && i % 120 == 0)
                        {
                            System.Console.Error.WriteLine($"    step px={px:0.0} py={py:0.0} key={string.Join("+", skey)} dodged={Dodged} still={stillFrames} facing={Player.facing} dontMove={Player.dontMove} state={Player.state} talk={Talking} trans={GlobalState.ScreenTransition} vel={Player.velocity}");
                        }
                        BotRunner.Status = "fight/plan";
                        yield return skey;
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
                    BotRunner.Status = "fight/up-fallback";
                    yield return K(KeyFunctions.Up);
                    continue;
                }
                float distSq = dx * dx + dy * dy;
                if (target != lastTarget || distSq < bestDist - 16f)
                {
                    lastTarget = target;
                    bestDist = distSq;
                    noProgress = 0;
                }
                else
                {
                    noProgress++;
                }
                if (target.GetType().Name == "Annoyer" && distSq < 44 * 44)
                {
                    // An awake Annoyer flies to a point 20px right of us and circles us there for 3s before it swoops: chasing it is
                    // what gets us hit. Stand still facing right and swing; it is in reach every time round.
                    BotRunner.Status = "fight/annoyer hold";
                    if (i % 6 == 0 && Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                    {
                        System.Console.Error.WriteLine($"  annoyer f{i} me={me} ann={c} d=({dx:0},{dy:0}) facing={Player.facing}");
                    }
                    var holdLanes = BotNav.LaneTiles();
                    if (holdLanes.Count > 0)
                    {
                        var hb = Player.Hitbox;
                        bool exposed = false;
                        foreach (var corner in new[] { new Vector2(hb.Left, hb.Top), new Vector2(hb.Right - 1, hb.Top), new Vector2(hb.Left, hb.Bottom - 1), new Vector2(hb.Right - 1, hb.Bottom - 1) })
                        {
                            exposed |= holdLanes.Contains(BotNav.TileOf(corner));
                        }
                        if (exposed)
                        {
                            // Never wait in a beam: go to the nearest floor tile that is out of every beam.
                            var exit = BotNav.FindPath(BotNav.TileOf(Pos), t => !holdLanes.Contains(t) && !holdLanes.Contains(new Point(t.X, t.Y - 1)) && !holdLanes.Contains(new Point(t.X, t.Y + 1)) && !holdLanes.Contains(new Point(t.X - 1, t.Y)) && !holdLanes.Contains(new Point(t.X + 1, t.Y)));
                            if (exit != null && exit.Count > 0)
                            {
                                var nc2 = BotNav.CentreOf(exit[0]) + new Vector2(5, 6);
                                yield return K(StepKey(nc2.X - me.X, nc2.Y - me.Y, target));
                                continue;
                            }
                        }
                    }
                    var safeFrame = StandSafe(target);
                    if (safeFrame.Length > 0)
                    {
                        yield return safeFrame; // something else (a patroller, a beam) is about to reach this spot: not here
                        continue;
                    }
                    // It homes in on a spot beside us in a straight line (through us if it comes from the other side) and then circles at
                    // ~20px: face wherever it is and keep swinging, the broom reaches it before it touches.
                    var annFace = Math.Abs(dx) >= Math.Abs(dy) ? (dx < 0 ? Facing.LEFT : Facing.RIGHT) : (dy < 0 ? Facing.UP : Facing.DOWN);
                    var annKey = annFace == Facing.LEFT ? KeyFunctions.Left : annFace == Facing.RIGHT ? KeyFunctions.Right : annFace == Facing.UP ? KeyFunctions.Up : KeyFunctions.Down;
                    if (Player.facing != annFace)
                    {
                        yield return K(annKey);
                        continue;
                    }
                    swing++;
                    if (distSq < 30 * 30 && swing - lastSwing >= SwingCooldown)
                    {
                        lastSwing = swing;
                        yield return K(KeyFunctions.Accept);
                        continue;
                    }
                    yield return K();
                    continue;
                }
                // Close but not getting closer (a wall or water between us and a shooter): plan a tile route instead of pushing at it.
                bool blockedClose = DodgeEnabled && noProgress > 40 && target.GetType().Name != "Annoyer";
                if (distSq > 30 * 30 || blockedClose)
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
                        yield return (DodgeEnabled && IsShooter(target) && GlobalState.CUR_HEALTH >= 3) ? K(dir) : SafeMove(dir, target);
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
                if (target.GetType().Name == "Slime" && distSq < 55 * 55 && !(DodgeEnabled && noProgress > 40) && FightPolicies.Slime(PolicyCtx(target), pmem) is PKey[] slimeKeys)
                {
                    if (i % 5 == 0 && Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                    {
                        System.Console.Error.WriteLine($"  slime policy f{i}: keys=[{string.Join("+", slimeKeys)}] P={Player.Hitbox} T={target.Hitbox} facing={Player.facing}");
                    }
                    yield return PolicyFrame(slimeKeys, target);
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
                    // Charging a shooter: dodging its volley only drags the fight out (it re-fires when the bullets are gone).
                    yield return (DodgeEnabled && IsShooter(target) && GlobalState.CUR_HEALTH >= 3) ? K(dir) : SafeMove(dir, target);
                }
            }
            if (!soft)
            {
                throw new TimeoutException($"bot: Fight timed out on {Map} screen {Grid}");
            }
        }
    }
}
