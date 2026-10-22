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
            int skip = 0;
            foreach (var tok in tokens.Split(' ', StringSplitOptions.RemoveEmptyEntries))
            {
                if (skip > 0)
                {
                    skip--;
                    continue;
                }
                int n = tok.Length > 1 && int.TryParse(tok.Substring(1), out int v) ? v : 1;
                switch (tok[0])
                {
                    case ')':
                    {
                        // )x,y,r[,frames]: stand by until no enemy is within r px of x,y (a hop across a gap lands among them otherwise)
                        var a = tok.Substring(1).Split(',').Select(float.Parse).ToArray();
                        var at = new Vector2(a[0], a[1]);
                        int most = a.Length > 3 ? (int)a[3] : 600;
                        for (int w = 0; w < most && Enemies().Any(e => (e.Hitbox.Center.ToVector2() - at).Length() < a[2]); w++)
                        {
                            BotRunner.Status = $"wait for the enemies to leave {at}";
                            BotRunner.NotStuck();
                            yield return K();
                        }
                        break;
                    }
                    case '\'':
                    {
                        // 'tx,ty,k: skip the next k tokens unless tile tx,ty can be walked to from here (a fight may leave us either side of a hole)
                        var a = tok.Substring(1).Split(',').Select(int.Parse).ToArray();
                        var here = BotNav.TileOf(Player.Position);
                        var to = new Point(a[0], a[1]);
                        if (here != to && BotNav.FindPath(here, to) == null)
                        {
                            skip = a[2];
                        }
                        break;
                    }
                    case 'U': yield return Hold(n, KeyFunctions.Up); break;
                    case 'D': yield return Hold(n, KeyFunctions.Down); break;
                    case 'L': yield return Hold(n, KeyFunctions.Left); break;
                    case 'R': yield return Hold(n, KeyFunctions.Right); break;
                    case 'W':
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DUMP") is string dump)
                        {
                            var dv = dump.Split(',').Select(int.Parse).ToArray(); // dev: tile map x0,y0,x1,y1
                            DumpTiles(new Point(dv[0] + 3, dv[1] + 3), new Point(dv[2] - 3, dv[3] - 3));
                        }
                        yield return Wait(n);
                        break;
                    case 'T': yield return ReadDialogue(); break;
                    case 'Q': yield return GoScreen(tok[1]); break;
                    case 'F': yield return n > 1 ? Fight(n, soft: true) : Fight(); break; // F<n>: soft fight, give up after n frames
                    case 'd': yield return Fight(n > 1 ? n : 2400, soft: true, only: e => e.GetType().Name == "Annoyer"); break; // d<n>: fight only the (awake) Annoyers here first
                    case 'I':
                    {
                        // Ix,y,side e.g. I768,672,l
                        var a = tok.Substring(1).Split(',');
                        yield return InteractAt(float.Parse(a[0]), float.Parse(a[1]), a[2][0]);
                        break;
                    }
                    case '/': // /: the screen as text (tiles, entities, player); /x,y another screen of this map (tiles, entity presets)
                    {
                        if (tok.Length > 1)
                        {
                            var a = tok.Substring(1).Split(',');
                            System.Console.Error.Write(BotNav.ScreenText(new Point(int.Parse(a[0]), int.Parse(a[1]))));
                        }
                        else
                        {
                            System.Console.Error.Write(BotNav.RoomText());
                        }
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
                        // B<x>,<y>: stand on the button at x,y (tile centre); B<x>,<y>,<px>,<py>: stand with the player at exactly px,py on it
                        var a = tok.Substring(1).Split(',');
                        yield return StandUntilGateOpens(float.Parse(a[0]), float.Parse(a[1]), exact: a.Length >= 4 ? new Vector2(float.Parse(a[2]), float.Parse(a[3])) : null);
                        break;
                    }
                    case 'O': DodgeEnabled = tok[1] == '1'; break;
                    case 's': yield return tok == "s1" ? SteamRising() : SteamJustOff(); break; // s: wait for the screen's steam jets to finish burning (then ~3s before they burn again); s1: until they start to rise (~35 harmless frames before the burn)
                    case 'a': BotNav.Wade = tok[1] == '1'; break;
                    case 'f': BotNav.RaftNarrow = tok[1] == '1'; break; // f1: rafts ride on along narrow rivers instead of jumping off across them // a1: wade through water instead of rafting or going round (short stretches: Red Cave water drowns you after 3s)
                    case 'y': BotNav.LaneScale = n / 100f; break;
                    case 'p': BotRunner.NoAutoPlug = n == 1; break; // p1: no fetching dust to plug beams on the way
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
                        if (a[2].Length > 1)
                        {
                            // Jx,y,d+ / Jx,y,d-: sweep up / set down dust, standing so it lands square on the tile in front (a platform's
                            // box is small: dust set down a few px off misses it and falls in the hole), retried until it took
                            var (key, facing) = a[2][0] switch
                            {
                                'u' => (KeyFunctions.Up, Facing.UP),
                                'd' => (KeyFunctions.Down, Facing.DOWN),
                                'l' => (KeyFunctions.Left, Facing.LEFT),
                                _ => (KeyFunctions.Right, Facing.RIGHT)
                            };
                            yield return CrowdPlatforms.SweepTill(int.Parse(a[0]), int.Parse(a[1]), key, facing, a[2][1] == '+');
                        }
                        else
                        {
                            yield return StandFaceAccept(int.Parse(a[0]), int.Parse(a[1]), a[2][0]);
                        }
                        break;
                    }
                    case 'K': yield return TempleLaserDust(); break;
                    case 'k': yield return FillHoles(); break;
                    case '_':
                    {
                        // _x,y: through this screen's switch-pillar maze to world point x,y (hitting the pillar switches as needed)
                        var a = tok.Substring(1).Split(',');
                        yield return PillarMaze(float.Parse(a[0]), float.Parse(a[1]));
                        break;
                    }
                    case ']': yield return SilverfishOnButton(n > 1 ? n : 1800); break; // ]: herd the screen's silverfish onto a button (we hold the other)
                    case '(':
                    {
                        // (x,y: jump onto the moving platform at tile x,y from a floor tile next to it and ride it to its stop
                        var a = tok.Substring(1).Split(',');
                        yield return CrowdPlatforms.RideFrom(int.Parse(a[0]), int.Parse(a[1]));
                        break;
                    }
                    case 't': yield return CrowdPlatforms.Platforms(n); break; // t<n>: a Crowd moving-platform route (see CrowdPlatforms)
                    case 'x': yield return WallBossFight(); break; // x: the Crowd's Wall boss
                    case '`': SectionEnumerator.SnapshotNow(tok.Substring(1)); break;
                    case 'g': BotRunner.BossDamageBudget = int.Parse(tok.Substring(1)); break; // g<n>: this section may cost n hearts (documented, shrinking: every one is a TODO)
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
                        System.Console.Error.WriteLine("LANES: " + string.Join(" ", BotNav.LaneAxes().Keys.Select(q => $"{q.X},{q.Y}")) + " EMITTERS: " + string.Join(" ", BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "PewLaser").Select(e => $"{(int)e.Position.X / 16},{(int)e.Position.Y / 16}{e.facing}")));
                        System.Console.Error.WriteLine("BLOCKED: " + string.Join(" ", BotNav.Blocked.Select(q => $"{q.X},{q.Y}")));
                        for (int r = int.Parse(Environment.GetEnvironmentVariable("NR0") ?? "20"); r <= int.Parse(Environment.GetEnvironmentVariable("NR1") ?? "28"); r++) { System.Console.Error.WriteLine($"{r,3} " + BotNav.DebugRow(r, int.Parse(Environment.GetEnvironmentVariable("NC0") ?? "14"), int.Parse(Environment.GetEnvironmentVariable("NC1") ?? "30"))); }
                        break;
                    case '+': yield return OpenChests(); break;
                    case 'b':
                    {
                        // b<x>,<y>: to world point x,y on this map, first pressing the buttons we can reach until a raft/jump route opens up
                        var xy = tok.Substring(1).Split(',');
                        yield return PlanTo(float.Parse(xy[0]), float.Parse(xy[1]));
                        break;
                    }
                    case '|': yield return MoverOnButton(n); break; // &<s>: get a Mover onto one button while we stand on the other (until PUZZLES_SOLVED >= 2)
                    case 'm': yield return LureMovers(n); break; // m<s>: lure every Mover on the screen into a steam beam (or a button), at most s seconds
                    case 'i': yield return CollectCicada(); break;
                    case 'o': yield return EnterDoorTo(tok.Substring(1)); break; // o<MAP>: walk through this map's door that leads to MAP
                    case 'e':
                        if (tok.Length > 1)
                        {
                            // e<r|l|u|d>[at]: a running jump that way (over holes), taking off once x (r/l) or y (u/d) reaches at
                            yield return JumpToward(tok[1] switch { 'l' => KeyFunctions.Left, 'u' => KeyFunctions.Up, 'd' => KeyFunctions.Down, _ => KeyFunctions.Right },
                                tok.Length > 2 ? float.Parse(tok.Substring(2)) : float.NaN);
                        }
                        else
                        {
                            yield return Tap(KeyFunctions.Cancel); yield return Wait(30); // e: jump (the boots)
                        }
                        break;
                    case 'c':
                    {
                        // DEV ONLY (ANODYNE_BOT_DEV=1): top the card count up to n without earning the cards, to develop the later parts while the
                        // southern Fields chests (rafts) are unfinished. A route that uses it is not a real completion.
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEV") != "1")
                        {
                            throw new InvalidOperationException("bot: token c grants cards: development only (ANODYNE_BOT_DEV=1)");
                        }
                        var cs = GlobalState.inventory.CardStatus;
                        for (int ci = 0; ci < cs.Length && GlobalState.inventory.CardCount < n; ci++)
                        {
                            cs[ci] = true;
                        }
                        System.Console.Error.WriteLine($"DEV: cards topped up to {GlobalState.inventory.CardCount} (not earned)");
                        break;
                    }
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
                    case '=':
                    {
                        // Debug: =x0,y0,x1,y1 prints the tiles of that rectangle: '#' solid, '+' partly solid, '.' floor, 'o' still water, U/D/L/R water flowing that way, else the event's initial (h hole, s spike...)
                        var a = tok.Substring(1).Split(',').Select(int.Parse).ToArray();
                        var tiles = (AnodyneSharp.MapData.Tiles.Tile[])typeof(AnodyneSharp.MapData.Map).GetField("_tileObjects", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance).GetValue(BotNav.CurrentMap);
                        for (int ty = a[1]; ty <= a[3]; ty++)
                        {
                            var row = new System.Text.StringBuilder($"FLOW {ty,3} ");
                            for (int tx = a[0]; tx <= a[2]; tx++)
                            {
                                var (al, ev) = BotNav.CurrentMap.GetTileClass(new Point(tx, ty));
                                var t = tiles[BotNav.CurrentMap.GetTile(AnodyneSharp.MapData.Layer.BG, new Point(tx, ty))];
                                row.Append(ev == AnodyneSharp.MapData.Tiles.CollisionEventType.CONVEYOR ? t.direction switch { Touching.UP => 'U', Touching.DOWN => 'D', Touching.LEFT => 'L', Touching.RIGHT => 'R', _ => 'o' } : al == Touching.ANY ? '#' : ev != AnodyneSharp.MapData.Tiles.CollisionEventType.NONE ? char.ToLowerInvariant(ev.ToString()[0]) : al != Touching.NONE ? '+' : '.');
                            }
                            System.Console.Error.WriteLine(row.ToString());
                        }
                        break;
                    }
                    case 'v':
                        // DEV ONLY (ANODYNE_BOT_DEV=1): v<MAP> marks that map's Nexus pad as visited, for snapshots made before the pad was
                        // reached in one run (the Red Cave snapshots descend from a save without the Overworld/Fields/Beach/Red Sea pads).
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEV") != "1")
                        {
                            throw new InvalidOperationException("bot: token v activates Nexus pads: development only (ANODYNE_BOT_DEV=1)");
                        }
                        GlobalState.events.ActivatedNexusPortals.Add(tok.Substring(1));
                        break;
                    case 'q': System.Console.Error.WriteLine($"CARRYING {Player.broom.dust != null} raft={HasRaft()} grid={Grid} pos={Pos} portals={string.Join(",", GlobalState.events.ActivatedNexusPortals)}"); break;
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
                    case '<':
                    {
                        // <x,y[,r|u|l|d[j]]: rush the frog beyond x,y that way (default r, jumping a pit: rj), see FrogRush
                        var a = tok.Substring(1).Split(',');
                        string how = a.Length > 2 ? a[2] : "rj";
                        var rdir = how[0] == 'u' ? KeyFunctions.Up : how[0] == 'd' ? KeyFunctions.Down : how[0] == 'l' ? KeyFunctions.Left : KeyFunctions.Right;
                        yield return FrogRush(float.Parse(a[0]), float.Parse(a[1]), rdir, how.EndsWith("j"));
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
                        // Pb<x>,<y>: only while still inside the dungeon map (the walk before it may already have left through the door)
                        if (tok.Length > 1 && tok[1] == 'b' && Map != "BEDROOM")
                        {
                            break;
                        }
                        var xy = tok.Substring(tok.Length > 1 && tok[1] == 'b' ? 2 : 1).Split(',');
                        yield return GoTo(float.Parse(xy[0]), float.Parse(xy[1]));
                        break;
                    }
                    case 'G':
                    {
                        var xy = tok.Substring(1).Split(',');
                        if (xy.Length > 2 && xy[2].Length == 2 && xy[2][0] == 'k')
                        {
                            // G<x>,<y>,k<u|d|l|r>: from x,y push that way into the keyblock there until it opens (see OpenKeyBlock)
                            yield return OpenKeyBlock(float.Parse(xy[0]), float.Parse(xy[1]), xy[2][1] switch { 'u' => KeyFunctions.Up, 'd' => KeyFunctions.Down, 'l' => KeyFunctions.Left, _ => KeyFunctions.Right });
                            break;
                        }
                        yield return WalkTo(float.Parse(xy[0]), float.Parse(xy[1]), yFirst: xy.Length > 2 && xy[2] != "d", diagonal: xy.Length > 2 && xy[2] == "d"); // G<x>,<y>[,y|,d]: y first, or diagonally
                        break;
                    }
                    case 'S': yield return Wait(2); AnodyneSharp.Drawing.SpriteDrawer.SaveRender($"/tmp/bot/shot{n}.png"); break;
                    case 'A':
                        for (int i = 0; i < n; i++) { yield return Tap(KeyFunctions.Accept); yield return Wait(8); }
                        break;
                    case 'C':
                        for (int i = 0; i < n; i++) { yield return Tap(KeyFunctions.Cancel); yield return Wait(8); }
                        break;
                    case ';': yield return MenuEquipBroom(n); break; // ;<k>: equip broom k from the pause menu (0 plain, 1 extend, 2 widen, 3 swap)
                    case '[': yield return RatOnButton(n > 1 ? n : 1800); break; // [[frames]: get the screen's Rat onto one button (one dust swept if need be) while we hold the other
                    case '}': yield return KillSilverfish(n > 1 ? n : 3600); break; // }[frames]: broom every Silverfish on this screen (walk up to each from the side away from a wall)
                    case ':': yield return KillTownsfolk(n); break; // :<n>: sweep this screen's Black and White Town walkers until n are dead in all
                    case '{':
                    {
                        // {<Type>,x0,y0,x1,y1[,frames]: stand still until a live entity of that type is inside the world rectangle (by its position), e.g. a rat walking into the spot where we flip the pillars on it
                        var a = tok.Substring(1).Split(',');
                        var r = new Rectangle(int.Parse(a[1]), int.Parse(a[2]), int.Parse(a[3]) - int.Parse(a[1]), int.Parse(a[4]) - int.Parse(a[2]));
                        int limit = a.Length > 5 ? int.Parse(a[5]) : 1200;
                        for (int i = 0; ; i++)
                        {
                            if (BotRunner.Entities().Any(e => e.exists && e.GetType().Name == a[0] && r.Contains(new Point((int)e.Position.X, (int)e.Position.Y))))
                            {
                                break;
                            }
                            if (i >= limit)
                            {
                                throw new InvalidOperationException($"bot: no {a[0]} came into {r} in {limit} frames on {Map}");
                            }
                            yield return Wait(1);
                        }
                        break;
                    }
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
            if (!Talking)
            {
                return new KeyFunctions[0]; // nothing to advance (a screen scroll, say): an Accept here would only swing the broom
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

        static Point? _cameFrom; // the screen we were on before the last GoScreenTo

        protected static IEnumerable<object> GoScreenTo(int tx, int ty)
        {
            var from = Grid;
            yield return GoToWhere(t => t.X * 16 >= tx * 160 && t.X * 16 < tx * 160 + 160 && t.Y * 16 >= ty * 160 && t.Y * 16 < ty * 160 + 160,
                $"screen {tx},{ty}");
            yield return Until(() => !GlobalState.ScreenTransition && Grid == new Point(tx, ty), 600, $"arrive on screen {tx},{ty}");
            if (from != new Point(tx, ty))
            {
                _cameFrom = from;
            }
            if (!(AutoFight && GlobalState.inventory.HasBroom && Enemies().Count > 0))
            {
                yield return Wait(10); // let the scroll settle; with enemies about there is no time to stand around: straight into the fight
            }
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
        protected static IEnumerable<object> StandUntilGateOpens(float x, float y, int timeoutFrames = 3000, Vector2? exact = null)
        {
            yield return GoTo(x, y);
            BotRunner.Status = "standing on a button until a gate opens";
            bool Open() => BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "Gate" && !e.Solid);
            for (int i = 0; i < timeoutFrames && !Open(); i++)
            {
                BotRunner.NotStuck();
                if (Talking)
                {
                    yield return AdvanceKeys(i);
                    continue;
                }
                if (i % 100 == 0 && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    System.Console.Error.WriteLine("  buttons: " + string.Join(" ", BotRunner.Entities().Where(e => e.exists && (e.GetType().Name == "Shieldy" || e.GetType().Name == "Button")).Select(e => $"{e.GetType().Name}@{e.Hitbox.X},{e.Hitbox.Y}({e.Hitbox.Width}x{e.Hitbox.Height}) v={e.velocity.X:0},{e.velocity.Y:0}")) + $" hp={GlobalState.CUR_HEALTH} solved={GlobalState.PUZZLES_SOLVED}");
                }
                // Stay on the button: nudge back if something pushed us off. With an exact spot the player stands off-centre on purpose (a
                // Mover homes in on the player's position, so where we stand decides where it stops).
                var c = exact ?? BotNav.CentreOf(BotNav.TileOf(new Vector2(x, y)));
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
        private static long _jumpPressFrame = -10;
        private static bool _jumpOnSpot;

        private const float WalkPxPerFrame = 70f / 60f;

        private static readonly System.Text.RegularExpressions.Regex ProjectileName =
            new("Laser|Goo|Bullet|Fire|Spit|Shot|Bolt|Slash|Burst|Spike|Wave", System.Text.RegularExpressions.RegexOptions.Compiled);

        private static readonly Dictionary<Entity, long> _steamLastUp = new();
        private static readonly HashSet<Entity> _steamSeenUp = new(); // jets seen up at least once (so the time since they went down is known)

        /// <summary>True if the player is within `margin` px of a Red Cave steam jet's beam.</summary>
        protected static bool NearSteamJet(int margin)
        {
            var pb = Player.Hitbox;
            pb.Inflate(margin, margin);
            return BotRunner.Entities().Any(e => e.exists && IsSteamBeam(e) && e.Hitbox.Intersects(pb));
        }

        /// <summary>Token s: waits until the screen's steam jets have burned and just stopped: the start of their ~3s harmless spell.</summary>
        protected static IEnumerable<object> SteamJustOff()
        {
            // a whole cycle is longer than the stuck limit, and these waits have their own timeouts
            bool Burning()
            {
                BotRunner.NotStuck();
                return BotRunner.Entities().Any(e => e.exists && IsSteamBeam(e) && e.CurAnimName == "steam_is_harmful");
            }
            if (!BotRunner.Entities().Any(e => e.exists && IsSteamBeam(e)))
            {
                yield break;
            }
            BotRunner.Status = "wait for the steam to burn and stop";
            yield return Until(Burning, 400, "steam jets burning");
            yield return Until(() => !Burning(), 200, "steam jets stopping");
        }

        /// <summary>Token s1: waits until the screen's steam jets start to rise: they burn ~35 frames later (time to step across one).</summary>
        protected static IEnumerable<object> SteamRising()
        {
            bool Rising()
            {
                BotRunner.NotStuck();
                return BotRunner.Entities().Any(e => e.exists && IsSteamBeam(e) && e.CurAnimName == "emerge" && (e.visible || e._flickering));
            }
            if (!BotRunner.Entities().Any(e => e.exists && IsSteamBeam(e)))
            {
                yield break;
            }
            BotRunner.Status = "wait for the steam to rise";
            yield return Until(() => !Rising(), 400, "steam jets not rising");
            yield return Until(Rising, 400, "steam jets rising");
        }

        protected static bool IsSteamBeam(Entity e) => e.GetType().DeclaringType?.Name == "OnOffLaser";

        /// <summary>
        /// A Red Cave steam jet is hidden 2s, then rises harmlessly for ~0.6s, then burns for 2s. It counts as a danger while it burns, and in the last
        /// third of its hidden time (a crossing begun then would meet the burn): so a walker enters only in the first ~70 frames after it went down.
        /// </summary>
        protected static bool SteamDangerous(Entity e)
        {
            if (e.visible || e._flickering)
            {
                _steamLastUp[e] = BotRunner.Frame;
                _steamSeenUp.Add(e);
                // rising counts too: a walker that steps in while it emerges is still in it when the burn starts
                return e.CurAnimName != "recede";
            }
            if (!_steamSeenUp.Contains(e) || !_steamLastUp.TryGetValue(e, out long last))
            {
                // Not seen up since the screen loaded: where it is in its cycle is unknown (its idle timer starts when the screen loads, which
                // is during the scroll, and a walk that assumed a fresh 2s once met the burn). Keep out until it has risen once.
                return true;
            }
            return BotRunner.Frame - last > 70;
        }

        /// <summary>
        /// True if a steam jet lies over (or within 6px of) any of `tiles` and a crossing begun now could meet its burn: it is up (or rising),
        /// or it has been down for more than ~40 frames, or it has not been seen go down yet. A crossing of a few tiles takes well under a second.
        /// </summary>
        protected static bool SteamUnsafeOver(List<Point> tiles)
        {
            foreach (var e in BotRunner.Entities())
            {
                if (!e.exists || !IsSteamBeam(e))
                {
                    continue;
                }
                SteamDangerous(e); // keeps the jet's last-seen-up frame current
                var r = e.Hitbox;
                r.Inflate(6, 6);
                if (!tiles.Any(t => r.Intersects(new Rectangle(t.X * 16, t.Y * 16, 16, 16))))
                {
                    continue;
                }
                if (e.visible || e._flickering || !_steamSeenUp.Contains(e) || BotRunner.Frame - _steamLastUp[e] > 40)
                {
                    return true;
                }
            }
            return false;
        }

        /// <summary>Things that hurt on contact: living enemies and their projectiles (not `except`).</summary>
        protected static List<Entity> Dangers(Entity except = null)
        {
            // the steam jet's nozzle (OnOffLaser) is a harmless solid block that only matches the projectile names: its beam is the danger
            return BotRunner.Entities().Where(e => e.exists && e != except && !(e is Player) && e.width > 0 && e.height > 0 && !(IsSteamBeam(e) && !SteamDangerous(e)) && e.GetType().Name != "OnOffLaser" &&
                (e.GetType().IsDefined(typeof(AnodyneSharp.Entities.Enemy.EnemyAttribute), false) || ProjectileName.IsMatch(e.GetType().Name) || e.GetType().Name is "Four_Shooter" or "BounceDashTrap" or "OnSightDashTrap" or "SuburbKiller" || (e is AnodyneSharp.Entities.Enemy.Crowd.BaseSpikeRoller sr && (sr.CollisionOn || sr.visible)))).ToList();
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
                    if (IsSteamBeam(d) && pb.Intersects(r))
                    {
                        continue; // already in the jet: standing still is no better, let it walk out
                    }
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
            public bool HurtsInAir; // (most things that hurt by touch skip a player in the air; a Rat or a Gas Guy does not)
            public bool Launch; // (a frog's next volley round its corner: the one part of that rough guess a dodge must still keep off)
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

        /// <summary>Can the player stand on r? Like CornersWalkable, but a hole only counts where its 6x4 core (what makes you fall) touches r.</summary>
        private static bool StandsSafe(Rectangle r)
        {
            foreach (var (x, y) in new[] { (r.Left, r.Top), (r.Right - 1, r.Top), (r.Left, r.Bottom - 1), (r.Right - 1, r.Bottom - 1) })
            {
                var t = new Point((int)Math.Floor(x / 16f), (int)Math.Floor(y / 16f));
                if (BotNav.IsHole(t))
                {
                    if (new Rectangle(t.X * 16 + 5, t.Y * 16 + 5, 6, 4).Intersects(r))
                    {
                        return false;
                    }
                }
                else if (!BotNav.Walkable(t))
                {
                    return false;
                }
            }
            return true;
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

        private static readonly System.Reflection.FieldInfo FourTimerField =
            typeof(AnodyneSharp.Entities.Enemy.Redcave.Four_Shooter).GetField("timer", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);

        /// <summary>
        /// A Four_Shooter fires four 4px bullets (50px/s, spawned 8px from its centre) every 1.5s, along the axes (preset frame 0) or the
        /// diagonals (any other frame); its timer says when the next volley is. Its body is solid; the volleys to come are the threat.
        /// </summary>
        private static IEnumerable<Threat> PredictFourShooter(Entity sh)
        {
            var body = new Threat { Name = "Four_Shooter", Margin = 1, Source = sh, Stationary = true };
            for (int f = 0; f <= Horizon; f++)
            {
                body.At[f] = sh.Hitbox;
            }
            yield return body;
            if (FourTimerField == null)
            {
                yield break;
            }
            float timer = (float)FourTimerField.GetValue(sh);
            var dirs = sh.Frame == 0
                ? new[] { new Vector2(1, 0), new Vector2(-1, 0), new Vector2(0, 1), new Vector2(0, -1) }
                : new[] { Vector2.Normalize(new Vector2(1, 1)), Vector2.Normalize(new Vector2(-1, -1)), Vector2.Normalize(new Vector2(-1, 1)), Vector2.Normalize(new Vector2(1, -1)) };
            var c = new Vector2(sh.Hitbox.Center.X, sh.Hitbox.Center.Y);
            int first = (int)Math.Floor(Math.Max(timer, 0f) * 60f) + 1;
            for (int s = first; s <= Horizon; s += 90)
            {
                foreach (var d in dirs)
                {
                    var th = new Threat { Name = "Four_Shooter(bullet)", Margin = 1 };
                    for (int f = s; f <= Horizon; f++)
                    {
                        var pos = c + d * (8f + 50f * (f - s) / 60f) - new Vector2(2, 2);
                        var r = new Rectangle((int)pos.X, (int)pos.Y, 4, 4);
                        if (!CornersWalkable(r))
                        {
                            break; // it pops on the first wall
                        }
                        th.At[f] = r;
                    }
                    yield return th;
                }
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
                if (name == "Four_Shooter")
                {
                    result.AddRange(PredictFourShooter(d));
                    continue;
                }
                if (d is AnodyneSharp.Entities.Enemy.Crowd.BaseSpikeRoller)
                {
                    result.Add(PredictRoller(d));
                    continue;
                }
                if (d is AnodyneSharp.Entities.Enemy.Crowd.RotatorBullet)
                {
                    result.Add(PredictRotatorBullet(d));
                    continue;
                }
                if (d is AnodyneSharp.Entities.Enemy.Crowd.Frog.BurstBullet)
                {
                    if (PredictFrogBullet(d) is Threat fb)
                    {
                        result.Add(fb);
                    }
                    continue;
                }
                if (name == "OnSightDashTrap" && d.velocity == Vector2.Zero)
                {
                    result.AddRange(PredictSightTrap(d));
                    continue;
                }
                if (d is AnodyneSharp.Entities.Enemy.Apartment.Rat)
                {
                    result.Add(PredictRat(d));
                    continue;
                }
                if (d is AnodyneSharp.Entities.Enemy.Apartment.GasGuy)
                {
                    // It sets its position straight towards ours at 12px/s on each axis (velocity stays 0, so it would read as standing):
                    // it homes in on where we are now.
                    var gt = new Threat { Name = "GasGuy", Source = d, Margin = 2, HurtsInAir = true };
                    var pp = Player.Position;
                    for (int f = 0; f <= Horizon; f++)
                    {
                        float step = 12f * f / 60f;
                        float gx = d.Position.X + Math.Clamp(pp.X - d.Position.X, -step, step), gy = d.Position.Y + Math.Clamp(pp.Y - d.Position.Y, -step, step);
                        var r = d.Hitbox;
                        r.Offset((int)(gx - d.Position.X), (int)(gy - d.Position.Y));
                        gt.At[f] = r;
                    }
                    result.Add(gt);
                    continue;
                }
                if (name is "Shieldy" or "BounceDashTrap")
                {
                    result.Add(PredictPatroller(d)); // both walk a line and turn round at the first wall (the trap at 80px/s)
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
        /// A Rat walks at 40px/s and turns clockwise (down, left, up, right) the frame after a 1px feeler 7px ahead of its centre meets a wall,
        /// dust or any special floor (water, hole, ...): replay that. Only the 8x8 square 4px in from its hitbox's corner hurts.
        /// </summary>
        private static Threat PredictRat(Entity e)
        {
            var t = new Threat { Name = "Rat", Source = e, Margin = 1, HurtsInAir = true };
            var path = SimRat(new Vector2(e.Hitbox.X, e.Hitbox.Y), e.velocity, e.Hitbox.Width, RatBlocks(), Horizon);
            for (int f = 0; f <= Horizon; f++)
            {
                t.At[f] = RatBite(path[f].pos);
            }
            return t;
        }

        private static readonly System.Reflection.FieldInfo TrapIdle =
            typeof(AnodyneSharp.Entities.Enemy.Apartment.OnSightDashTrap).GetField("_idlepos", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);

        /// <summary>Where the screen's dash traps sit: an on-sight one that is charging or coming back is taken at its resting place.</summary>
        private static List<Rectangle> TrapRects()
        {
            var res = new List<Rectangle>();
            foreach (var e in BotRunner.Entities().Where(x => x.exists && x is AnodyneSharp.Entities.Enemy.Apartment.DashTrap))
            {
                var r = e.Hitbox;
                if (e is AnodyneSharp.Entities.Enemy.Apartment.OnSightDashTrap && TrapIdle?.GetValue(e) is Vector2 idle)
                {
                    r = new Rectangle((int)(idle.X + r.X - e.Position.X), (int)(idle.Y + r.Y - e.Position.Y), r.Width, r.Height);
                }
                res.Add(r);
            }
            return res;
        }

        /// <summary>The part of a Rat (hitbox top-left at p) that hurts.</summary>
        private static Rectangle RatBite(Vector2 p) => new((int)p.X + 4, (int)p.Y + 4, 8, 8);

        /// <summary>
        /// What turns a Rat besides the map: dust, and the solid map-like entities (gates, key blocks, signs; switch pillars only while fully up).
        /// With `pillarsFlipped` the switch pillars are taken the other way up, as after a hit on the pillar switch. `except` leaves dust out.
        /// </summary>
        private static List<Rectangle> RatBlocks(bool pillarsFlipped = false, Entity except = null)
        {
            var res = new List<Rectangle>();
            foreach (var e in BotRunner.Entities().Where(x => x.exists && x != except))
            {
                if (e is AnodyneSharp.Entities.Dust)
                {
                    res.Add(e.Hitbox);
                }
                else if (e is AnodyneSharp.Entities.Gadget.Switches.SwitchPillar)
                {
                    bool up;
                    try
                    {
                        up = e.Frame == 0;
                    }
                    catch (NullReferenceException)
                    {
                        up = true;
                    }
                    if (up != pillarsFlipped)
                    {
                        res.Add(e.Hitbox);
                    }
                }
                else if (e.Solid && System.Reflection.CustomAttributeExtensions.GetCustomAttributes<AnodyneSharp.Entities.CollisionAttribute>(e.GetType()).Any(c => c.PartOfMap))
                {
                    res.Add(e.Hitbox);
                }
            }
            return res;
        }

        /// <summary>
        /// A Rat's hitbox top-left and velocity for each of the next `frames` frames (index 0 = now): it walks straight and turns clockwise
        /// the frame after a 1px feeler 7px ahead of its centre meets a wall, a special floor or one of `dusts`.
        /// </summary>
        private static (Vector2 pos, Vector2 v)[] SimRat(Vector2 pos, Vector2 v, int size, List<Rectangle> dusts, int frames)
        {
            var map = BotNav.CurrentMap;
            var res = new (Vector2, Vector2)[frames + 1];
            bool turn = false;
            // A dash trap kills a rat that walks into it: from then on it is nowhere (far off the map, still).
            var traps = TrapRects();
            for (int f = 0; f <= frames; f++)
            {
                if (pos.X > -500 && traps.Any(t => t.Intersects(new Rectangle((int)pos.X, (int)pos.Y, size, size))))
                {
                    pos = new Vector2(-1000, -1000);
                    v = Vector2.Zero;
                }
                res[f] = (pos, v);
                if (map == null || v == Vector2.Zero)
                {
                    continue;
                }
                if (turn)
                {
                    v = new Vector2(-v.Y, v.X);
                    turn = false;
                }
                pos += v / 60f;
                var dir = Math.Abs(v.X) > Math.Abs(v.Y) ? new Vector2(Math.Sign(v.X), 0) : new Vector2(0, Math.Sign(v.Y));
                var p = pos + new Vector2(size / 2f, size / 2f) + dir * 7;
                var (al, ev) = map.GetTileClass(new Point((int)Math.Floor(p.X / 16f), (int)Math.Floor(p.Y / 16f)));
                turn = al != Touching.NONE || ev != AnodyneSharp.MapData.Tiles.CollisionEventType.NONE;
                foreach (var d in dusts)
                {
                    turn |= d.Contains((int)p.X, (int)p.Y);
                }
            }
            return res;
        }

        /// <summary>
        /// A TeleGuy hurts by touch while it stands. Hit it and it poofs (0.5s), reappears 16px from us on the side opposite our facing, waits
        /// 0.5s (harmless, and deaf to the broom), then for 1.5s charges at 60px/s back to where it was hit (where a hit kills it half the time,
        /// else it poofs again). So right after the hit turn our back on it: it comes back on the side it was hit from, charges away from us
        /// (to where it was), and we turn round and hit it as its charge starts. No sidestep needed, which a one-tile corridor would not allow.
        /// </summary>
        protected static IEnumerable<object> KillTeleGuy(Entity tg)
        {
            long lastPoof = -1000;
            KeyFunctions hitFace = KeyFunctions.Down;
            bool swung = false;
            for (int i = 0; i < 2400 && tg.exists && tg.CurAnimName != "dying"; i++)
            {
                if (BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "GasGuy" && (e.Center - Player.Center).Length() < 44))
                {
                    yield break; // a Gas Guy on us comes first (its gas hurts every 0.8s): back to the fight, which goes for it
                }
                var pb = Player.Hitbox;
                var tb = tg.Hitbox;
                var me = new Vector2(pb.X, pb.Y);
                string anim = tg.CurAnimName ?? "";
                var back = hitFace switch { KeyFunctions.Up => KeyFunctions.Down, KeyFunctions.Down => KeyFunctions.Up, KeyFunctions.Left => KeyFunctions.Right, _ => KeyFunctions.Left };
                if (anim == "poof")
                {
                    lastPoof = BotRunner.Frame;
                    swung = false;
                    BotRunner.Status = "teleguy: poofed, back turned";
                    yield return Player.facing == FacingOf(back) ? K() : K(back);
                    continue;
                }
                long since = BotRunner.Frame - lastPoof;
                if (since < 140)
                {
                    // back beside us on the side we hit it from: face it again, and strike once its charge (after 0.5s) has begun
                    if (Player.facing != FacingOf(hitFace))
                    {
                        yield return K(hitFace);
                        continue;
                    }
                    if (since >= 36 && !swung)
                    {
                        BotRunner.Status = "teleguy: strike its charge";
                        swung = true;
                        yield return Tap(KeyFunctions.Accept);
                        yield return Wait(10);
                        continue;
                    }
                    BotRunner.Status = "teleguy: wait for its charge";
                    yield return K();
                    continue;
                }
                // Standing: walk up to it (not into it), face it and swing.
                var tc = new Vector2(tb.Center.X, tb.Center.Y);
                var pc = new Vector2(pb.Center.X, pb.Center.Y);
                var off = pc - tc;
                Vector2 spot;
                KeyFunctions face;
                if (Math.Abs(off.X) > Math.Abs(off.Y))
                {
                    spot = new Vector2(off.X > 0 ? tb.Right + 3 : tb.Left - 3 - 10, tb.Center.Y - 6);
                    face = off.X > 0 ? KeyFunctions.Left : KeyFunctions.Right;
                }
                else
                {
                    spot = new Vector2(tb.Center.X - 5, off.Y > 0 ? tb.Bottom + 3 : tb.Top - 3 - 12);
                    face = off.Y > 0 ? KeyFunctions.Up : KeyFunctions.Down;
                }
                if (!StandsSafe(new Rectangle((int)spot.X, (int)spot.Y, 10, 12)))
                {
                    // That side is wall (we stood beside it, below a wall, for 300 frames): the nearest side we can stand on.
                    var sides = new (Vector2 at, KeyFunctions f)[]
                    {
                        (new Vector2(tb.Right + 3, tb.Center.Y - 6), KeyFunctions.Left),
                        (new Vector2(tb.Left - 13, tb.Center.Y - 6), KeyFunctions.Right),
                        (new Vector2(tb.Center.X - 5, tb.Bottom + 3), KeyFunctions.Up),
                        (new Vector2(tb.Center.X - 5, tb.Top - 15), KeyFunctions.Down),
                    };
                    var ok = sides.Where(sd => StandsSafe(new Rectangle((int)sd.at.X, (int)sd.at.Y, 10, 12))).OrderBy(sd => (sd.at - me).Length()).ToList();
                    if (ok.Count > 0)
                    {
                        spot = ok[0].at;
                        face = ok[0].f;
                    }
                }
                if ((spot - me).Length() > 2f)
                {
                    BotRunner.Status = "teleguy: walk up to it";
                    if ((spot - me).Length() > 20f)
                    {
                        if (BotNav.FindPath(BotNav.TileOf(Pos), BotNav.TileOf(spot)) == null)
                        {
                            yield return K(); // (over a hole edge, falling or respawning: no route from here yet)
                            continue;
                        }
                        yield return GoTo(spot.X, spot.Y, maxReplans: 1);
                        continue;
                    }
                    float sx = spot.X - me.X, sy = spot.Y - me.Y;
                    var ks = new List<KeyFunctions>();
                    if (Math.Abs(sx) > 1f) { ks.Add(sx < 0 ? KeyFunctions.Left : KeyFunctions.Right); }
                    if (Math.Abs(sy) > 1f) { ks.Add(sy < 0 ? KeyFunctions.Up : KeyFunctions.Down); }
                    yield return K(ks.ToArray());
                    continue;
                }
                hitFace = face;
                yield return Hold(1, face);
                yield return Tap(KeyFunctions.Accept);
                yield return Wait(4);
            }
        }

        private static Facing FacingOf(KeyFunctions k) => k switch { KeyFunctions.Up => Facing.UP, KeyFunctions.Down => Facing.DOWN, KeyFunctions.Left => Facing.LEFT, _ => Facing.RIGHT };

        /// <summary>
        /// A Rat walks a fixed loop and bites with its middle only: chasing it means meeting its bite. Instead replay its walk, pick a spot a
        /// little ahead of where it will be once we can get there, stand there facing back along its way and swing as it walks up.
        /// </summary>
        protected static IEnumerable<object> KillRat(Entity rat)
        {
            int size = rat.Hitbox.Width;
            for (int attempt = 0; attempt < 3 && rat.exists; attempt++)
            {
                var path = SimRat(new Vector2(rat.Hitbox.X, rat.Hitbox.Y), rat.velocity, size, RatBlocks(), 420);
                var here = BotNav.TileOf(Pos);
                var others = PredictThreats(rat); // the rest of the room (traps, bullets, other rats) must leave the spot alone too
                (int f, Vector2 tl, KeyFunctions face)? pick = null;
                for (int f = 40; f <= 380 && pick == null; f += 4)
                {
                    var v = path[f].v;
                    if (v == Vector2.Zero || path[f + 30].v != v)
                    {
                        continue; // it turns before it reaches us
                    }
                    var dir = Vector2.Normalize(v);
                    var rc = path[f].pos + new Vector2(size / 2f, size / 2f);
                    var tl = rc + dir * 24 - new Vector2(5, 6);
                    var box = new Rectangle((int)tl.X, (int)tl.Y, 10, 12);
                    if (!StandsSafe(box))
                    {
                        continue;
                    }
                    bool crossed = false;
                    for (int g = 0; g < f && !crossed; g++)
                    {
                        var bite = RatBite(path[g].pos);
                        bite.Inflate(3, 3);
                        crossed = bite.Intersects(box);
                    }
                    if (crossed)
                    {
                        continue; // it walks over the spot on the way there
                    }
                    bool unsafeSpot = false;
                    var wide = box;
                    wide.Inflate(2, 2);
                    foreach (var th in others)
                    {
                        for (int g = 0; g <= Horizon && !unsafeSpot; g += 3)
                        {
                            unsafeSpot = th.At[g] is Rectangle tr && tr.Intersects(wide);
                        }
                    }
                    if (unsafeSpot)
                    {
                        continue;
                    }
                    var route = BotNav.FindPath(here, t => t == BotNav.TileOf(tl), BotNav.TileOf(tl));
                    if (route == null || (route.Count + 1) * 16 / WalkPxPerFrame + 20 > f)
                    {
                        continue;
                    }
                    var face = Math.Abs(dir.X) > Math.Abs(dir.Y) ? (dir.X > 0 ? KeyFunctions.Left : KeyFunctions.Right) : (dir.Y > 0 ? KeyFunctions.Up : KeyFunctions.Down);
                    pick = (f, tl, face);
                }
                if (pick == null)
                {
                    System.Console.Error.WriteLine($"  rat: no ambush spot within reach of {rat.Hitbox}");
                    yield break;
                }
                var (pf, ptl, pface) = pick.Value;
                long due = BotRunner.Frame + pf;
                BotRunner.Status = $"rat: ambush at {ptl.X:0},{ptl.Y:0} facing {pface}";
                yield return GoTo(ptl.X, ptl.Y);
                for (int k = 0; k < 90 && (new Vector2(Player.Hitbox.X, Player.Hitbox.Y) - ptl).Length() > 1f; k++)
                {
                    // the last pixels with the dodger on (a plain walk here stepped into a rotator's fireball)
                    var pp = new Vector2(Player.Hitbox.X, Player.Hitbox.Y);
                    var want = Math.Abs(ptl.X - pp.X) > 1f ? (ptl.X < pp.X ? KeyFunctions.Left : KeyFunctions.Right) : (ptl.Y < pp.Y ? KeyFunctions.Up : KeyFunctions.Down);
                    yield return SafeMove(want, dist: (ptl - pp).Length());
                }
                yield return Hold(1, pface);
                for (int i = 0; i < 200 && rat.exists && BotRunner.Frame < due + 60; i++)
                {
                    var pb = Player.Hitbox;
                    var rb = rat.Hitbox;
                    int gap = pface switch
                    {
                        KeyFunctions.Left => pb.Left - rb.Right,
                        KeyFunctions.Right => rb.Left - pb.Right,
                        KeyFunctions.Up => pb.Top - rb.Bottom,
                        _ => rb.Top - pb.Bottom
                    };
                    bool inLine = pface is KeyFunctions.Left or KeyFunctions.Right ? Math.Abs(rb.Center.Y - pb.Center.Y) < 10 : Math.Abs(rb.Center.X - pb.Center.X) < 10;
                    if (inLine && gap >= -2 && gap <= 9)
                    {
                        yield return Tap(KeyFunctions.Accept);
                        yield return Wait(12);
                        break;
                    }
                    yield return K();
                }
            }
        }

        /// <summary>
        /// Rats on buttons: two buttons held at once open a gate, and either we hold one while a Rat walks over the other, or two Rats are on
        /// both at once. Rats turn clockwise at walls, dust, gates and raised switch pillars (and die on dash traps), so this searches what we
        /// can change (move one dust, when to start doing it, or hit the pillar switch to flip the pillars) for the layout after which, from
        /// about where the rats are when we are done, the buttons get pressed and no rat bites where we stand; then does that, waits for the
        /// puzzle to count and steps out of the rats' way. When nothing works because the rats die too soon, it first moves the dust so they
        /// live (a loop of their own) and plans again from there.
        /// </summary>
        protected static IEnumerable<object> RatOnButton(int limit, int stage = 0, int retries = 0)
        {
            var rats = BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Enemy.Apartment.Rat).ToList();
            var buttons = BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Gadget.Button).ToList();
            if (rats.Count == 0 && buttons.Count == 2 && retries < 3 && _cameFrom.HasValue)
            {
                // The rat ran into a dash trap (or the broom): step back out and in again, which brings the screen's rats back.
                var g = Grid;
                var back = _cameFrom.Value;
                System.Console.Error.WriteLine($"  rat on button: the rats are dead; out to screen {back.X},{back.Y} and back in to bring them back (retry {retries + 1})");
                yield return GoScreenTo(back.X, back.Y);
                yield return Wait(20);
                yield return GoScreenTo(g.X, g.Y);
                yield return Wait(20);
                yield return RatOnButton(limit, 0, retries + 1);
                yield break;
            }
            if (rats.Count == 0 || buttons.Count != 2)
            {
                throw new Exception($"rat on button: needs rats and two buttons here ({rats.Count} rats, {buttons.Count} buttons)");
            }
            var dustEs = BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Dust).ToList();
            var pswitch = BotRunner.Entities().FirstOrDefault(e => e.exists && e is AnodyneSharp.Entities.Gadget.Switches.PillarSwitch);
            bool anyPillar = BotRunner.Entities().Any(e => e.exists && e is AnodyneSharp.Entities.Gadget.Switches.SwitchPillar);
            var grid = Grid;
            int size = rats[0].Hitbox.Width;
            var o4 = new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) };
            Point TileAt(Rectangle r) => new((int)Math.Floor(r.Center.X / 16f), (int)Math.Floor(r.Center.Y / 16f));
            var buttonTiles = buttons.Select(b => TileAt(b.Hitbox)).ToList();
            var dustTiles = dustEs.Select(d => TileAt(d.Hitbox)).ToList();
            bool Floor(Point t) => t.X >= grid.X * 10 && t.X < grid.X * 10 + 10 && t.Y >= grid.Y * 10 && t.Y < grid.Y * 10 + 10 && BotNav.Walkable(t);
            bool Free(Point t) => Floor(t) && !buttonTiles.Contains(t) && !dustTiles.Contains(t);
            var here = BotNav.TileOf(Pos);
            Rectangle StandRect(Point t) => new(t.X * 16 + 1, t.Y * 16, 14, 16); // where we stand on a tile (a little wider than our hitbox)
            // Where we hit the pillar switch from: a free tile next to it that we can walk to on this screen.
            Point? flipFrom = null;
            if (pswitch != null && anyPillar)
            {
                var st = TileAt(pswitch.Hitbox);
                foreach (var o in o4)
                {
                    var q = new Point(st.X + o.X, st.Y + o.Y);
                    var way = q == here ? new List<Point>() : BotNav.FindPath(here, q);
                    if (Free(q) && way != null && way.All(Floor)) // on this screen: leaving it would respawn the rats where they started
                    {
                        flipFrom = q;
                        break;
                    }
                }
            }
            // About how long a walk between tiles takes (a little over the straight-line steps), in frames.
            int WalkFrames(Point p, Point q) => (int)((Math.Abs(p.X - q.X) + Math.Abs(p.Y - q.Y)) * 1.2f * 16 / WalkPxPerFrame);
            // How well a plan works. The rats go on under `before` for b frames (we walk to the dust, maybe wait there at `waitRect`), then
            // under `during` until we are done (about `setup` frames from now), then under `after`. From around when we are done, the share
            // of runs where within 25s the buttons get pressed (by a rat on the far one while we hold `mine`, or with mine < 0 by rats on
            // both) and no rat has bitten `ours` (our button, or wherever we wait) - or, with `survive`, where every rat is still alive.
            const int Horizon25 = 1200;
            // Runs already simulated, by plan (the button we hold changes only how they are judged, not where the rats go).
            var runCache = new Dictionary<(int, int), List<List<(Vector2 pos, Vector2 v)[]>>>();
            float Score(List<Rectangle> before, int b, Rectangle? waitRect, int waitFrom, List<Rectangle> during, int setup, List<Rectangle> after, int mine, Rectangle ours, bool survive, out int when, (int, int)? key = null)
            {
                int total = setup + 180;
                when = 0;
                // (keeping them alive is a race against a trap: judge it by about when we will be done, not by a late finish)
                int lo = Math.Max(b, setup - (survive ? 30 : 60)), hi = survive ? Math.Min(total, setup + 60) : total;
                int step = survive ? 15 : 40;
                List<List<(Vector2 pos, Vector2 v)[]>> phases = null;
                if (key == null || !runCache.TryGetValue(key.Value, out phases))
                {
                    var ways = new List<(Vector2 pos, Vector2 v)[]>();
                    foreach (var r in rats)
                    {
                        var p1 = SimRat(new Vector2(r.Hitbox.X, r.Hitbox.Y), r.velocity, size, before, Math.Max(b, 0));
                        var p2 = SimRat(p1[p1.Length - 1].pos, p1[p1.Length - 1].v, size, during, total - b);
                        var w = p1.Take(p1.Length - 1).Concat(p2).ToArray();
                        if (waitRect != null && Enumerable.Range(waitFrom, Math.Max(0, b + 30 - waitFrom)).Any(f => f < w.Length && RatBite(w[f].pos).Intersects(waitRect.Value)))
                        {
                            ways = null; // it bites us while we wait by the dust
                            break;
                        }
                        ways.Add(w);
                    }
                    phases = new List<List<(Vector2 pos, Vector2 v)[]>>();
                    if (ways != null)
                    {
                        for (int s = lo; s <= hi; s += step)
                        {
                            phases.Add(ways.Select(w => SimRat(w[s].pos, w[s].v, size, after, Horizon25)).ToList());
                        }
                    }
                    if (key != null)
                    {
                        runCache[key.Value] = phases;
                    }
                }
                int ok = 0, n = 0;
                foreach (var runs in phases)
                {
                    n++;
                    if (survive)
                    {
                        ok += runs.All(r => r[Horizon25].pos.X > -500) ? 1 : 0;
                        continue;
                    }
                    int hit = -1;
                    bool bit = false;
                    for (int f = 0; f <= Horizon25 && !bit && hit < 0; f++)
                    {
                        var hbs = runs.Select(r => new Rectangle((int)r[f].pos.X, (int)r[f].pos.Y, size, size)).ToList();
                        bool on0 = hbs.Any(h => h.Intersects(buttons[0].Hitbox)), on1 = hbs.Any(h => h.Intersects(buttons[1].Hitbox));
                        if (mine < 0 ? on0 && on1 : (mine == 0 ? on1 : on0))
                        {
                            hit = f;
                        }
                        bit = runs.Any(r => RatBite(r[f].pos).Intersects(ours));
                    }
                    if (hit >= 0 && !bit)
                    {
                        ok++;
                        when = Math.Max(when, hit);
                    }
                }
                return n == 0 ? 0 : ok / (float)(Math.Max(n, (hi - lo) / step + 1));
            }
            // Free tiles within two steps of t that we can walk to without leaving the screen, t first.
            List<Point> NearFree(Point t)
            {
                var res = new List<Point> { t };
                for (int dy = -2; dy <= 2; dy++)
                {
                    for (int dx = -2; dx <= 2; dx++)
                    {
                        var q = new Point(t.X + dx, t.Y + dy);
                        if (q != t && Free(q) && BotNav.FindPath(t, q) is { } way && way.All(Floor) && way.Count <= 4)
                        {
                            res.Add(q);
                        }
                    }
                }
                return res;
            }
            // Tiles in line with an idle on-sight dash trap: standing there sets it off.
            var trapHomes = TrapRects();
            bool InTrapLine(Point t) => trapHomes.Any(r => TileAt(r).X == t.X || TileAt(r).Y == t.Y);
            // Tiles a dust cannot be put on: under a trap, an enemy or anything else solid.
            var occupied = BotRunner.Entities().Where(e => e.exists && e.GetType().Namespace?.StartsWith("AnodyneSharp.Entities.Enemy") == true).Select(e => e.Hitbox)
                .Concat(TrapRects()).Concat(RatBlocks().Where(r => !dustEs.Any(d => d.Hitbox == r))).ToList();
            bool DustFits(Point t) => Free(t) && !occupied.Any(r => r.Intersects(new Rectangle(t.X * 16 + 2, t.Y * 16 + 2, 12, 12)));
            // Where we sweep a dust up from: a free tile next to it (on this screen), out of the dash traps' lines if we can.
            Point? PickFrom(Point dt) => o4.Select(o => new Point(dt.X + o.X, dt.Y + o.Y)).Where(Free).OrderBy(InTrapLine).Cast<Point?>().FirstOrDefault();
            float best = -1;
            int bestWhen = int.MaxValue;
            (int mine, bool flip, int dust, Point to, Point stand, Point wait, int delay)? plan = null;
            void Consider(float sc, int w, (int, bool, int, Point, Point, Point, int) p)
            {
                if (sc > best + 0.001f || (Math.Abs(sc - best) < 0.001f && w < bestWhen))
                {
                    best = sc;
                    bestWhen = w;
                    plan = p;
                }
            }
            var now = RatBlocks();
            // Every way of moving one dust: (which, from where we sweep it, where it goes, where we stand to put it there).
            var moves = new List<(int di, Point pick, Point to, Point stand, List<Rectangle> without, List<Rectangle> after)>();
            for (int di = 0; di < dustEs.Count; di++)
            {
                var pick = PickFrom(dustTiles[di]);
                if (pick == null)
                {
                    continue;
                }
                var without = RatBlocks(false, dustEs[di]);
                for (int ty = grid.Y * 10; ty < grid.Y * 10 + 10; ty++)
                {
                    for (int tx = grid.X * 10; tx < grid.X * 10 + 10; tx++)
                    {
                        var to = new Point(tx, ty);
                        if (!DustFits(to))
                        {
                            continue;
                        }
                        var stand = o4.Select(o => new Point(tx + o.X, ty + o.Y)).Where(Free).OrderBy(InTrapLine).FirstOrDefault();
                        if (stand == Point.Zero)
                        {
                            continue;
                        }
                        moves.Add((di, pick.Value, to, stand, without, new List<Rectangle>(without) { new Rectangle(tx * 16, ty * 16, 16, 16) }));
                    }
                }
            }
            if (stage == 0)
            {
                for (int mine = -1; mine < 2; mine++)
                {
                    foreach (bool flip in new[] { false, true })
                    {
                        if (flip && pswitch == null)
                        {
                            continue;
                        }
                        var after = RatBlocks(flip);
                        var from = flip && flipFrom != null ? flipFrom.Value : here;
                        // Holding a button we wait on it; otherwise anywhere near where we end up that the rats leave alone.
                        var waits = mine >= 0 ? new List<Point> { buttonTiles[mine] } : NearFree(from);
                        float sc = -1;
                        int w = 0;
                        Point waitTile = from;
                        foreach (var wt in waits)
                        {
                            int setup = (flip ? WalkFrames(here, from) + 30 : 0) + WalkFrames(from, wt);
                            float c = Score(now, 0, null, 0, now, setup, after, mine, StandRect(wt), false, out int cw);
                            if (c > sc + 0.001f)
                            {
                                sc = c;
                                w = cw;
                                waitTile = wt;
                            }
                        }
                        System.Console.Error.WriteLine($"  rat on button: " + (mine < 0 ? $"rats on both (we wait at {waitTile})" : $"we hold {buttonTiles[mine]}") + (flip ? ", pillars flipped" : "") + $": {sc:P0} within {w}f" + (flip && flipFrom == null ? " (switch out of reach)" : ""));
                        if (flip && flipFrom == null)
                        {
                            continue;
                        }
                        Consider(sc, w, (mine, flip, -1, Point.Zero, Point.Zero, waitTile, 0));
                    }
                }
            }
            for (int mi = 0; mi < moves.Count; mi++)
            {
                var mv = moves[mi];
                int walk1 = WalkFrames(here, mv.pick);
                // (about as long to either button: the rats' ways are worked out once for all three ways of waiting)
                int toButtons = (WalkFrames(mv.stand, buttonTiles[0]) + WalkFrames(mv.stand, buttonTiles[1])) / 2;
                // (start at once, or wait by the dust for the rats to come round to the right place first)
                for (int delay = 0; delay <= 300; delay += 60)
                {
                    int b = walk1 + delay;
                    int setup = b + 30 + WalkFrames(mv.pick, mv.stand) + 30 + toButtons;
                    for (int mine = -1; mine < 2; mine++)
                    {
                        var waitTile = mine >= 0 ? buttonTiles[mine] : mv.stand;
                        float sc = Score(now, b, StandRect(mv.pick), walk1, mv.without, setup, mv.after, mine, StandRect(waitTile), false, out int w, (mi, delay));
                        Consider(sc, w + delay, (mine, false, mv.di, mv.to, mv.stand, waitTile, delay));
                    }
                }
            }
            if ((plan == null || best < 0.5f) && stage == 0)
            {
                // Nothing works as things are, perhaps because the rats run into a dash trap first: put the dust where they live on.
                float bestLive = -1;
                (int di, Point pick, Point to, Point stand, List<Rectangle> without, List<Rectangle> after)? keep = null;
                foreach (var mv in moves)
                {
                    int setup = WalkFrames(here, mv.pick) + 30 + WalkFrames(mv.pick, mv.stand) + 30;
                    float sc = Score(now, WalkFrames(here, mv.pick), null, 0, mv.without, setup, mv.after, -1, Rectangle.Empty, true, out _);
                    if (sc > bestLive + 0.001f || (Math.Abs(sc - bestLive) < 0.001f && keep != null && WalkFrames(mv.pick, mv.stand) < WalkFrames(keep.Value.pick, keep.Value.stand)))
                    {
                        bestLive = sc;
                        keep = mv;
                    }
                }
                System.Console.Error.WriteLine($"  rat on button: {moves.Count} dust moves, the best for the rats' lives {bestLive:P0}" + (keep == null ? "" : $" ({dustTiles[keep.Value.di]} -> {keep.Value.to})"));
                if (keep != null && bestLive >= (retries < 3 && _cameFrom.HasValue ? 0.25f : 0.5f)) // a long shot is fine while a fresh try is left
                {
                    var k = keep.Value;
                    System.Console.Error.WriteLine($"  rat on button: nothing works yet (best {best:P0}); first the dust {dustTiles[k.di]} -> {k.to} (from {k.stand}) keeps the rats alive ({bestLive:P0})");
                    yield return MoveDust(dustTiles[k.di], k.pick, k.to, k.stand, 0);
                    yield return RatOnButton(limit, 1, retries);
                    yield break;
                }
            }
            if (plan == null || best < 0.5f)
            {
                throw new Exception($"rat on button: nothing we can change gets the rats onto the buttons (best {best:0.00})");
            }
            var (m, flipIt, dIdx, dest, standAt, waitAt, wait0) = plan.Value;
            System.Console.Error.WriteLine($"  rat on button: " + (m < 0 ? $"rats on both (we wait at {waitAt})" : $"we hold {buttonTiles[m]}") + (flipIt ? ", pillars flipped" : "")
                + (dIdx < 0 ? "" : $", dust {dustTiles[dIdx]} -> {dest} (from {standAt}" + (wait0 > 0 ? $", after waiting {wait0}f" : "") + ")") + $", works from {best:P0} of the rats' ways, within {bestWhen}f");
            int start = GlobalState.PUZZLES_SOLVED;
            bool fight = AutoFight;
            AutoFight = false; // the rats are the tool here, not targets
            try
            {
                if (flipIt)
                {
                    var st = TileAt(pswitch.Hitbox);
                    var ff = flipFrom.Value;
                    int was = GlobalState.PillarSwitchOn;
                    yield return StandFaceAccept(ff.X, ff.Y, FaceChar(new Point(st.X - ff.X, st.Y - ff.Y)));
                    yield return Wait(20);
                    if (GlobalState.PillarSwitchOn == was)
                    {
                        throw new Exception($"rat on button: hitting the pillar switch at {st} did nothing");
                    }
                }
                if (dIdx >= 0)
                {
                    yield return MoveDust(dustTiles[dIdx], PickFrom(dustTiles[dIdx]).Value, dest, standAt, wait0);
                }
                if (m >= 0 || waitAt != BotNav.TileOf(Pos))
                {
                    var bc = BotNav.CentreOf(waitAt);
                    yield return GoTo(bc.X, bc.Y);
                    yield return WalkTo(bc.X, bc.Y, tol: 1f);
                }
                for (int f = 0; f < limit && GlobalState.PUZZLES_SOLVED < start + 2; f++)
                {
                    yield return Wait(1);
                }
                if (GlobalState.PUZZLES_SOLVED < start + 2)
                {
                    throw new Exception($"rat on button: the rats did not get onto the buttons in {limit} frames");
                }
                // Done (the gate stays open): get off the rats' way before one of them walks into us.
                var blocks = RatBlocks();
                var soon = rats.Where(r => r.exists).Select(r => SimRat(new Vector2(r.Hitbox.X, r.Hitbox.Y), r.velocity, size, blocks, 120)).ToList();
                bool Bitten(Point t) => soon.Any(w => w.Any(x => RatBite(x.pos).Intersects(StandRect(t))));
                var me = BotNav.TileOf(Pos);
                if (Bitten(me))
                {
                    var clear = o4.Select(o => new Point(me.X + o.X, me.Y + o.Y)).FirstOrDefault(q => Floor(q) && !Bitten(q));
                    if (clear != Point.Zero)
                    {
                        var cc = BotNav.CentreOf(clear);
                        yield return WalkTo(cc.X, cc.Y, tol: 1.5f, timeout: 60);
                    }
                }
            }
            finally
            {
                AutoFight = fight;
            }
        }

        /// <summary>Sweep up the dust on tile dt from `pick` (after waiting `delay` frames there) and put it down on `to` from `stand`.</summary>
        private static IEnumerable<object> MoveDust(Point dt, Point pick, Point to, Point stand, int delay)
        {
            if (delay > 0)
            {
                var pc = BotNav.CentreOf(pick);
                yield return GoTo(pc.X, pc.Y);
                yield return WalkTo(pc.X, pc.Y, tol: 1.5f, timeout: 120);
                yield return Wait(delay);
            }
            yield return StandFaceAccept(pick.X, pick.Y, FaceChar(new Point(dt.X - pick.X, dt.Y - pick.Y)));
            yield return Wait(10);
            if (Player.broom.dust == null)
            {
                throw new Exception($"rat on button: did not pick up the dust at {dt}");
            }
            yield return StandFaceAccept(stand.X, stand.Y, FaceChar(new Point(to.X - stand.X, to.Y - stand.Y)));
            yield return Wait(10);
        }

        /// <summary>
        /// A screen of switch pillars with pillar switches: a hit on any switch raises the lowered pillars and lowers the raised ones (all of
        /// them, on every screen). Search where we can walk and which switch to hit when (a breadth-first search over our tile and whether
        /// the pillars are flipped from how they are now) for the way to tile (gx,gy) on this screen, then walk it a leg at a time, hitting
        /// the switches on the way. A hit that would raise a pillar where we stand is not taken.
        /// </summary>
        protected static IEnumerable<object> PillarMaze(float gx, float gy)
        {
            var goal = BotNav.TileOf(new Vector2(gx, gy));
            var grid = Grid;
            Point TileAt(Rectangle r) => new((int)Math.Floor(r.Center.X / 16f), (int)Math.Floor(r.Center.Y / 16f));
            bool OnScreen(Point t) => t.X >= grid.X * 10 && t.X < grid.X * 10 + 10 && t.Y >= grid.Y * 10 && t.Y < grid.Y * 10 + 10;
            var o4 = new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) };
            for (int round = 0; round < 12; round++)
            {
                var here = BotNav.TileOf(Pos);
                var pillars = BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Gadget.Switches.SwitchPillar)
                    .ToDictionary(e => TileAt(e.Hitbox), e => e.CurAnimName is "up" or "solidify");
                var switches = BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Gadget.Switches.PillarSwitch).Select(e => TileAt(e.Hitbox)).ToHashSet();
                bool Up(Point t, bool flipped) => pillars.TryGetValue(t, out bool up) && up != flipped;
                bool Open(Point t, bool flipped) => OnScreen(t) && BotNav.Walkable(t) && !switches.Contains(t) && !Up(t, flipped);
                var prev = new Dictionary<(Point, bool), (Point, bool)>();
                var q = new Queue<(Point, bool)>();
                prev[(here, false)] = (here, false);
                q.Enqueue((here, false));
                (Point, bool)? end = null;
                while (q.Count > 0 && end == null)
                {
                    var (c, fl) = q.Dequeue();
                    if (c == goal)
                    {
                        end = (c, fl);
                        break;
                    }
                    var next = o4.Select(o => (new Point(c.X + o.X, c.Y + o.Y), fl)).Where(n => Open(n.Item1, fl)).ToList();
                    if (o4.Any(o => switches.Contains(new Point(c.X + o.X, c.Y + o.Y))) && !Up(c, !fl))
                    {
                        next.Add((c, !fl));
                    }
                    foreach (var n in next)
                    {
                        if (!prev.ContainsKey(n))
                        {
                            prev[n] = (c, fl);
                            q.Enqueue(n);
                        }
                    }
                }
                if (end == null)
                {
                    throw new Exception($"pillar maze: no way from {here} to {goal} with these switches");
                }
                var steps = new List<(Point, bool)>();
                for (var c = end.Value; c != (here, false); c = prev[c])
                {
                    steps.Insert(0, c);
                }
                // Walk up to the first switch hit (or the goal), then hit it and plan again from how the pillars really are.
                var leg = steps.TakeWhile(x => !x.Item2).Select(x => x.Item1).ToList();
                bool hit = leg.Count < steps.Count;
                System.Console.Error.WriteLine($"  pillar maze: {steps.Count(x => x.Item2) / 2 + (hit ? 1 : 0)} switch hit(s) to {goal}; walk " + string.Join(">", leg.Select(t => $"{t.X},{t.Y}")) + (hit ? ", then hit the switch" : ""));
                foreach (var t in leg)
                {
                    var c = BotNav.CentreOf(t);
                    yield return WalkTo(c.X, c.Y, tol: 1.5f, timeout: 120);
                }
                if (!hit)
                {
                    yield break;
                }
                var at = leg.Count > 0 ? leg[leg.Count - 1] : here;
                var sw = o4.Select(o => new Point(at.X + o.X, at.Y + o.Y)).First(switches.Contains);
                int was = GlobalState.PillarSwitchOn;
                yield return StandFaceAccept(at.X, at.Y, FaceChar(new Point(sw.X - at.X, sw.Y - at.Y)));
                yield return Wait(10);
                if (GlobalState.PillarSwitchOn == was)
                {
                    throw new Exception($"pillar maze: hitting the switch at {sw} did nothing");
                }
                yield return Wait(50); // the pillars take ~0.9s to rise all the way
            }
            throw new Exception("pillar maze: gave up after 12 legs");
        }

        /// <summary>
        /// Where to wait for a Gas Guy on a screen with frogs and dash traps: a floor tile of this screen we can walk to, not on its edge (a
        /// doorway would see us off the screen), out of every on-sight dash trap's row and column and 80px or more from every frog, the
        /// nearest such to us.
        /// </summary>
        private static Point? GasWaitSpot()
        {
            var grid = Grid;
            var here = BotNav.TileOf(Pos);
            var traps = TrapRects().Select(r => BotNav.TileOf(new Vector2(r.Center.X - BotNav.HalfW, r.Center.Y - BotNav.HalfH))).ToList();
            var frogs = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Frog").Select(e => new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y)).ToList();
            Point? best = null;
            int bestD = int.MaxValue;
            for (int ty = grid.Y * 10; ty < grid.Y * 10 + 10; ty++)
            {
                for (int tx = grid.X * 10; tx < grid.X * 10 + 10; tx++)
                {
                    var t = new Point(tx, ty);
                    int d = Math.Abs(t.X - here.X) + Math.Abs(t.Y - here.Y);
                    if (d >= bestD || !BotNav.Walkable(t) || tx % 10 == 0 || tx % 10 == 9 || ty % 10 == 0 || ty % 10 == 9 || traps.Any(r => r.X == t.X || r.Y == t.Y)
                        || frogs.Any(f => (f - new Vector2(t.X * 16 + 8, t.Y * 16 + 8)).Length() < 80))
                    {
                        continue;
                    }
                    if (t == here || (BotNav.FindPath(here, t) is { } way && way.All(q => q.X / 10 == grid.X && q.Y / 10 == grid.Y)))
                    {
                        best = t;
                        bestD = d;
                    }
                }
            }
            return best;
        }

        private static readonly System.Reflection.FieldInfo GatePreset =
            typeof(AnodyneSharp.Entities.Gadget.Gate).GetField("_preset", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);

        /// <summary>Whether the screen has buttons and a shut gate that opens on puzzles solved (frame 5-7) rather than kills.</summary>
        private static bool PuzzleGateShut()
        {
            if (!BotRunner.Entities().Any(e => e.exists && e is AnodyneSharp.Entities.Gadget.Button))
            {
                return false;
            }
            return BotRunner.Entities().Any(e => e.exists && e is AnodyneSharp.Entities.Gadget.Gate && e.Solid
                && GatePreset?.GetValue(e) is AnodyneSharp.Entities.EntityPreset p && p.Frame % 10 >= 5);
        }

        /// <summary>
        /// A Silverfish onto a button (with us on the other, if there are two). It sits until it sees us (in front of it, as it turns every
        /// 0.8s, or within 30px), then runs straight away from us until it hits a wall: a sliding block we push from the far side of it. So
        /// search the pushes (where it slides to from each side) for the fewest that leave it on a button where, holding the other button,
        /// we do not move it again (it is out of sight, or a wall stops it at once); stand where the first push wants us, let it run, and
        /// plan again from where it stops (a walk past it can spook it on the way). Then hold our button until all of them count.
        /// </summary>
        protected static IEnumerable<object> SilverfishOnButton(int limit)
        {
            var buttons = BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Gadget.Button).ToList();
            if (buttons.Count == 0 || buttons.Count > 2)
            {
                throw new Exception($"silverfish on button: needs one or two buttons here ({buttons.Count})");
            }
            var grid = Grid;
            Point TileAt(Rectangle r) => new((int)Math.Floor(r.Center.X / 16f), (int)Math.Floor(r.Center.Y / 16f));
            var buttonTiles = buttons.Select(b => TileAt(b.Hitbox)).ToList();
            bool OnScreen(Point t) => t.X >= grid.X * 10 && t.X < grid.X * 10 + 10 && t.Y >= grid.Y * 10 && t.Y < grid.Y * 10 + 10;
            var o4 = new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) };
            bool fight = AutoFight;
            AutoFight = false; // the fish is the tool here
            var tried = new HashSet<(Point, Point)>();
            int resets = 0;
            try
            {
                // A Gas Guy's clouds kill a silverfish they drift over, and they drift at us: kill it first, from as far from the fish as we can.
                var fish0 = BotRunner.Entities().FirstOrDefault(e => e.exists && e is AnodyneSharp.Entities.Enemy.Apartment.Silverfish);
                if (fish0 != null && BotRunner.Entities().Any(e => e.exists && e is AnodyneSharp.Entities.Enemy.Apartment.GasGuy && OnScreen(TileAt(e.Hitbox))))
                {
                    var ft0 = TileAt(fish0.Hitbox);
                    var here0 = BotNav.TileOf(Pos);
                    Point? far = null;
                    int farD = -1;
                    for (int ty = grid.Y * 10; ty < grid.Y * 10 + 10; ty++)
                    {
                        for (int tx = grid.X * 10; tx < grid.X * 10 + 10; tx++)
                        {
                            var t = new Point(tx, ty);
                            int d = Math.Abs(t.X - ft0.X) + Math.Abs(t.Y - ft0.Y);
                            if (d > farD && t.X != ft0.X && t.Y != ft0.Y && BotNav.Walkable(t) && !buttonTiles.Contains(t)
                                && (t == here0 || (BotNav.FindPath(here0, t) is { } way && way.All(OnScreen))))
                            {
                                far = t;
                                farD = d;
                            }
                        }
                    }
                    System.Console.Error.WriteLine($"  silverfish on button: a Gas Guy here; fight it from {far} (the fish is at {ft0})");
                    if (far != null)
                    {
                        var fc = BotNav.CentreOf(far.Value);
                        yield return GoTo(fc.X, fc.Y);
                    }
                    yield return Fight(1800, false, e => e is AnodyneSharp.Entities.Enemy.Apartment.GasGuy);
                }
                for (int round = 0; round < 16; round++)
                {
                    var fish = BotRunner.Entities().FirstOrDefault(e => e.exists && e is AnodyneSharp.Entities.Enemy.Apartment.Silverfish);
                    if (fish == null)
                    {
                        throw new Exception("silverfish on button: no silverfish here");
                    }
                    BotNav.SpookFish = fish;
                    for (int f = 0; f < 120 && fish.velocity != Vector2.Zero; f++)
                    {
                        yield return Wait(1);
                    }
                    // Pillars count as walls while up; a hit on the pillar switch flips them all, which is a move of ours too.
                    var blocksBy = new[] { RatBlocks(false), RatBlocks(true) };
                    // Only walls (and the screen's edge) stop it: a hole or water it runs onto is the end of it.
                    bool Wall(Point t, bool fl) => !OnScreen(t) || BotNav.CurrentMap.GetTileClass(t).Item1 != Touching.NONE || blocksBy[fl ? 1 : 0].Any(b => b.Contains(t.X * 16 + 8, t.Y * 16 + 8));
                    bool Deadly(Point t) => BotNav.CurrentMap.GetTileClass(t).Item2 is AnodyneSharp.MapData.Tiles.CollisionEventType.HOLE or AnodyneSharp.MapData.Tiles.CollisionEventType.CONVEYOR;
                    var pillarTiles = BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Gadget.Switches.SwitchPillar).Select(e => TileAt(e.Hitbox)).ToHashSet();
                    Point? Slide(Point p, Point d, bool fl)
                    {
                        while (!Wall(new Point(p.X + d.X, p.Y + d.Y), fl))
                        {
                            p = new Point(p.X + d.X, p.Y + d.Y);
                            if (Deadly(p))
                            {
                                return null;
                            }
                        }
                        return p;
                    }
                    // Holding button o, does the fish on f stay put? It sees us if lined up or near, and runs away along the longer axis.
                    bool Stays(Point f, Point o, bool fl)
                    {
                        int dx = o.X - f.X, dy = o.Y - f.Y;
                        if (dx != 0 && dy != 0 && Math.Sqrt(dx * dx + dy * dy) * 16 > 34)
                        {
                            return true;
                        }
                        var away = Math.Abs(dx) > Math.Abs(dy) ? new Point(-Math.Sign(dx), 0) : new Point(0, -Math.Sign(dy));
                        return Slide(f, away, fl) is Point to && to == f;
                    }
                    bool Goal(Point f, bool fl) => buttonTiles.Contains(f) && (buttonTiles.Count == 1 || buttonTiles.Where(b => b != f).Any(b => Stays(f, b, fl)));
                    var ft = TileAt(fish.Hitbox);
                    var here = BotNav.TileOf(Pos);
                    if (Goal(ft, false))
                    {
                        var ours = buttonTiles.Where(b => b != ft).FirstOrDefault(b => Stays(ft, b, false));
                        System.Console.Error.WriteLine($"  silverfish on button: it sits on {ft}" + (buttonTiles.Count > 1 ? $", we hold {ours}" : ""));
                        if (buttonTiles.Count > 1)
                        {
                            yield return FishWalk(fish, ours);
                        }
                        for (int f = 0; f < limit && GlobalState.PUZZLES_SOLVED < buttons.Count && TileAt(fish.Hitbox) == ft; f++)
                        {
                            yield return Wait(1);
                        }
                        if (GlobalState.PUZZLES_SOLVED >= buttons.Count)
                        {
                            yield break;
                        }
                        continue; // it ran off after all: plan again from where it stops
                    }
                    // Where we stand to push it from p towards d: on its line behind it (it sees along its line through anything), 3 tiles
                    // off if we can (near enough to be seen as it turns, not so near that a walk up to it spooks it from the side), reachable
                    // from here without leaving the screen.
                    var reach = new Dictionary<Point, bool>();
                    bool CanReach(Point s)
                    {
                        if (!reach.TryGetValue(s, out bool r))
                        {
                            r = s == here || (BotNav.FindPath(here, s) is { } way && way.All(OnScreen));
                            reach[s] = r;
                        }
                        return r;
                    }
                    Point? Stand(Point p, Point d)
                    {
                        foreach (int k in new[] { 3, 4, 2, 5, 6, 7, 8, 9, 1 })
                        {
                            var s = new Point(p.X - d.X * k, p.Y - d.Y * k);
                            if (OnScreen(s) && BotNav.Walkable(s) && !tried.Contains((p, s)) && s != p && CanReach(s))
                            {
                                return s;
                            }
                        }
                        return null;
                    }
                    // Where we hit the pillar switch from (a free tile next to one, reachable).
                    var swTile = BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Gadget.Switches.PillarSwitch).Select(e => TileAt(e.Hitbox)).Cast<Point?>().FirstOrDefault();
                    Point? flipAt = swTile == null ? null : o4.Select(o => new Point(swTile.Value.X + o.X, swTile.Value.Y + o.Y))
                        .Where(t => OnScreen(t) && BotNav.Walkable(t) && (t == here || (BotNav.FindPath(here, t) is { } way && way.All(OnScreen) && !way.Contains(swTile.Value)))).Cast<Point?>().FirstOrDefault();
                    var prev = new Dictionary<(Point, bool), ((Point, bool) from, Point d, Point stand)>();
                    var q = new Queue<(Point, bool)>();
                    q.Enqueue((ft, false));
                    prev[(ft, false)] = ((ft, false), Point.Zero, Point.Zero);
                    (Point, bool)? goal = null;
                    while (q.Count > 0 && goal == null)
                    {
                        var (c, fl) = q.Dequeue();
                        var nexts = new List<((Point, bool) n, Point d, Point stand)>();
                        foreach (var d in o4)
                        {
                            if (Slide(c, d, fl) is Point n && n != c && Stand(c, d) is Point st)
                            {
                                nexts.Add(((n, fl), d, st));
                            }
                        }
                        if (flipAt != null && !pillarTiles.Contains(c))
                        {
                            nexts.Add(((c, !fl), Point.Zero, flipAt.Value)); // (not with the fish on a pillar, which would rise under it)
                        }
                        foreach (var (n, d, st) in nexts)
                        {
                            if (prev.ContainsKey(n))
                            {
                                continue;
                            }
                            prev[n] = ((c, fl), d, st);
                            if (Goal(n.Item1, n.Item2))
                            {
                                goal = n;
                                break;
                            }
                            q.Enqueue(n);
                        }
                    }
                    if (goal == null && resets < 2)
                    {
                        // Stuck (in a corner, say: nothing makes it run out of one): step off the screen and back, which puts it back where
                        // it started (what we killed stays dead).
                        var exit = new List<Point>();
                        for (int i = 0; i < 10; i++)
                        {
                            exit.Add(new Point(grid.X * 10 + i, grid.Y * 10 - 1));
                            exit.Add(new Point(grid.X * 10 + i, grid.Y * 10 + 10));
                            exit.Add(new Point(grid.X * 10 - 1, grid.Y * 10 + i));
                            exit.Add(new Point(grid.X * 10 + 10, grid.Y * 10 + i));
                        }
                        var way = BotNav.FindPath(here, t => exit.Contains(t));
                        if (way != null && way.Count > 0)
                        {
                            var o = way[way.Count - 1];
                            var next = new Point((int)Math.Floor(o.X / 10f), (int)Math.Floor(o.Y / 10f));
                            System.Console.Error.WriteLine($"  silverfish on button: stuck at {ft}; out to screen {next} and back to start it over");
                            resets++;
                            var oc = BotNav.CentreOf(o) + new Vector2(5, 6);
                            if (BotRunner.Entities().FirstOrDefault(e => e.exists && e is AnodyneSharp.Entities.Enemy.Apartment.Silverfish && (e.Center - oc).Length() < 28) is Entity inDoor)
                            {
                                // It has run into the doorway itself (and nothing we do makes it leave an edge it backs into): jump over it.
                                var jd = next.X < grid.X ? KeyFunctions.Left : next.X > grid.X ? KeyFunctions.Right : next.Y < grid.Y ? KeyFunctions.Up : KeyFunctions.Down;
                                var jv = DirVector(jd);
                                var standAt = inDoor.Center - jv * 18f - new Vector2(5, 6);
                                BotRunner.Status = "silverfish on button: jump the fish in the doorway";
                                yield return GoTo(standAt.X, standAt.Y);
                                yield return WalkTo(standAt.X, standAt.Y, tol: 1.5f, timeout: 90);
                                yield return JumpToward(jd, float.NaN);
                            }
                            yield return GoScreenTo(next.X, next.Y);
                            yield return Wait(10);
                            yield return GoScreenTo(grid.X, grid.Y);
                            yield return Wait(20);
                            tried.Clear();
                            continue;
                        }
                    }
                    if (goal == null)
                    {
                        throw new Exception($"silverfish on button: no pushes take it from {ft} onto a button");
                    }
                    var steps = new List<((Point, bool) to, Point d, Point stand)>();
                    for (var c = goal.Value; c != (ft, false); c = prev[c].from)
                    {
                        steps.Insert(0, (c, prev[c].d, prev[c].stand));
                    }
                    var first = steps[0];
                    System.Console.Error.WriteLine($"  silverfish on button: from {ft}, {steps.Count} move(s): " + string.Join(" ", steps.Select(x => x.d == Point.Zero ? "flip the pillars" : $"{x.d.X},{x.d.Y}->{x.to.Item1}")) + $"; first from {first.stand}");
                    if (first.d == Point.Zero)
                    {
                        var sw = swTile.Value;
                        int was = GlobalState.PillarSwitchOn;
                        yield return FishWalk(fish, first.stand);
                        yield return StandFaceAccept(first.stand.X, first.stand.Y, FaceChar(new Point(sw.X - first.stand.X, sw.Y - first.stand.Y)));
                        yield return Wait(10);
                        if (GlobalState.PillarSwitchOn == was)
                        {
                            throw new Exception($"silverfish on button: hitting the pillar switch at {sw} did nothing");
                        }
                        yield return Wait(50);
                        continue;
                    }
                    tried.Add((ft, first.stand));
                    yield return FishWalk(fish, first.stand);
                    bool ran = false;
                    for (int f = 0; f < 300; f++)
                    {
                        if (fish.velocity != Vector2.Zero)
                        {
                            ran = true;
                        }
                        else if (ran)
                        {
                            break;
                        }
                        if (!fish.exists)
                        {
                            break;
                        }
                        yield return Wait(1);
                    }
                }
                throw new Exception("silverfish on button: gave up after 16 pushes");
            }
            finally
            {
                AutoFight = fight;
                BotNav.SpookFish = null;
            }
        }

        /// <summary>
        /// Walk to tile `to` without spooking a sitting silverfish on the way: it sees along its row or column only the way it faces (and it
        /// turns clockwise every 0.8s), so before stepping into its line from the side it would see us on, wait for it to turn just past
        /// that way, which leaves 2.4s before it looks there again, and cross then. (Within 30px it sees us whatever: the route keeps off.)
        /// </summary>
        private static IEnumerable<object> FishWalk(Entity fish, Point to)
        {
            var here = BotNav.TileOf(Pos);
            var path = here == to ? new List<Point>() : BotNav.FindPath(here, to);
            if (path == null)
            {
                throw new Exception($"silverfish on button: no way to {to}");
            }
            Facing Toward(Point ft, Point t) => t.Y == ft.Y ? (t.X < ft.X ? Facing.LEFT : Facing.RIGHT) : (t.Y < ft.Y ? Facing.UP : Facing.DOWN);
            Facing After(Facing f) => f switch { Facing.LEFT => Facing.UP, Facing.UP => Facing.RIGHT, Facing.RIGHT => Facing.DOWN, _ => Facing.LEFT };
            Point FishTile() => BotNav.TileOf(new Vector2(fish.Hitbox.Center.X - BotNav.HalfW, fish.Hitbox.Center.Y - BotNav.HalfH));
            bool InLine(Point t, Point ft) => (t.X == ft.X || t.Y == ft.Y) && t != ft;
            var cur = here;
            for (int i = 0; i < path.Count; i++)
            {
                var t = path[i];
                var ft = FishTile();
                if (fish.exists && fish.velocity == Vector2.Zero && InLine(t, ft) && !InLine(cur, ft))
                {
                    // About to step into its line: wait (on this side) for it to have just turned past facing us.
                    var toward = Toward(ft, t);
                    var last = fish.facing;
                    for (int w = 0; w < 300; w++)
                    {
                        if (fish.facing != last && fish.facing == After(toward))
                        {
                            break;
                        }
                        last = fish.facing;
                        BotRunner.Status = $"silverfish: wait for it to look away from {toward} before crossing its line";
                        BotRunner.NotStuck();
                        yield return K();
                    }
                }
                var c = BotNav.CentreOf(t);
                yield return WalkTo(c.X, c.Y, tol: i == path.Count - 1 ? 1.5f : 3f, timeout: 90, diagonal: false);
                cur = t;
            }
        }

        /// <summary>
        /// An idle dash trap charges the moment the player lines up with it across or along (and only a player in the air is safe):
        /// its row and its column, out to the walls, are where it can be in a moment. Two threats, standing for the whole horizon.
        /// </summary>
        private static IEnumerable<Threat> PredictSightTrap(Entity d)
        {
            var r = d.Hitbox;
            var t0 = BotNav.TileOf(new Vector2(r.Center.X, r.Center.Y) - new Vector2(BotNav.HalfW, BotNav.HalfH));
            int Reach(Point step)
            {
                int n = 0;
                var t = t0;
                while (n < 10)
                {
                    t = new Point(t.X + step.X, t.Y + step.Y);
                    if (!BotNav.Walkable(t) && !BotNav.IsConveyor(t))
                    {
                        break;
                    }
                    n++;
                }
                return n * 16;
            }
            int l = Reach(new Point(-1, 0)), rr = Reach(new Point(1, 0)), u = Reach(new Point(0, -1)), dn = Reach(new Point(0, 1));
            var row = new Rectangle(r.Left - l, r.Top, r.Width + l + rr, r.Height);
            var col = new Rectangle(r.Left, r.Top - u, r.Width, r.Height + u + dn);
            // It can only be as far along its lines as 80px/s has carried it since now (it starts when we line up, which is now at the soonest):
            // a line crossed far from it is safe for a while, which a band standing over the whole line forever never allowed.
            foreach (var band in new[] { row, col })
            {
                var t = new Threat { Name = "OnSightDashTrap", Margin = 1, Source = d, Stationary = false };
                for (int f = 0; f <= Horizon; f++)
                {
                    int reach = (int)(80f * f / 60f);
                    var b = band == row
                        ? new Rectangle(Math.Max(row.Left, r.Left - reach), r.Top, Math.Min(row.Right, r.Right + reach) - Math.Max(row.Left, r.Left - reach), r.Height)
                        : new Rectangle(r.Left, Math.Max(col.Top, r.Top - reach), r.Width, Math.Min(col.Bottom, r.Bottom + reach) - Math.Max(col.Top, r.Top - reach));
                    t.At[f] = b;
                }
                yield return t;
            }
        }

        private static readonly System.Reflection.FieldInfo RotRadiusField =
            typeof(AnodyneSharp.Entities.Enemy.Crowd.RotatorBullet).GetField("radius", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        private static readonly System.Reflection.FieldInfo RotAngleField =
            typeof(AnodyneSharp.Entities.Enemy.Crowd.RotatorBullet).GetField("angle", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        private static readonly System.Reflection.FieldInfo RotParentField =
            typeof(AnodyneSharp.Entities.Enemy.Crowd.RotatorBullet).GetField("parent", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);

        /// <summary>A Crowd Rotator's fireball circles its post (out to 59px at 30px/s, 1.2 rad/s): exactly where it will be. Harmless to a jumper.</summary>
        private static Threat PredictRotatorBullet(Entity d)
        {
            float r = (float)RotRadiusField.GetValue(d), a = (float)RotAngleField.GetValue(d);
            var parent = (Entity)RotParentField.GetValue(d);
            var c = parent.Center;
            var th = new Threat { Name = "RotatorBullet", Margin = 2, Source = d };
            var hb = d.Hitbox;
            var off = new Vector2(hb.X, hb.Y) - d.Position; // hitbox top-left relative to the position the orbit sets
            for (int f = 0; f <= Horizon; f++)
            {
                float rf = Math.Min(59f, r + 30f * f / 60f), af = a + 1.2f * f / 60f;
                var p = c + new Vector2(MathF.Cos(af), MathF.Sin(af)) * rf + off;
                th.At[f] = new Rectangle((int)p.X, (int)p.Y, hb.Width, hb.Height);
            }
            return th;
        }

        private static readonly System.Reflection.FieldInfo RollerInitField =
            typeof(AnodyneSharp.Entities.Enemy.Crowd.FallingSpikeRoller).GetField("_initPos", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        private static readonly System.Reflection.FieldInfo RollerVelField =
            typeof(AnodyneSharp.Entities.Enemy.Crowd.BaseSpikeRoller).GetField("vel", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);

        /// <summary>
        /// A Crowd spike roller. The falling kind waits unseen until we touch it, drops for offset.Y/60 s (harmless), then rolls at 20px/s for
        /// 112px and stays put there (still spiked). The bouncy kind rolls at 40px/s and turns round at walls. Neither hurts us in the air.
        /// </summary>
        private static Threat PredictRoller(Entity e)
        {
            if (e is AnodyneSharp.Entities.Enemy.Crowd.BouncySpikeRoller)
            {
                var pt = PredictPatroller(e);
                pt.Name = "SpikeRoller";
                return pt;
            }
            var th = new Threat { Name = "SpikeRoller", Source = e, Margin = 1 };
            var r = e.Hitbox;
            var roller = (AnodyneSharp.Entities.Enemy.Crowd.BaseSpikeRoller)e;
            var v = e.velocity;
            float fallLeft = roller.CollisionOn ? 0 : e.offset.Y / 60f;
            float left = 112f;
            if (RollerInitField?.GetValue(e) is Vector2 init)
            {
                left = Math.Max(0, 112f - (init - e.Position).Length());
            }
            if (v == Vector2.Zero && !roller.CollisionOn && RollerVelField?.GetValue(e) is Vector2 vel)
            {
                v = vel; // still falling: it rolls when it lands
            }
            th.Stationary = v == Vector2.Zero || left <= 0;
            for (int f = 0; f <= Horizon; f++)
            {
                float t = f / 60f - fallLeft;
                if (t < 0)
                {
                    continue; // still coming down: no harm yet
                }
                float moved = Math.Min(left, v.Length() * t);
                var d = v == Vector2.Zero ? Vector2.Zero : Vector2.Normalize(v) * moved;
                var at = r;
                at.Offset((int)d.X, (int)d.Y);
                th.At[f] = at;
            }
            return th;
        }

        private static readonly System.Reflection.FieldInfo FrogParabolaField =
            typeof(AnodyneSharp.Entities.Enemy.Crowd.Frog.BurstBullet).GetField("parabola", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        private static readonly System.Reflection.FieldInfo ParabolaTField =
            typeof(AnodyneSharp.Entities.Parabola_Thing).GetField("t", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        private static readonly System.Reflection.FieldInfo ParabolaPeriodField =
            typeof(AnodyneSharp.Entities.Parabola_Thing).GetField("period", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);

        /// <summary>
        /// A Crowd frog's bullet is lobbed (height 16) and only hurts while it is low (offset.Y &lt;= 8: the first and last ~15% of its
        /// flight); once it lands it splashes standing still and is harmless. Null once it has landed.
        /// </summary>
        private static Threat PredictFrogBullet(Entity d)
        {
            if (d.velocity == Vector2.Zero)
            {
                return null;
            }
            var para = FrogParabolaField?.GetValue(d);
            if (para == null)
            {
                return null;
            }
            float t0 = (float)ParabolaTField.GetValue(para);
            float period = (float)ParabolaPeriodField.GetValue(para);
            var th = new Threat { Name = "FrogBullet", Margin = 2, Source = d };
            for (int f = 0; f <= Horizon; f++)
            {
                float t = t0 + f / 60f;
                if (t > period + 1 / 60f)
                {
                    break;
                }
                float k = Math.Clamp(t / period, 0f, 1f);
                if (4 * k * (1 - k) * 16 > 8)
                {
                    continue; // high in the air: passes over us
                }
                var r = d.Hitbox;
                r.Offset((int)(d.velocity.X * f / 60f), (int)(d.velocity.Y * f / 60f));
                th.At[f] = r;
            }
            return th;
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

        /// <summary>Would a jump now (steering dir in the air, null for on the spot) come down into something: frames 26..34 of walking that way.</summary>
        private static bool LandsInHit(KeyFunctions? dir, List<Threat> threats)
        {
            var pb = Player.Hitbox;
            var v = dir is KeyFunctions d ? DirVector(d) * WalkPxPerFrame * 0.83f : Vector2.Zero; // (in the air we move at 0.83x)
            for (int f = 26; f <= Math.Min(34, Horizon); f++)
            {
                var p = new Vector2(pb.X, pb.Y) + v * Math.Min(f, 28);
                var mine = new Rectangle((int)p.X, (int)p.Y, pb.Width, pb.Height);
                foreach (var th in threats)
                {
                    if (th.At[f] is Rectangle r && mine.Intersects(r)) // (exact: a margin here turned a clean jump over a dash trap's line into hopping on the spot)
                    {
                        return true;
                    }
                }
            }
            return false;
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
        private static KeyFunctions[] SpaceTimeMove(Vector2 goal, List<Threat> threats, bool useCache = true, bool survive = false)
        {
            var st = new List<StThreat>(threats.Count);
            foreach (var t in threats)
            {
                st.Add(new StThreat { At = t.At, Margin = t.Margin, Stationary = t.Stationary });
            }
            // The plan is good for a few frames: re-search every 3rd frame only (a search costs milliseconds).
            if (useCache && _stCacheFrame >= 0 && BotRunner.Frame - _stCacheFrame < 3 && BotRunner.Frame - _stCacheFrame >= 0 && (goal - _stCacheGoal).Length() < 40f)
            {
                return _stCache < 0 ? null : StActions[_stCache];
            }
            var swPlan = System.Diagnostics.Stopwatch.StartNew();
            var fastWalk0 = FastWalkable();
            // A dodge (survive) plan stays on this screen: backing out through the edge into the previous one hands the fight to a screen
            // transition (and the enemies there); there is nearly always room to sidestep up or down instead.
            var gridNow = Grid;
            Func<Rectangle, bool> fastWalk = survive
                ? r => fastWalk0(r) && r.Left >= gridNow.X * 160 && r.Right <= gridNow.X * 160 + 160 && r.Top >= gridNow.Y * 160 && r.Bottom <= gridNow.Y * 160 + 160
                : fastWalk0;
            int a = SpaceTime.Plan(Player.Hitbox, goal, st, fastWalk, Horizon, maxNodes: 700, survive: survive);
            if (Environment.GetEnvironmentVariable("ANODYNE_ST_TIME") != null) { System.Console.Error.WriteLine($"  plan {swPlan.Elapsed.TotalMilliseconds:0.00}ms nodes {SpaceTime.Nodes} threats {st.Count} -> {a}"); }
            if (a < 0)
            {
                // Doomed with the comfortable margins: settle for exact geometry (a bullet is exactly its rectangle).
                foreach (var t in st)
                {
                    t.Margin = 0;
                }
                a = SpaceTime.Plan(Player.Hitbox, goal, st, fastWalk, Horizon, maxNodes: 400, survive: survive);
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

        /// <summary>CornersWalkable with the tiles around the player looked up once (a search asks about the same few tiles hundreds of thousands of times).</summary>
        private static Func<Rectangle, bool> FastWalkable()
        {
            var pt = BotNav.TileOf(new Vector2(Player.Hitbox.X, Player.Hitbox.Y));
            const int R = 14;
            int ox = pt.X - R, oy = pt.Y - R;
            var grid = new sbyte[(2 * R + 1) * (2 * R + 1)]; // 0 unknown, 1 walkable, -1 not
            bool Tile(int tx, int ty)
            {
                int gx = tx - ox, gy = ty - oy;
                if (gx < 0 || gy < 0 || gx > 2 * R || gy > 2 * R)
                {
                    return BotNav.Walkable(new Point(tx, ty));
                }
                ref sbyte c = ref grid[gy * (2 * R + 1) + gx];
                if (c == 0)
                {
                    c = BotNav.Walkable(new Point(tx, ty)) ? (sbyte)1 : (sbyte)-1;
                }
                return c > 0;
            }
            return r => Tile((int)Math.Floor(r.Left / 16f), (int)Math.Floor(r.Top / 16f)) && Tile((int)Math.Floor((r.Right - 1) / 16f), (int)Math.Floor(r.Top / 16f)) &&
                        Tile((int)Math.Floor(r.Left / 16f), (int)Math.Floor((r.Bottom - 1) / 16f)) && Tile((int)Math.Floor((r.Right - 1) / 16f), (int)Math.Floor((r.Bottom - 1) / 16f));
        }

        /// <summary>Waiting in place is only fine if no danger will reach us: otherwise step to a neighbouring spot where none will.</summary>
        protected static KeyFunctions[] StandSafe(Entity except, bool ignoreAnnoyers = false)
        {
            var threats = PredictThreats(except);
            if (ignoreAnnoyers)
            {
                // they orbit us (their positions are set directly, so they read as standing still): the broom, not dodging, answers them
                threats = threats.Where(t => t.Source == null || t.Source.GetType().Name != "Annoyer").ToList();
            }
            if (threats.Count == 0 || PlanHitFrameStanding(Horizon, KeyFunctions.Right, 0, threats) == int.MaxValue)
            {
                return K();
            }
            // Standing here is about to be hit: search for a way to keep clear over the whole horizon (a first-fit sidestep ran from a patroller
            // straight into the next danger, then back again).
            if (Environment.GetEnvironmentVariable("ANODYNE_NO_SPACETIME") == null)
            {
                var pbs = Player.Hitbox;
                var stay = SpaceTimeMove(new Vector2(pbs.X, pbs.Y), threats, useCache: false, survive: true);
                if (stay != null && stay.Length > 0)
                {
                    return K(stay);
                }
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

        /// <summary>The last SafeMove held still only to let a steam jet go down (a wait of up to a whole cycle, ~5s: not "stuck").</summary>
        protected static bool SteamWait;

        private static readonly Dictionary<Entity, (string anim, int since)> _slasherPhase = new();
        private static KeyFunctions[] _slasherRun;

        /// <summary>
        /// Every Slasher's coming cut, for the space-time planner: it floats 84 frames, winds up 30 (drifting after us at 20px/s) and cuts
        /// where it then is, facing us: a long 36x10 strip beyond its body when we are 36px or more from it, else a wide strip across its
        /// front; the cut lasts ~15 frames. Its body is harmless. Its facing can still turn and it still drifts, so each cut is taken
        /// for both axes towards us, long and wide, grown by the drift still to come. Late in a float the wind-up is near: predicted too.
        /// </summary>
        private static List<Threat> SlasherThreats()
        {
            var list = new List<Threat>();
            int now = (int)BotRunner.Frame;
            var pc = Player.VisualCenter;
            foreach (var sl in BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Slasher").ToList())
            {
                string anim = sl.CurAnimName ?? "";
                string kind = anim.StartsWith("float") ? "float" : anim.StartsWith("warning") ? "warning" : "attack";
                if (!_slasherPhase.TryGetValue(sl, out var ph) || ph.anim != kind)
                {
                    ph = (kind, now);
                    _slasherPhase[sl] = ph;
                }
                foreach (var sub in sl.SubEntities().Where(e => e.exists && e.visible && e.GetType().Name.EndsWith("Slash")))
                {
                    if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && now - ph.since <= 1)
                    {
                        System.Console.Error.WriteLine($"  slashcheck f{now} actual={sub.Hitbox} predicted={SlasherCutRect(sl.Position, Player.Position, Player.VisualCenter)} sl={sl.Position} me={Player.Position} {sl.facing}");
                    }
                    var th = new Threat { Name = "Slasher cut", Source = sl, Margin = 2, Stationary = true };
                    for (int k = 0; k <= 16; k++)
                    {
                        th.At[k] = sub.Hitbox;
                    }
                    list.Add(th);
                }
                int left = kind == "warning" ? 30 - (now - ph.since) : kind == "float" ? 84 - (now - ph.since) + 30 : -1;
                if (left < 0 || left > Horizon)
                {
                    continue;
                }
                left = Math.Max(left, 0);
                var toUs = pc - sl.Center;
                // where it will be: it drifts straight at us (20px/s on each axis) until the cut
                float drift = 20f * left / 60f;
                var p = sl.Position + new Vector2(Math.Sign(toUs.X) * Math.Min(drift, Math.Abs(toUs.X)), Math.Sign(toUs.Y) * Math.Min(drift, Math.Abs(toUs.Y)));
                int grow = 3 + left / 20;
                var facings = new List<Facing> { sl.facing };
                facings.Add(Math.Abs(toUs.X) >= Math.Abs(toUs.Y) ? (toUs.Y < 0 ? Facing.UP : Facing.DOWN) : (toUs.X < 0 ? Facing.LEFT : Facing.RIGHT));
                facings.Add(Math.Abs(toUs.X) >= Math.Abs(toUs.Y) ? (toUs.X < 0 ? Facing.LEFT : Facing.RIGHT) : (toUs.Y < 0 ? Facing.UP : Facing.DOWN));
                var rects = new List<Rectangle>();
                foreach (var f in facings.Distinct())
                {
                    switch (f)
                    {
                        case Facing.UP: rects.Add(new Rectangle((int)p.X + 3, (int)p.Y - 36, 10, 36)); rects.Add(new Rectangle((int)p.X - 10, (int)p.Y - 4, 36, 10)); break;
                        case Facing.DOWN: rects.Add(new Rectangle((int)p.X + 3, (int)p.Y + 16, 10, 36)); rects.Add(new Rectangle((int)p.X - 10, (int)p.Y + 10, 36, 10)); break;
                        case Facing.LEFT: rects.Add(new Rectangle((int)p.X - 36, (int)p.Y + 3, 36, 10)); rects.Add(new Rectangle((int)p.X - 5, (int)p.Y - 10, 10, 36)); break;
                        default: rects.Add(new Rectangle((int)p.X + 16, (int)p.Y + 3, 36, 10)); rects.Add(new Rectangle((int)p.X + 11, (int)p.Y - 10, 10, 36)); break;
                    }
                }
                foreach (var r0 in rects)
                {
                    var r = r0;
                    r.Inflate(grow, grow);
                    var th = new Threat { Name = "Slasher coming cut", Source = sl, Margin = 1, Stationary = true };
                    for (int k = Math.Max(0, left - 3); k <= Math.Min(Horizon, left + 16); k++)
                    {
                        th.At[k] = r;
                    }
                    list.Add(th);
                }
            }
            return list;
        }

        /// <summary>Frames until this Slasher cuts (-1: it is cutting or resting): 84 of float, then 30 of wind-up.</summary>
        private static int SlasherCutIn(Entity sl)
        {
            if (!_slasherPhase.TryGetValue(sl, out var ph))
            {
                return -1;
            }
            int now = (int)BotRunner.Frame;
            return ph.anim == "warning" ? Math.Max(0, 30 - (now - ph.since)) : ph.anim == "float" ? 84 - (now - ph.since) + 30 : -1;
        }

        /// <summary>The cut a Slasher at `sp` makes at a player at `pp` (Position) / `pvc` (visual centre): it faces us on the dominant axis;
        /// a long strip beyond it when we are 36px or more from its centre, else a wide one across its front.</summary>
        private static Rectangle SlasherCutRect(Vector2 sp, Vector2 pp, Vector2 pvc)
        {
            var d = pp - sp;
            var f = Math.Abs(d.X) > Math.Abs(d.Y) ? (d.X > 0 ? Facing.RIGHT : Facing.LEFT) : (d.Y > 0 ? Facing.DOWN : Facing.UP);
            bool wide = (sp + new Vector2(8, 8) - pvc).LengthSquared() < 36 * 36;
            int x = (int)sp.X, y = (int)sp.Y;
            return (f, wide) switch
            {
                (Facing.UP, false) => new Rectangle(x + 3, y - 36, 10, 36),
                (Facing.DOWN, false) => new Rectangle(x + 3, y + 16, 10, 36),
                (Facing.LEFT, false) => new Rectangle(x - 36, y + 3, 36, 10),
                (Facing.RIGHT, false) => new Rectangle(x + 16, y + 3, 36, 10),
                (Facing.UP, true) => new Rectangle(x - 10, y - 4, 36, 10),
                (Facing.DOWN, true) => new Rectangle(x - 10, y + 10, 36, 10),
                (Facing.LEFT, true) => new Rectangle(x - 5, y - 10, 10, 36),
                _ => new Rectangle(x + 11, y - 10, 10, 36),
            };
        }

        /// <summary>How long into a Slasher's 1.4s float (84 frames, then a 30-frame wind-up) the bot keeps striking before it gets clear of the coming cut: 70 lands the third hit (0.5s apart) in one float.</summary>
        private static readonly int SlasherStrikeFrames = int.TryParse(Environment.GetEnvironmentVariable("ANODYNE_BOT_SLASHER_STRIKE"), out int ssf) ? ssf : 70;

        private static readonly System.Reflection.FieldInfo SinkingField =
            typeof(AnodyneSharp.Entities.Player).GetField("IS_SINKING", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);

        /// <summary>Wading in water (no raft): the game ignores the jump key then, whatever tile our position rounds to.</summary>
        private static bool Sinking => SinkingField?.GetValue(Player) is bool b && b;

        protected static KeyFunctions[] SafeMove(KeyFunctions wanted, Entity except = null, float dist = 40f, bool stopForBlockers = false, List<Vector2> look = null)
        {
            Dodged = false;
            BlockedBy = null;
            SteamWait = false;
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
            if (dangers.All(IsSteamBeam))
            {
                // only steam jets (burning or about to): hold back from stepping in, but never stand still inside one
                SteamWait = WouldCollide(wanted, dangers, 8);
                return SteamWait ? K() : K(wanted);
            }
            var threats = PredictThreats(except);
            if (GlobalState.inventory.CanJump && !(Player.raft == null && BotNav.IsConveyor(BotNav.TileOf(Pos))) && !Sinking && Environment.GetEnvironmentVariable("ANODYNE_BOT_NO_JUMPDODGE") == null)
            {
                // (Wading, sinking into water, there is no jumping; puddles are fine.) With the boots a jump is ~28 frames in the air, and everything that hurts by touch or bullet checks "not in the air": jump
                // through the volley / the dash instead of waiting for a gap that may never come. Steer in the air towards where we were going.
                if (Player.state == AnodyneSharp.Entities.PlayerState.AIR)
                {
                    Dodged = true;
                    return _jumpOnSpot ? K() : K(wanted);
                }
                _jumpOnSpot = false;
                var moving = threats.Where(t => !t.Stationary && !t.HurtsInAir).ToList();
                var airHurt = threats.Where(t => t.HurtsInAir).ToList();
                if (moving.Count > 0 && (airHurt.Count == 0 || (PlanHitFrame(wanted, 0, wanted, 30, airHurt) == int.MaxValue && PlanHitFrameStanding(30, wanted, 0, airHurt) == int.MaxValue)))
                {
                    int hitStill = PlanHitFrameStanding(0, KeyFunctions.Right, 0, moving);
                    int hitWalk = PlanHitFrame(wanted, 0, wanted, 24, moving);
                    int first = Math.Min(hitStill == int.MaxValue ? 999 : hitStill, hitWalk == int.MaxValue ? 999 : hitWalk);
                    // (a jump is ~28 frames: one that comes down into the next bullet of a shooter's lane is no dodge, so look at the landing too;
                    // jumping on the spot may land clear where the jump on the way does not)
                    bool jumpOn = first <= 22, onSpot = false;
                    if (jumpOn && hitStill == int.MaxValue && hitWalk > 1 && LandsInHit(wanted, moving))
                    {
                        // Standing is safe and the hit is a few steps off, but a jump from here would come down in it (a dash trap's
                        // whole line, say): walk on and take off later, so the jump carries us over it.
                        return K(wanted);
                    }
                    if (jumpOn && LandsInHit(wanted, moving))
                    {
                        onSpot = !LandsInHit(null, moving);
                        jumpOn = onSpot;
                    }
                    if (jumpOn)
                    {
                        Dodged = true;
                        BotRunner.Status += onSpot ? " [jump the hit on the spot]" : " [jump the hit]";
                        _jumpOnSpot = onSpot;
                        if (_jumpPressFrame == BotRunner.Frame - 1)
                        {
                            // pressed last frame and still on the ground: the game only jumps on a fresh press, so let go for a frame
                            return onSpot ? K() : K(wanted);
                        }
                        _jumpPressFrame = BotRunner.Frame;
                        return onSpot ? K(KeyFunctions.Cancel) : K(KeyFunctions.Cancel, wanted);
                    }
                }
            }
            if (look != null && look.Count > 0 && Environment.GetEnvironmentVariable("ANODYNE_NO_SPACETIME") == null && PathPlanHit(look, threats) != int.MaxValue
                && PathPlanHit(look, threats.Where(t => !t.Stationary).ToList()) != int.MaxValue) // a stationary blocker never moves away: waiting for it is pointless, the blocker logic below deals with it
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
                if (stand && hit <= 1)
                {
                    hit = -1; // already being hit where we stand (a Gas Guy drifting through walls onto us): any step beats standing in it
                }
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
            var one = solid == null ? new List<Entity>() : new List<Entity> { solid };
            if (Environment.GetEnvironmentVariable("ANODYNE_NO_STEPKEY") == null && wantH && wantV && WouldCollide(h, one, 6) && !WouldCollide(v, one, 6))
            {
                return v;
            }
            // A wall corner in the way (the 10px hitbox overhangs a tile edge by a pixel or two): slide along it with the other axis first.
            if (wantH && !WalkableAhead(h, 3) && Math.Abs(py) > 0.5f && WalkableAhead(v, 2))
            {
                return v;
            }
            if (!wantH && wantV && !WalkableAhead(v, 3) && Math.Abs(px) > 0.5f && WalkableAhead(h, 2))
            {
                return h;
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
            if (BotNav.Walkable(new Point(tx, ty - 1)) && BotRunner.Entities().Any(e => e.exists && e.GetType().Name is "Frog" or "Burst_Plant" && (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - new Vector2(c.X + 5, c.Y + 6)).Length() < 90))
            {
                // A shooter is about: its bullets are aimed at the middle of us. Stand at the top of the tile (still the same tile): the volley
                // lands a few pixels short and mostly misses, which is enough while we do slow work (setting dust down) here. (Not under a
                // wall: the top of the tile pokes into it.)
                c.Y -= 5;
            }
            BotRunner.Status += $" [stand {tx},{ty}]";
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
                BotNav.HardAvoid = null; // an earlier emitter that gave up (`continue`) must not leave its beam lanes walled off
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
                BotNav.HardAvoid = new HashSet<Point>(lanes0); BotNav.HardAvoid.UnionWith(BotNav.NearEnemies(14, null, stationaryOnly: true)); // keep clear of stationary shields too; the walks to the dust and to the plug spot must not cross a live beam
                var stand = SafeStand(target, lanes0, et);
                if (stand.HasValue)
                {
                    BotRunner.Status = $"plugging laser at {et.X},{et.Y}: fetch dust, stand {stand.Value.X},{stand.Value.Y}";
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
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            var near = BotNav.NearEnemies(14, null, stationaryOnly: true);
                            System.Console.Error.WriteLine($"  laser {et.X},{et.Y}: no safe tile to plug {target.X},{target.Y} from; me {BotNav.TileOf(Pos)} lanes {string.Join(" ", lanes0.Select(l => $"{l.X},{l.Y}"))} walk {string.Join(" ", new[] { new Point(0, 1), new Point(0, -1), new Point(-1, 0) }.Select(o => $"{o.X},{o.Y}:{BotNav.Walkable(new Point(target.X + o.X, target.Y + o.Y))}/{near.Contains(new Point(target.X + o.X, target.Y + o.Y))}"))}");
                        }
                        continue;
                    }
                    BotRunner.Status = $"plugging laser at {et.X},{et.Y}: two-step, fetch dust, stand {stand1.Value.X},{stand1.Value.Y}";
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
                BotNav.HardAvoid = null;
                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    System.Console.Error.WriteLine($"  laser {et.X},{et.Y} plugged: {plugged()}");
                }
            }
            BotNav.HardAvoid = null;
        }


        private static bool _pickedDust;

        /// <summary>A tile next to `tile` (not the emitter's) that is walkable, off every open beam and reachable without crossing one.</summary>
        private static Point? SafeStand(Point tile, HashSet<Point> lanes, Point emitter)
        {
            foreach (var o in new[] { new Point(0, 1), new Point(0, -1), new Point(1, 0), new Point(-1, 0) })
            {
                var t = new Point(tile.X + o.X, tile.Y + o.Y);
                if (t == emitter || !BotNav.Walkable(t) || lanes.Contains(t) || BotNav.NearEnemies(14, null, stationaryOnly: true).Contains(t))
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
                if (BotNav.Walkable(t) && !lanes.Contains(t) && !BotNav.NearEnemies(14, null, stationaryOnly: true).Contains(t))
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
                if (onPath.Contains(t))
                {
                    result.Add(em); // the route squeezes past the emitter itself: its muzzle bullets are on the path
                    continue;
                }
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
            var start = BotNav.TileOf(Pos);
            if (!BotNav.Walkable(start) && BotRunner.Entities().Any(e => e.exists && e is AnodyneSharp.Entities.Gadget.Propelled && e.Hitbox.Intersects(Player.Hitbox)))
            {
                // Riding a platform over a hole: no route starts there, so step off onto the floor beside it first (towards the goal if we can).
                var off = new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) }.Select(o => new Point(start.X + o.X, start.Y + o.Y))
                    .Where(BotNav.Walkable).OrderBy(t => Math.Abs(t.X - goal.X) + Math.Abs(t.Y - goal.Y)).ToList();
                if (off.Count > 0)
                {
                    var oc = BotNav.CentreOf(off[0]);
                    yield return WalkTo(oc.X, oc.Y, tol: 1.5f, timeout: 120);
                }
            }
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
                    DumpTiles(here, goal);
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

        /// <summary>
        /// Token i: the Health Cicada of this map (a HealthCicadaSentinel preset that has not been used): walk to it at once (it flies off
        /// if we stray 80px once it has come down), then sit through its cutscene (it heals, gnaws a new heart into the bar).
        /// </summary>
        /// <summary>
        /// A frog in a corridor fires a volley the moment we come within 64px and again each time its bullets land while we stay in, so a
        /// plan clear of them all (the fight's) seldom comes where two stand together (the Crowd's (4,6), behind a one-tile pit) or one fills a
        /// strip (5,5). Killing it is what opens the way: wait out of its reach, run in along dir to (x,y) without stopping (the volley comes
        /// down where we were when it fired, behind us), jump the pit if there is one, swing twice a second apart (it flickers in between)
        /// and back off if it still lives. Costs a heart or so: the sections that use it have a budget.
        /// </summary>
        /// <summary>
        /// Black and White Town: the man in front of the house only lets you in once ten townsfolk are dead (event SuburbKilled). They
        /// are not enemies (one broom hit, never hurt you) and wander at 20px/s, turning every second or two: walk up to the nearest
        /// one still walking on this screen and swing, keeping clear of the killers on the way (SafeMove counts them as dangers).
        /// </summary>
        /// <summary>
        /// Brooms every Silverfish on this screen. A silverfish runs straight away from us (50px/s, until it hits a wall) once we are within 30px
        /// or in its line of sight, so chasing one is hopeless: wait while it runs, then walk up to it from the side opposite a wall (it bolts into
        /// that wall and stays put) and swing. One in the open is first scared from the side that sends it into the nearest wall.
        /// </summary>
        protected static IEnumerable<object> KillSilverfish(int frames)
        {
            bool Wall(Point t) => BotNav.IsBeamWall(t) || BotRunner.Entities().Any(g => g.exists && g.Solid && g.GetType().Name == "Gate" && BotNav.TileOf(g.Position) == t);
            var dirs = new[] { new Point(0, 1), new Point(0, -1), new Point(1, 0), new Point(-1, 0) };
            var gasMem = new PolicyMem();
            var last = new Dictionary<Entity, Vector2>();
            for (int i = 0; i < frames; i++)
            {
                var me = new Vector2(Player.Hitbox.Center.X, Player.Hitbox.Center.Y);
                var moving = new HashSet<Entity>();
                foreach (var e in BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Silverfish"))
                {
                    // moving = it actually moved since the last frame (one pushing into a thin wall keeps its velocity but never gets anywhere)
                    if (e.velocity != Vector2.Zero && (!last.TryGetValue(e, out var lp) || (lp - e.Position).Length() > 0.1f))
                    {
                        moving.Add(e);
                    }
                    last[e] = e.Position;
                }
                var fish = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Silverfish" && new Point((int)(e.Center.X / 160), (int)(e.Center.Y / 160)) == Grid)
                    .OrderBy(e => (e.Center - me).Length()).FirstOrDefault();
                if (fish == null)
                {
                    yield break;
                }
                BotRunner.Status = $"silverfish at {fish.Position}";
                // one running at us (a first run goes the way it did not face, wherever we are): step out of its lane
                var bolt = moving.FirstOrDefault(e => e.velocity.X != 0 ? Math.Abs(e.Center.Y - me.Y) < 15 && (me.X - e.Center.X) * e.velocity.X > 0 && Math.Abs(me.X - e.Center.X) < 64
                                          : Math.Abs(e.Center.X - me.X) < 14 && (me.Y - e.Center.Y) * e.velocity.Y > 0 && Math.Abs(me.Y - e.Center.Y) < 64);
                if (bolt != null)
                {
                    var side = bolt.velocity.X != 0 ? (me.Y < bolt.Center.Y ? new[] { KeyFunctions.Up, KeyFunctions.Down } : new[] { KeyFunctions.Down, KeyFunctions.Up })
                                                    : (me.X < bolt.Center.X ? new[] { KeyFunctions.Left, KeyFunctions.Right } : new[] { KeyFunctions.Right, KeyFunctions.Left });
                    var before = Pos;
                    yield return K(side[0]);
                    if ((Pos - before).Length() < 0.3f)
                    {
                        yield return K(side[1]);
                    }
                    continue;
                }
                if (BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "GasGuy" && new Point((int)(e.Center.X / 160), (int)(e.Center.Y / 160)) == Grid))
                {
                    // the Gas Guy homes in on us through everything: broom it first (its gas kills silverfish anyway)
                    var gas = BotRunner.Entities().First(e => e.exists && e.GetType().Name == "GasGuy" && new Point((int)(e.Center.X / 160), (int)(e.Center.Y / 160)) == Grid);
                    BotRunner.Status = $"gas guy at {gas.Position}";
                    yield return PolicyFrame(FightPolicies.Slime(PolicyCtx(gas), gasMem) ?? new PKey[0], gas); // it comes to us: keep it at broom's length and swing
                    continue;
                }
                if (moving.Contains(fish))
                {
                    yield return K(); // running: let it reach its wall
                    continue;
                }
                var ft = BotNav.TileOf(fish.Center - new Vector2(BotNav.HalfW, BotNav.HalfH));
                // the side to come from: one we can stand on, with a wall right behind the fish (else the side whose run is shortest)
                Point best = Point.Zero;
                int bestRun = int.MaxValue;
                foreach (var s in dirs)
                {
                    var stand = new Point(ft.X + s.X, ft.Y + s.Y);
                    if (!BotNav.Walkable(stand) || BotNav.FindPath(BotNav.TileOf(Pos), stand) == null)
                    {
                        continue;
                    }
                    int run = 0;
                    while (run < 10 && !Wall(new Point(ft.X - s.X * (run + 1), ft.Y - s.Y * (run + 1))))
                    {
                        run++;
                    }
                    if (run < bestRun)
                    {
                        bestRun = run;
                        best = s;
                    }
                }
                if (bestRun == int.MaxValue)
                {
                    System.Console.Error.WriteLine($"silverfish at {fish.Position}: no side to reach it from");
                    yield break;
                }
                // stand 21px out from its centre on that side, level with it
                var goal = fish.Center + new Vector2(best.X, best.Y) * 21f;
                var d = goal - me;
                float along = best.X != 0 ? d.X : d.Y, across = best.X != 0 ? d.Y : d.X;
                if (Math.Abs(along) > 24 || Math.Abs(across) > 24)
                {
                    // far: walk the tile route to the tile two out on that side (outside its 30px fright radius as long as it can)
                    var far = new Point(ft.X + best.X * 2, ft.Y + best.Y * 2);
                    var target = BotNav.Walkable(far) ? far : new Point(ft.X + best.X, ft.Y + best.Y);
                    var route = BotNav.FindPath(BotNav.TileOf(Pos), target);
                    if (route != null && route.Count > 0)
                    {
                        var c = BotNav.CentreOf(route[0]);
                        float rx = c.X - Pos.X, ry = c.Y - Pos.Y;
                        yield return K(Math.Abs(rx) > 1.5f ? (rx < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (ry < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                        continue;
                    }
                }
                if (Math.Abs(across) > 2)
                {
                    yield return K(best.X != 0 ? (across < 0 ? KeyFunctions.Up : KeyFunctions.Down) : (across < 0 ? KeyFunctions.Left : KeyFunctions.Right));
                    continue;
                }
                var face = best.X > 0 ? KeyFunctions.Left : best.X < 0 ? KeyFunctions.Right : best.Y > 0 ? KeyFunctions.Up : KeyFunctions.Down;
                if (Math.Abs(along) > 2)
                {
                    yield return K(best.X != 0 ? (along < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (along < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                    continue;
                }
                yield return K(face);
                yield return Tap(KeyFunctions.Accept);
                yield return Wait(16);
            }
            throw new TimeoutException($"bot: silverfish still alive after {frames} frames on {Map} {Grid}");
        }

        protected static IEnumerable<object> KillTownsfolk(int total)
        {
            var skip = new HashSet<Entity>(); // walled off from us (in a yard, behind the fence)
            Entity chased = null;
            int chaseFrames = 0;
            for (int guard = 0; guard < 3000 && GlobalState.events.GetEvent("SuburbKilled") < total; guard++)
            {
                var me = new Vector2(Player.Hitbox.Center.X, Player.Hitbox.Center.Y);
                var killers = BotRunner.Entities().Where(e => e.exists && BotNav.IsSuburbKiller(e)).Select(e => new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y)).ToList();
                var walker = BotRunner.Entities().Where(e => e.exists && !skip.Contains(e) && e.GetType().Name == "SuburbWalker" && e.velocity != Vector2.Zero && new Point((int)(e.Position.X / 160), (int)(e.Position.Y / 160)) == Grid
                        && killers.All(k => (k - new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y)).Length() > 48f)) // (one by a killer is left alone: the killer wakes as we swing)
                    .OrderBy(e => (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - me).Length()).FirstOrDefault();
                if (walker == null)
                {
                    System.Console.Error.WriteLine($"townsfolk dead: {GlobalState.events.GetEvent("SuburbKilled")} (none left to reach on {Grid}, {skip.Count} walled off)");
                    yield break;
                }
                BotRunner.Status = $"townsfolk {GlobalState.events.GetEvent("SuburbKilled")}/{total}";
                chaseFrames = walker == chased ? chaseFrames + 1 : 0;
                chased = walker;
                if (chaseFrames > 600)
                {
                    skip.Add(walker);
                    continue;
                }
                if (killers.Select(k => me - k).Where(v => v.Length() < 44f).OrderBy(v => v.Length()).Cast<Vector2?>().FirstOrDefault() is Vector2 away)
                {
                    // a killer close by (awake ones creep after us at 30px/s): back off from it first, whichever way is open
                    var keys = (Math.Abs(away.X) > Math.Abs(away.Y)
                        ? new[] { away.X < 0 ? KeyFunctions.Left : KeyFunctions.Right, away.Y < 0 ? KeyFunctions.Up : KeyFunctions.Down }
                        : new[] { away.Y < 0 ? KeyFunctions.Up : KeyFunctions.Down, away.X < 0 ? KeyFunctions.Left : KeyFunctions.Right });
                    var before = Pos;
                    yield return K(keys[0]);
                    if ((Pos - before).Length() < 0.5f)
                    {
                        yield return K(keys[1]);
                    }
                    chaseFrames += 2;
                    continue;
                }
                var d = new Vector2(walker.Hitbox.Center.X, walker.Hitbox.Center.Y) - me;
                if (d.Length() > 36f)
                {
                    // far, maybe round a wall: walk the tile route to beside it (it moves 20px/s, so a stale goal is still close)
                    var wt = BotNav.TileOf(new Vector2(walker.Hitbox.Center.X - 5, walker.Hitbox.Center.Y - 6));
                    var route = BotNav.FindPath(BotNav.TileOf(Pos), t => Math.Abs(t.X - wt.X) + Math.Abs(t.Y - wt.Y) <= 1);
                    if (route == null)
                    {
                        skip.Add(walker);
                        continue;
                    }
                    if (route.Count > 0)
                    {
                        var c = BotNav.CentreOf(route[0]);
                        float rx = c.X - Pos.X, ry = c.Y - Pos.Y;
                        bool horiz = Math.Abs(rx) > 1.5f;
                        yield return SafeMove(horiz ? (rx < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (ry < 0 ? KeyFunctions.Up : KeyFunctions.Down), null, Math.Max(1f, horiz ? Math.Abs(rx) : Math.Abs(ry)));
                        continue;
                    }
                }
                var face = Math.Abs(d.X) > Math.Abs(d.Y) ? (d.X < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (d.Y < 0 ? KeyFunctions.Up : KeyFunctions.Down);
                float across = Math.Abs(d.X) > Math.Abs(d.Y) ? Math.Abs(d.Y) : Math.Abs(d.X);
                if (d.Length() < 20f && across < 8f)
                {
                    yield return K(face);
                    yield return Tap(KeyFunctions.Accept);
                    yield return Wait(16);
                    continue;
                }
                // line up across first when close, else close the distance along the long axis
                var step = d.Length() < 28f && across >= 8f
                    ? (Math.Abs(d.X) > Math.Abs(d.Y) ? (d.Y < 0 ? KeyFunctions.Up : KeyFunctions.Down) : (d.X < 0 ? KeyFunctions.Left : KeyFunctions.Right))
                    : face;
                yield return SafeMove(step, null, 6f);
            }
        }

        /// <summary>Runs dir until the take-off coordinate (if any) and jumps holding dir all the way until landing (e.g. over a pit), then
        /// stands still a moment. A hole only swallows the player after a few frames on it, so the take-off can be a little over its edge.</summary>
        protected static IEnumerable<object> JumpToward(KeyFunctions dir, float at)
        {
            bool horiz = dir == KeyFunctions.Left || dir == KeyFunctions.Right;
            bool plus = dir == KeyFunctions.Right || dir == KeyFunctions.Down;
            for (int i = 0; i < 120 && !float.IsNaN(at); i++)
            {
                float c = horiz ? Pos.X : Pos.Y;
                if (plus ? c >= at : c <= at)
                {
                    break;
                }
                yield return K(dir);
            }
            yield return K(dir);
            yield return K(KeyFunctions.Cancel, dir);
            for (int i = 0; i < 60 && (i < 3 || Player.state == AnodyneSharp.Entities.PlayerState.AIR); i++)
            {
                yield return K(dir);
            }
            yield return Wait(10);
        }

        protected static IEnumerable<object> FrogRush(float x, float y, KeyFunctions dir, bool jump)
        {
            var d = DirVector(dir);
            var back = dir switch { KeyFunctions.Right => KeyFunctions.Left, KeyFunctions.Left => KeyFunctions.Right, KeyFunctions.Up => KeyFunctions.Down, _ => KeyFunctions.Up };
            var at = new Vector2(x, y);
            for (int attempt = 0; attempt < 4; attempt++)
            {
                var frog = BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Enemy.Crowd.Frog && Vector2.Dot(new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - at, d) > 0
                        && (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - at).Length() < 64)
                    .OrderBy(e => (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - (at + d * 30)).LengthSquared()).FirstOrDefault();
                if (frog == null)
                {
                    yield break;
                }
                BotRunner.Status = "frog rush: wait out of reach";
                // (72px out from its middle along the run: its reach is 64px)
                float along = Vector2.Dot(new Vector2(frog.Hitbox.Center.X, frog.Hitbox.Center.Y) - (at + new Vector2(5, 6)), d);
                var wait = at - d * Math.Max(30f, 72f - along);
                yield return GoGlobal(wait.X, wait.Y);
                for (int i = 0; i < 40 && (Pos - wait).Length() > 1.5f; i++)
                {
                    var off = wait - Pos;
                    yield return K(Math.Abs(off.X) > Math.Abs(off.Y) ? (off.X > 0 ? KeyFunctions.Right : KeyFunctions.Left) : (off.Y > 0 ? KeyFunctions.Down : KeyFunctions.Up));
                }
                for (int i = 0; i < 240 && BotRunner.Entities().Any(e => e.exists && e is AnodyneSharp.Entities.Enemy.Crowd.Frog.BurstBullet); i++)
                {
                    BotRunner.Status = "frog rush: wait for the air to clear";
                    yield return K();
                }
                BotRunner.Status = "frog rush: run in";
                for (int i = 0; i < 120 && Vector2.Dot(at - Pos, d) > 0.5f; i++)
                {
                    yield return K(dir);
                }
                if (jump)
                {
                    BotRunner.Status = "frog rush: jump";
                    yield return K(KeyFunctions.Cancel, dir);
                    for (int i = 0; i < 40 && (i < 3 || Player.state == AnodyneSharp.Entities.PlayerState.AIR); i++)
                    {
                        yield return K(dir);
                    }
                }
                for (int swing = 0; swing < 3 && frog.exists; swing++)
                {
                    BotRunner.Status = $"frog rush: swing {swing + 1}";
                    yield return K(dir);
                    yield return K(KeyFunctions.Accept);
                    yield return Wait(swing < 2 ? 62 : 20);
                }
                if (!frog.exists || frog.CurAnimName == "die")
                {
                    yield return Wait(30);
                    yield break;
                }
                BotRunner.Status = "frog rush: back off";
                if (jump)
                {
                    yield return K(KeyFunctions.Cancel, back);
                }
                for (int i = 0; i < 40 && (i < 3 || Player.state == AnodyneSharp.Entities.PlayerState.AIR || (!jump && i < 30)); i++)
                {
                    yield return K(back);
                }
                yield return Wait(120);
            }
        }

        protected static IEnumerable<object> CollectCicada()
        {
            var pr = AnodyneSharp.Entities.EntityManager.GetMapEntities(GlobalState.CURRENT_MAP_NAME)
                .FirstOrDefault(p => p.Type != null && p.Type.Name == "HealthCicadaSentinel" && p.Alive);
            if (pr == null)
            {
                yield break;
            }
            int maxBefore = GlobalState.MAX_HEALTH;
            BotRunner.Status = $"to the health cicada at {pr.Position.X:0},{pr.Position.Y:0}";
            yield return GoGlobal(pr.Position.X, pr.Position.Y);
            bool sawBusy = false;
            for (int i = 0; i < 1800; i++)
            {
                sawBusy |= Talking;
                if (GlobalState.MAX_HEALTH > maxBefore && !Talking && i > 60)
                {
                    break;
                }
                if (!sawBusy && i > 240)
                {
                    break; // it never came: nothing more to wait for
                }
                yield return Talking ? AdvanceKeys(i) : K();
            }
            yield return Wait(30);
        }

        /// <summary>Token o&lt;MAP&gt;: the door of this map that is linked to a door on MAP (e.g. oREDCAVE): walk there and through it.</summary>
        protected static IEnumerable<object> EnterDoorTo(string spec)
        {
            // "MAP" or "MAP@x,y": the door nearest that world position when a map has several doors to the same place
            string map = spec;
            Vector2? near = null;
            if (spec.Contains('@'))
            {
                map = spec.Substring(0, spec.IndexOf('@'));
                var xy = spec.Substring(spec.IndexOf('@') + 1).Split(',');
                near = new Vector2(float.Parse(xy[0]), float.Parse(xy[1]));
            }
            string here = GlobalState.CURRENT_MAP_NAME;
            var door = AnodyneSharp.Entities.EntityManager.GetMapEntities(here)
                .Where(pr => pr.Type != null && pr.Type.Name.EndsWith("Door") && pr.Type.Name != "NexusPad" && pr.Type.Name != "NexusDoor")
                .Where(pr => string.Equals(AnodyneSharp.Entities.EntityManager.GetLinkedDoor(pr)?.Map, map, StringComparison.OrdinalIgnoreCase))
                .OrderBy(pr => near.HasValue ? (new Vector2(pr.Position.X, pr.Position.Y) - near.Value).Length() : 0f)
                .FirstOrDefault();
            if (door == null)
            {
                throw new InvalidOperationException($"bot: no door from {here} to {map}");
            }
            BotRunner.Status = $"to the door to {map} at {door.Position.X:0},{door.Position.Y:0}";
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
            {
                System.Console.Error.WriteLine($"  doors of {here}: " + string.Join(" ", AnodyneSharp.Entities.EntityManager.GetMapEntities(here).Where(pr => pr.Type != null && pr.Type.Name.EndsWith("Door")).Select(pr => $"{pr.Type.Name}@{pr.Position.X:0},{pr.Position.Y:0}->{AnodyneSharp.Entities.EntityManager.GetLinkedDoor(pr)?.Map}")) + $"; taking {door.Position}");
            }
            // UpDoor / DownDoor / LeftDoor / RightDoor: the way we walk into it
            var into = door.Type.Name.StartsWith("Down") ? KeyFunctions.Down : door.Type.Name.StartsWith("Left") ? KeyFunctions.Left : door.Type.Name.StartsWith("Right") ? KeyFunctions.Right : KeyFunctions.Up;
            // the door tile itself is not floor: go to the tile in front of it (the side we come from)
            var back = into == KeyFunctions.Up ? new Vector2(0, 22) : into == KeyFunctions.Down ? new Vector2(0, -22) : into == KeyFunctions.Left ? new Vector2(22, 0) : new Vector2(-22, 0);
            yield return GoGlobal(door.Position.X + back.X, door.Position.Y + back.Y);
            // line up with the door's own (small) hitbox before walking in
            var de = BotRunner.Entities().FirstOrDefault(e => e.exists && e.GetType().Name == door.Type.Name && (e.Position - door.Position).Length() < 24);
            if (de != null)
            {
                bool vertical = into == KeyFunctions.Up || into == KeyFunctions.Down;
                for (int i = 0; i < 120; i++)
                {
                    float off = vertical ? (de.Hitbox.Center.X - 5) - Pos.X : (de.Hitbox.Center.Y - 6) - Pos.Y;
                    if (Math.Abs(off) <= 1.5f)
                    {
                        break;
                    }
                    yield return K(vertical ? (off < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (off < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                }
            }
            for (int i = 0; i < 240 && GlobalState.CURRENT_MAP_NAME == here; i++)
            {
                yield return GlobalState.ScreenTransition ? K() : K(into);
            }
            yield return Wait(60);
        }

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
                    if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                    {
                        System.Console.Error.WriteLine($"  chests: no route from {hereT} to any of {string.Join(" ", chests.Select(c => $"{c.Position.X:0},{c.Position.Y:0}"))} (failed {_failedChests.Count})");
                    }
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

        /// <summary>Walks to world point x,y, pressing the nearest reachable unpressed button (whose gates may open the way) while there is no route.</summary>
        protected static IEnumerable<object> PlanTo(float x, float y)
        {
            for (int n = 0; n < 20; n++)
            {
                BotNav.ResetDustCache();
                bool jumpOk = GlobalState.inventory.CanJump;
                var hereT = BotNav.TileOf(Pos);
                if (BotNav.RaftPath(hereT, OnConveyor(hereT), BotNav.TileOf(new Vector2(x, y)), jumpOk) != null)
                {
                    yield return GoGlobal(x, y);
                    yield break;
                }
                var btn = AnodyneSharp.Entities.EntityManager.GetMapEntities(GlobalState.CURRENT_MAP_NAME)
                    .Where(pr => pr.Type != null && pr.Type.Name == "Button" && !_pressedButtons.Contains(pr.EntityID))
                    .Select(pr => (pr, rp: BotNav.RaftPath(hereT, OnConveyor(hereT), BotNav.TileOf(new Vector2(pr.Position.X + 3, pr.Position.Y + 2)), jumpOk)))
                    .Where(b => b.rp != null).OrderBy(b => b.rp.Count).Select(b => b.pr).FirstOrDefault();
                if (btn == null)
                {
                    throw new InvalidOperationException($"bot: no route to ({x},{y}) on {Map} and no button left to press");
                }
                _pressedButtons.Add(btn.EntityID);
                BotRunner.Status = $"to the button at {btn.Position.X:0},{btn.Position.Y:0} (on the way to {x:0},{y:0})";
                System.Console.Error.WriteLine($"  plan to ({x},{y}): pressing the button at {btn.Position.X},{btn.Position.Y}");
                yield return GoGlobal(btn.Position.X + 3, btn.Position.Y + 2);
                yield return Wait(90);
            }
            throw new InvalidOperationException($"bot: no route to ({x},{y}) on {Map}");
        }

        /// <summary>Opens the pause menu, goes to the Map tab and takes the "return to this area's Nexus pad" warp.</summary>
        /// <summary>Pause menu -> Items (the second tab) -> down to broom row k (0 plain, 1 extend, 2 widen, 3 swap) -> equip -> close.</summary>
        protected static IEnumerable<object> MenuEquipBroom(int k)
        {
            yield return Tap(KeyFunctions.Pause);
            yield return Wait(20);
            for (int i = 0; i < 8; i++)
            {
                yield return Tap(KeyFunctions.Up);
                yield return Wait(4);
            }
            yield return Tap(KeyFunctions.Down);
            yield return Wait(4);
            yield return Tap(KeyFunctions.Accept);
            yield return Wait(10);
            for (int i = 0; i < k; i++)
            {
                yield return Tap(KeyFunctions.Down);
                yield return Wait(4);
            }
            yield return Tap(KeyFunctions.Accept);
            yield return Wait(10);
            System.Console.Error.WriteLine($"equipped broom: {GlobalState.inventory.EquippedBroom}");
            yield return Tap(KeyFunctions.Pause);
            yield return Wait(20);
        }

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

        /// <summary>Debug: the tiles between two points (. walkable, ~ water, # not), for a route that could not be found.</summary>
        private static void DumpTiles(Point a, Point b)
        {
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") == null)
            {
                return;
            }
            BotNav.RefreshShooterTiles();
            int x0 = Math.Min(a.X, b.X) - 3, x1 = Math.Max(a.X, b.X) + 3;
            System.Console.Error.WriteLine($"  tiles from x {x0}");
            for (int ty = Math.Min(a.Y, b.Y) - 3; ty <= Math.Max(a.Y, b.Y) + 3; ty++)
            {
                var row = new System.Text.StringBuilder();
                for (int tx = x0; tx <= x1; tx++)
                {
                    var t = new Point(tx, ty);
                    row.Append(t == a ? '@' : t == b ? 'G' : BotNav.Walkable(t) ? '.' : BotNav.IsConveyor(t) ? '~' : BotNav.IsGateTile(t) ? 'g' : '#');
                }
                System.Console.Error.WriteLine($"  tiles {ty,3}: {row}");
            }
        }

        /// <summary>True while a dash trap is being baited and passed (its own walking must not start another pass).</summary>
        protected static bool PassingTrap;

        /// <summary>
        /// An idle OnSightDashTrap whose tile the route walks over, coming at it along its line for at least three tiles: the trap,
        /// a side tile beside the route two tiles short of it (out of its line, to bait it from), and the trap tile's index in the path.
        /// </summary>
        protected static (Entity, Point, int)? SightTrapOnPath(List<Point> path)
        {
            foreach (var e in BotRunner.Entities())
            {
                if (!e.exists || e.GetType().Name != "OnSightDashTrap" || e.velocity != Vector2.Zero || !GlobalState.inventory.CanJump)
                {
                    continue;
                }
                var t = BotNav.TileOf(e.Position + new Vector2(8, 8));
                int k = path.IndexOf(t);
                if (k < 0)
                {
                    continue;
                }
                var prev = new List<Point> { BotNav.TileOf(Pos) };
                prev.AddRange(path.Take(k));
                int n = prev.Count;
                if (n < 3)
                {
                    continue;
                }
                var a = prev[n - 1];
                var b = prev[n - 2];
                var step = new Point(t.X - a.X, t.Y - a.Y);
                if (b != new Point(a.X - step.X, a.Y - step.Y))
                {
                    continue; // not coming at it along its line
                }
                foreach (var side in new[] { new Point(step.Y, step.X), new Point(-step.Y, -step.X) })
                {
                    var st = new Point(b.X + side.X, b.Y + side.Y);
                    if (BotNav.Walkable(st))
                    {
                        return (e, st, k);
                    }
                }
            }
            return null;
        }

        /// <summary>
        /// Walks the route at the dash trap on path[ti] head on: it charges as soon as we line up, and is jumped (the air is safe)
        /// when it is a few pixels off; we land behind it and run on along the route while it goes to the wall and comes back at half speed.
        /// </summary>
        protected static IEnumerable<object> PassSightTrap(Entity trap, List<Point> path, int ti)
        {
            var tt = path[ti];
            var grid0 = Grid;
            bool jumped = false;
            for (int pi = 0; pi < path.Count && pi <= ti + 3 && Grid == grid0; pi++)
            {
                if (pi + 1 < path.Count && InBounceLane(BotNav.CentreOf(path[pi])))
                {
                    continue; // no stopping in a bouncing trap's lane: aim past it (we jump across)
                }
                var c = BotNav.CentreOf(path[pi]);
                for (int f = 0; f < 90 && Grid == grid0 && !GlobalState.ScreenTransition; f++)
                {
                    float dx = c.X - Pos.X, dy = c.Y - Pos.Y;
                    if (Math.Abs(dx) < 2f && Math.Abs(dy) < 2f)
                    {
                        break;
                    }
                    var keys = new List<KeyFunctions>();
                    if (Math.Abs(dx) >= 1f)
                    {
                        keys.Add(dx < 0 ? KeyFunctions.Left : KeyFunctions.Right);
                    }
                    if (Math.Abs(dy) >= 1f)
                    {
                        keys.Add(dy < 0 ? KeyFunctions.Up : KeyFunctions.Down);
                    }
                    if (!jumped && trap.exists && trap.velocity != Vector2.Zero && Player.state != AnodyneSharp.Entities.PlayerState.AIR)
                    {
                        // Coming at us: the gap between its front and ours along its line of travel.
                        var r = trap.Hitbox;
                        var me = Player.Hitbox;
                        bool overlapX = me.Left < r.Right && me.Right > r.Left, overlapY = me.Top < r.Bottom && me.Bottom > r.Top;
                        float gap = trap.velocity.Y < 0 && overlapX ? r.Top - me.Bottom
                            : trap.velocity.Y > 0 && overlapX ? me.Top - r.Bottom
                            : trap.velocity.X < 0 && overlapY ? r.Left - me.Right
                            : trap.velocity.X > 0 && overlapY ? me.Left - r.Right : float.MaxValue;
                        if (gap >= -2 && gap <= 7)
                        {
                            jumped = true;
                            keys.Add(KeyFunctions.Cancel);
                            BotRunner.Status = $"dash trap at {tt.X},{tt.Y}: jump it";
                        }
                    }
                    if (!keys.Contains(KeyFunctions.Cancel) && Player.state != AnodyneSharp.Entities.PlayerState.AIR && BounceBandAhead(keys))
                    {
                        keys.Add(KeyFunctions.Cancel); // a bouncing trap's lane is too wide to walk across between its passes: jump it
                        BotRunner.Status = $"dash trap at {tt.X},{tt.Y}: jump a bouncing one's lane";
                    }
                    if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null && f % 4 == 0)
                    {
                        System.Console.Error.WriteLine($"  trap pass f{BotRunner.Frame} me={Player.Hitbox} air={Player.state == AnodyneSharp.Entities.PlayerState.AIR} trap={trap.Hitbox} v={trap.velocity} to {path[pi]}");
                    }
                    BotRunner.NotStuck();
                    yield return K(keys.ToArray());
                }
            }
            while (GlobalState.ScreenTransition)
            {
                yield return K();
            }
        }

        /// <summary>The lanes of the bouncing dash traps near us: the strip they sweep (to the walls) along their line of travel.</summary>
        private static List<Rectangle> BounceLanes()
        {
            var lanes = new List<Rectangle>();
            foreach (var e in BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "BounceDashTrap" && (e.Position - Pos).Length() < 80))
            {
                var r = e.Hitbox;
                lanes.Add(e.velocity.X != 0 ? new Rectangle(r.Left - 160, r.Top - 1, r.Width + 320, r.Height + 2) : new Rectangle(r.Left - 1, r.Top - 160, r.Width + 2, r.Height + 320));
            }
            return lanes;
        }

        private static bool InBounceLane(Vector2 c) => BounceLanes().Any(l => l.Contains((int)c.X, (int)c.Y));

        /// <summary>True when the keys would take our box into a bouncing trap's lane within the next 3 frames (from outside it).</summary>
        private static bool BounceBandAhead(List<KeyFunctions> keys)
        {
            var box = Player.Hitbox;
            var lanes = BounceLanes();
            if (lanes.Count == 0 || lanes.Any(l => l.Intersects(box)))
            {
                return false;
            }
            var v = Vector2.Zero;
            foreach (var k in keys)
            {
                v += DirVector(k);
            }
            var off = v * 3f * WalkPxPerFrame;
            var next = new Rectangle(box.X + (int)Math.Round(off.X), box.Y + (int)Math.Round(off.Y), box.Width, box.Height);
            return lanes.Any(l => l.Intersects(next));
        }

        /// <summary>True while the automatic emitter plugging runs (its own walking must not trigger more plugging).</summary>
        protected static bool Plugging;

        protected static IEnumerable<object> RaftTo(float gx, float gy)
        {
            if (InRaft)
            {
                throw new InvalidOperationException("bot: nested raft request");
            }
            InRaft = true;
            BotNav.RaftBlocked.Clear();
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
                    DumpTiles(here, goal);
                    throw new InvalidOperationException($"bot: no raft route from {here} to {goal} on {Map}");
                }
                int wi = BotNav.Wade ? -1 : path.FindIndex(t => OnConveyor(t) && !(jump && BotNav.NarrowWater(t)));
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
                var lastTile = BotNav.TileOf(Pos);
                var rideLast = Pos;
                int rideStall = 0, raftSwings = 0;
                KeyFunctions? lastRideKey = null;
                var edgeCrossings = new Dictionary<(Point, Point), int>();
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
                        // Keep pushing the way the raft crossed: the player lands just inside the new screen, and a current running
                        // back towards the edge would carry it straight back over on the first frame after the scroll.
                        yield return lastRideKey.HasValue ? K(lastRideKey.Value) : K();
                        continue;
                    }
                    var cur = BotNav.TileOf(Pos);
                    if (cur.X / 10 != lastTile.X / 10 || cur.Y / 10 != lastTile.Y / 10)
                    {
                        // A current that pushes the raft back over a screen edge it was ridden across: after a few goes, route round it.
                        var edge = (lastTile, cur);
                        edgeCrossings[edge] = edgeCrossings.GetValueOrDefault(edge) + 1;
                        if (edgeCrossings[edge] >= 3 && edgeCrossings.GetValueOrDefault((cur, lastTile)) >= 3)
                        {
                            // The whole screen edge (its water is one current), not just this column (row).
                            RaftLog($"edge {lastTile}<->{cur} keeps pushing back: routing round that screen edge");
                            var along = lastTile.X == cur.X ? new Point(1, 0) : new Point(0, 1);
                            int origin = lastTile.X == cur.X ? lastTile.X / 10 * 10 : lastTile.Y / 10 * 10;
                            for (int k = 0; k < 10; k++)
                            {
                                var a = along.X != 0 ? new Point(origin + k, lastTile.Y) : new Point(lastTile.X, origin + k);
                                var b = along.X != 0 ? new Point(origin + k, cur.Y) : new Point(cur.X, origin + k);
                                if (BotNav.IsConveyor(a) && BotNav.IsConveyor(b))
                                {
                                    BotNav.RaftBlocked.Add((a, b));
                                    BotNav.RaftBlocked.Add((b, a));
                                }
                            }
                            break;
                        }
                    }
                    lastTile = cur;
                    bool wet = OnConveyor(cur);
                    wasWet |= wet;
                    if (cur == goal && !(wasWet && !wet))
                    {
                        yield break;
                    }
                    if (!wet && wasWet)
                    {
                        RaftLog($"ride ends on land at {cur}");
                        if (Player.broom.dust == null && BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "Dust" && (e.Position - Pos).Length() < 40))
                        {
                            yield return PickUpDust(true);
                        }
                        BotNav.ResetDustCache(); // the raft's dust has moved (or is in the broom now)
                        if (cur == goal)
                        {
                            yield break; // (the next hop may need that dust for its own raft)
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
                    // The 10px hitbox has ~3px of slack in a 16px tile: only correct sideways when well off, else go along the route (a raft
                    // that cannot slide the last pixel sideways would otherwise sit there for ever).
                    var rideKey = Math.Abs(ddx) > 3.5f || (Math.Abs(ddx) > 2f && Math.Abs(ddy) < 3f) ? (ddx < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (ddy < 0 ? KeyFunctions.Up : KeyFunctions.Down);
                    // Snagged on a corner (a lily pad, the bank) going along the route, with the current dragging back off the
                    // centre line: hold the way along and the sideways correction together.
                    bool alongY = rideKey == KeyFunctions.Up || rideKey == KeyFunctions.Down;
                    // (a mark, not the last frame: a snagged raft jitters a pixel back and forth)
                    if (Math.Abs(alongY ? Pos.Y - rideLast.Y : Pos.X - rideLast.X) > 2f)
                    {
                        rideStall = 0;
                        rideLast = Pos;
                    }
                    else
                    {
                        rideStall++;
                    }
                    float side = alongY ? ddx : ddy;
                    if (rideStall > 10 && Math.Abs(side) > 0.5f)
                    {
                        yield return K(rideKey, alongY ? (side < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (side < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                        continue;
                    }
                    lastRideKey = rideKey;
                    // An Annoyer or Slime at arm's length: swing at it from the raft (the broom never picks up the dust we ride on). The
                    // Forest's channel up to the Mushroom card has a Slime sitting in it and an Annoyer that comes at a stalled raft.
                    if (wet && HasRaft() && raftSwings < 40 && Enemies().Where(e => e.GetType().Name is "Annoyer" or "Slime")
                        .Select(e => new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - new Vector2(Player.Hitbox.Center.X, Player.Hitbox.Center.Y))
                        .Where(v => v.Length() < (rideStall > 20 ? 40f : 24f)).OrderBy(v => v.Length()).Cast<Vector2?>().FirstOrDefault() is Vector2 foe)
                    {
                        raftSwings++;
                        yield return K(Math.Abs(foe.X) > Math.Abs(foe.Y) ? (foe.X < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (foe.Y < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                        yield return Tap(KeyFunctions.Accept);
                        yield return Wait(14);
                        continue;
                    }
                    // Bullets over the water (Red Cave's four-shooters) are dodged like on land: hold back (the raft drifts) or hop over them.
                    yield return SafeMove(rideKey, null, 16f);
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
            // The corridor Shieldy we shoved east can end up right under the button: it is only touching distance, never in the way.
            bool savedDodge = DodgeEnabled;
            DodgeEnabled = false;
            try
            {
                yield return StandUntilGateOpens(112, 272);
            }
            finally
            {
                DodgeEnabled = savedDodge;
            }
        }

        public static string DebugEnemies() => string.Join(" ", Enemies().Select(e => $"{e.GetType().Name}@{e.Hitbox.X},{e.Hitbox.Y}v{e.velocity.X:0},{e.velocity.Y:0}"));

        /// <summary>Enemies the bot has decided are not worth the trouble on this screen (out of reach, asleep behind a wall): no fight or route looks at them again.</summary>
        protected static readonly HashSet<Entity> IgnoredEnemies = new();
        private static Point _ignoredGrid = new(-1, -1);
        private static string _ignoredMap = "";

        static readonly Dictionary<Entity, (Vector2 pos, long moved)> _annoyerSeen = new();
        internal static Entity SwingTarget;
        internal static long SwingTargetFrame = -1;
        static readonly Dictionary<Entity, long> _teleGuyPoof = new();

        /// <summary>
        /// A TeleGuy stands still until the broom hits it; then it poofs, comes back beside us and charges for 1.5s (~2.5s in all).
        /// </summary>
        internal static bool TeleGuyActive(Entity tg)
        {
            if (tg.CurAnimName is "poof" or "unpoof")
            {
                _teleGuyPoof[tg] = BotRunner.Frame;
            }
            return _teleGuyPoof.TryGetValue(tg, out long f) && BotRunner.Frame - f < 160;
        }

        /// <summary>
        /// A swing in a fight that would broom an idle TeleGuy that is not the one we are after is held back: it would wake it, and it
        /// charges at us while something else is about.
        /// </summary>
        internal static KeyFunctions[] SwingGuard(KeyFunctions[] held)
        {
            if (Player == null || !held.Contains(KeyFunctions.Accept) || SwingTargetFrame < BotRunner.Frame - 1 || SwingTarget is AnodyneSharp.Entities.Enemy.Apartment.TeleGuy)
            {
                return held;
            }
            var pb = Player.Hitbox;
            var reach = Player.facing switch
            {
                Facing.UP => new Rectangle(pb.X - 8, pb.Y - 26, pb.Width + 16, 26),
                Facing.DOWN => new Rectangle(pb.X - 8, pb.Bottom, pb.Width + 16, 26),
                Facing.LEFT => new Rectangle(pb.X - 26, pb.Y - 8, 26, pb.Height + 16),
                _ => new Rectangle(pb.Right, pb.Y - 8, 26, pb.Height + 16),
            };
            foreach (var e in BotRunner.Entities())
            {
                if (e.exists && e is AnodyneSharp.Entities.Enemy.Apartment.TeleGuy && !TeleGuyActive(e) && e.Hitbox.Intersects(reach))
                {
                    return held.Where(k => k != KeyFunctions.Accept).ToArray();
                }
            }
            return held;
        }
        /// <summary>
        /// A fight's steps never take us onto a door or portal: the planner keeps off them, but a fight's own dodging does not, and a
        /// Crowd DownDoor beside a Dog sent us to a screen across the map mid-fight.
        /// </summary>
        internal static KeyFunctions[] DoorGuard(KeyFunctions[] held)
        {
            if (Player == null || SwingTargetFrame < BotRunner.Frame - 1 || Environment.GetEnvironmentVariable("ANODYNE_NO_DOORGUARD") != null)
            {
                return held;
            }
            var pb = Player.Hitbox;
            var doors = BotRunner.Entities().Where(e => e.exists && (e.GetType().Name.EndsWith("Door") || e.GetType().Name.EndsWith("Portal"))).ToList();
            if (doors.Count == 0)
            {
                return held;
            }
            return held.Where(k =>
            {
                if (k is not (KeyFunctions.Left or KeyFunctions.Right or KeyFunctions.Up or KeyFunctions.Down))
                {
                    return true;
                }
                var d = DirVector(k) * 3f;
                var next = new Rectangle(pb.X + (int)d.X, pb.Y + (int)d.Y, pb.Width, pb.Height);
                return !doors.Any(e => e.Hitbox.Intersects(next) && !e.Hitbox.Intersects(pb));
            }).ToArray();
        }

        /// <summary>
        /// A bullet Slime's shot (aimed at us, 40px/s, from its corner): hop over it, as only a player on the ground is hurt. A shot fired from
        /// broom's length (where the Slime policy stands) touches us the frame it spawns, so the hop goes off just before the Slime's shot timer
        /// runs out, sideways when there is floor there (the shot then flies through where we were). (Hopping shots from further off as they
        /// arrive did worse: the landings came down on the Slimes or other shots.) ANODYNE_NO_BULLETHOP turns it off.
        /// </summary>
        private static KeyFunctions? _hopDir;
        private static long _hopUntil;

        internal static KeyFunctions[] BulletHop(KeyFunctions[] held)
        {
            if (Player != null && Player.state == AnodyneSharp.Entities.PlayerState.AIR && BotRunner.Frame <= _hopUntil && _hopDir is KeyFunctions hd)
            {
                return new[] { hd }; // (keep going across the shot's line all the way: steering in the air elsewhere lands in holes)
            }
            if (Player == null || Player.state != AnodyneSharp.Entities.PlayerState.GROUND || Environment.GetEnvironmentVariable("ANODYNE_NO_BULLETHOP") != null)
            {
                return held;
            }
            var pb = Player.Hitbox;
            pb.Inflate(1, 1);
            foreach (var sl in BotRunner.Entities().OfType<AnodyneSharp.Entities.Enemy.Slime>().Where(e => e.exists))
            {
                var shots = sl.SubEntities().OfType<AnodyneSharp.Entities.Enemy.Slime.Bullet>().ToList();
                if (shots.Count == 0)
                {
                    continue; // (a plain Slime)
                }
                var sb = sl.Hitbox;
                int gap = Math.Max(Math.Max(sb.Left - pb.Right, pb.Left - sb.Right), Math.Max(sb.Top - pb.Bottom, pb.Top - sb.Bottom));
                if (Environment.GetEnvironmentVariable("ANODYNE_HOP_TRACE") != null && gap < 40) { System.Console.Error.WriteLine($"  slimehop f{BotRunner.Frame} sl={sl.Position} due={SlimeShotIn(sl)} gap={gap} me={Player.Position} st={Player.state}"); }
                if (gap < 30 && SlimeShotIn(sl) is float due && due <= 1.5f / 60f)
                {
                    BotRunner.Status = "hop the slime's next shot";
                    if (HopAcross(Player.Position - sl.Position) is KeyFunctions[] hop)
                    {
                        return hop;
                    }
                }
            }
            return held;
        }

        /// <summary>
        /// A jump across a shot's line (it moves too slowly to pass under a jump in place): sideways to `line` where we land on floor, else
        /// back along it, else forward. A jump keeps the speed we had along any axis no key is held on, so the landing counts that too (a hop
        /// "in place" while walking drifts on into a hole). Null when no landing is safe.
        /// </summary>
        private static KeyFunctions[] HopAcross(Vector2 line)
        {
            bool alongX = Math.Abs(line.X) > Math.Abs(line.Y);
            var keys = alongX
                ? new[] { KeyFunctions.Up, KeyFunctions.Down, line.X > 0 ? KeyFunctions.Right : KeyFunctions.Left, line.X > 0 ? KeyFunctions.Left : KeyFunctions.Right }
                : new[] { KeyFunctions.Left, KeyFunctions.Right, line.Y > 0 ? KeyFunctions.Down : KeyFunctions.Up, line.Y > 0 ? KeyFunctions.Up : KeyFunctions.Down };
            const float air = 0.46f; // (the jump's length, s)
            foreach (var k in keys)
            {
                var d = DirVector(k);
                var drift = Player.velocity * air;
                var move = d.X != 0 ? new Vector2(d.X * 24f, drift.Y) : new Vector2(drift.X, d.Y * 24f);
                var land = Player.Hitbox;
                land.Offset((int)Math.Round(move.X), (int)Math.Round(move.Y));
                land.Inflate(3, 3); // (no hole tile at all under the landing: the jump's length is not exact, and the fight walks on as we land)
                if (new[] { (land.Left, land.Top), (land.Right - 1, land.Top), (land.Left, land.Bottom - 1), (land.Right - 1, land.Bottom - 1) }
                    .Select(c => new Point((int)Math.Floor(c.Item1 / 16f), (int)Math.Floor(c.Item2 / 16f))).All(t => BotNav.Walkable(t) && !BotNav.IsHole(t)))
                {
                    _hopDir = k;
                    _hopUntil = BotRunner.Frame + 40;
                    return new[] { KeyFunctions.Cancel, k };
                }
            }
            return null;
        }

        /// <summary>Whether a Dog is in its Attack state (its three dashes), or null when that cannot be read.</summary>
        private static bool? DogAttacking(Entity dog)
        {
            const System.Reflection.BindingFlags F = System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance;
            var root = dog.GetType().GetField("_state", F)?.GetValue(dog);
            var abs = typeof(AnodyneSharp.FSM.TimerState).BaseType;
            var active = root == null || abs == null ? null : abs.GetField("activeChildren", F)?.GetValue(root) as System.Collections.IEnumerable;
            var top = active?.Cast<object>().FirstOrDefault();
            return top == null ? null : top.GetType().Name == "AttackState";
        }

        /// <summary>Seconds until a bullet Slime's next shot (its Move state's ShootTimer), or null when it is not moving (hurt, dying).</summary>
        private static float? SlimeShotIn(Entity slime)
        {
            const System.Reflection.BindingFlags F = System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance;
            var root = slime.GetType().GetField("state", F)?.GetValue(slime);
            var abs = typeof(AnodyneSharp.FSM.TimerState).BaseType;
            if (root == null || abs == null)
            {
                return null;
            }
            var active = abs.GetField("activeChildren", F)?.GetValue(root) as System.Collections.IEnumerable;
            var move = active?.Cast<object>().FirstOrDefault() as AnodyneSharp.FSM.TimerState;
            if (move == null)
            {
                return null;
            }
            var now = (float)typeof(AnodyneSharp.FSM.TimerState).GetField("current", F).GetValue(move);
            foreach (var t in (System.Collections.IEnumerable)typeof(AnodyneSharp.FSM.TimerState).GetField("timers", F).GetValue(move))
            {
                var tt = t.GetType();
                if ((string)tt.GetField("name").GetValue(t) == "ShootTimer")
                {
                    return (float)tt.GetField("current").GetValue(t) - now;
                }
            }
            return null;
        }

        /// <summary>
        /// Where to take off (Pos along `dir`) to jump a stopped spike roller lying across the way: right up to it, landing (~28px on) clear
        /// past it on floor. Null when it is too thick along that way or there is no floor beyond.
        /// </summary>
        internal static float? RollerHop(Entity roller, KeyFunctions dir)
        {
            var bh = roller.Hitbox;
            var pb = Player.Hitbox;
            const float Jump = 27f;
            float off = dir switch
            {
                // (5px short: JumpToward runs a frame past the mark, and touching its edge already hurts)
                KeyFunctions.Right => bh.Left - pb.Width - 5 - pb.X,
                KeyFunctions.Left => pb.X - bh.Right - 5,
                KeyFunctions.Down => bh.Top - pb.Height - 5 - pb.Y,
                _ => pb.Y - bh.Bottom - 5,
            };
            var d = DirVector(dir);
            var take = new Vector2(pb.X, pb.Y) + d * Math.Max(0, off);
            var land = take + d * (Jump + 2f);
            var landBox = new Rectangle((int)land.X, (int)land.Y, pb.Width, pb.Height);
            if (off < -4 || landBox.Intersects(bh) || !StandsSafe(landBox))
            {
                return null;
            }
            var at = take - new Vector2(pb.X, pb.Y) + Pos;
            return dir is KeyFunctions.Left or KeyFunctions.Right ? at.X : at.Y;
        }

        /// <summary>
        /// Walks to `x,y` and pushes `dir` into the keyblock beside it until it opens: a timed push misses whenever something is in the
        /// way (the Crowd's wandering people stand between us and its keyblock), so wait for the way to clear and push again.
        /// </summary>
        protected static IEnumerable<object> OpenKeyBlock(float x, float y, KeyFunctions dir)
        {
            var at = new Vector2(x, y);
            var gate = BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Gadget.SmallKeyGate)
                .OrderBy(e => (e.Center - (at + DirVector(dir) * 16)).LengthSquared()).FirstOrDefault();
            if (gate == null)
            {
                throw new InvalidOperationException($"bot: no keyblock by {x},{y} on {Map}");
            }
            for (int attempt = 0; attempt < 12 && gate.Solid; attempt++)
            {
                BotRunner.Status = $"keyblock: clear of people (try {attempt + 1})";
                for (int w = 0; w < 300 && BotRunner.Entities().Any(e => e.exists && e is AnodyneSharp.Entities.Enemy.Person
                        && (e.Center - gate.Center).Length() < 28); w++)
                {
                    BotRunner.NotStuck();
                    yield return K();
                }
                yield return WalkTo(x, y);
                BotRunner.Status = $"keyblock: push (try {attempt + 1})";
                for (int f = 0; f < 50 && gate.Solid; f++)
                {
                    yield return K(dir);
                }
                yield return Wait(20);
                if (!string.IsNullOrEmpty(GlobalState.Dialogue))
                {
                    yield return ReadDialogue(); // ("you need a key": the push did reach it)
                }
            }
            if (gate.Solid)
            {
                throw new InvalidOperationException($"bot: keyblock at {gate.Position} would not open on {Map} ({GlobalState.inventory.GetCurrentMapKeys()} keys)");
            }
        }

        /// <summary>Tiles by the enemies other than Slashers (TeleGuys, Gas Guys...) that a run from a Slasher's cut keeps off.</summary>
        static HashSet<Point> OthersBySlashers()
        {
            var set = new HashSet<Point>();
            if (Environment.GetEnvironmentVariable("ANODYNE_NO_SLAVOID") != null)
            {
                return set;
            }
            foreach (var e in BotRunner.Entities().Where(e => e.exists && e.GetType().Name is "TeleGuy" or "GasGuy"))
            {
                var r = e.Hitbox;
                r.Inflate(6, 6);
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

        static readonly HashSet<Entity> _unreachableHearts = new();
        static Entity _heartChased;
        static float _heartBest;
        static int _heartStall;

        /// <summary>
        /// An Annoyer sits still until we come within 64px; awake, it always flies to us (over holes too), so chasing it is no use:
        /// it has moved within the last half second.
        /// </summary>
        protected static bool AnnoyerAwake(Entity a)
        {
            long now = BotRunner.Frame;
            if (!_annoyerSeen.TryGetValue(a, out var seen))
            {
                _annoyerSeen[a] = (a.Position, -1000);
                return false;
            }
            if (seen.pos != a.Position)
            {
                seen = (a.Position, now);
                _annoyerSeen[a] = seen;
            }
            return now - seen.moved <= 30;
        }

        protected static List<Entity> Enemies()
        {
            if (_ignoredGrid != Grid || _ignoredMap != Map)
            {
                IgnoredEnemies.Clear();
                _ignoredGrid = Grid;
                _ignoredMap = Map;
            }
            if (IgnoredEnemies.Count > 0 && BotRunner.Player != null)
            {
                // One given up on as out of reach that has come to us (an Annoyer leaving its perch) is a target again.
                var pc = Player.Hitbox.Center;
                IgnoredEnemies.RemoveWhere(e => e.exists && e.GetType().Name == "Annoyer" && Math.Abs(e.Hitbox.Center.X - pc.X) + Math.Abs(e.Hitbox.Center.Y - pc.Y) < 48);
            }
            return BotRunner.Entities().Where(e => e.exists && e.GetType().IsDefined(typeof(EnemyAttribute), false) && !IgnoredEnemies.Contains(e)).ToList();
        }

        /// <summary>
        /// Two-button gate with a Mover: stand on one button, and make the Mover cross the other on its way to us. Lure the Mover to a spot
        /// beyond the other button (on the line from our button through it), then walk onto our button; it follows and presses its button.
        /// </summary>
        protected static IEnumerable<object> MoverOnButton(int seconds)
        {
            int attempt = 0;
            for (int f = 0; f < seconds * 60 && GlobalState.PUZZLES_SOLVED < 2; attempt++)
            {
                var buttons = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Button").ToList();
                var mover = BotRunner.Entities().FirstOrDefault(e => e.exists && e.GetType().Name == "Mover");
                if (buttons.Count < 2 || mover == null)
                {
                    yield break;
                }
                var mine = buttons[attempt % buttons.Count];
                var theirs = buttons[(attempt + 1) % buttons.Count];
                var dir = theirs.Position - mine.Position;
                dir.Normalize();
                // the lure spot: beyond their button, as far as a walkable tile allows
                Point? spot = null;
                for (int d = 5; d >= 2 && spot == null; d--)
                {
                    var q = theirs.Position + dir * (d * 16);
                    var t = BotNav.TileOf(q);
                    if (t.X >= Grid.X * 10 + 1 && t.X < Grid.X * 10 + 9 && t.Y >= Grid.Y * 10 + 1 && t.Y < Grid.Y * 10 + 9 && BotNav.FindPath(BotNav.TileOf(Pos), t) != null)
                    {
                        spot = t;
                    }
                }
                if (spot == null)
                {
                    continue;
                }
                BotRunner.Status = $"buttons: lure mover to {spot.Value.X},{spot.Value.Y}, then stand on {mine.Position}";
                yield return GoTo(spot.Value.X * 16, spot.Value.Y * 16);
                for (int w = 0; w < 10 && (mover.Position - Pos).Length() > 24; w++)
                {
                    yield return Wait(30);
                    f += 30;
                }
                yield return GoTo(mine.Position.X, mine.Position.Y);
                for (int w = 0; w < 8 && GlobalState.PUZZLES_SOLVED < 2; w++)
                {
                    yield return Wait(30);
                    f += 30;
                }
            }
        }

        /// <summary>
        /// Red Cave Movers home in on the player in 1s bursts and die in an On-Off steam beam. Stand where the straight line from the mover to
        /// us crosses a beam but we are well clear of every beam, and wait; try another spot if nothing happens.
        /// </summary>
        protected static IEnumerable<object> LureMovers(int seconds)
        {
            int attempt = 0;
            for (int f = 0; f < seconds * 60; f += 30)
            {
                // a Mover caught by the steam plays its death animation for a while before it stops existing: it is done already
                var movers = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Mover" && e.CurAnimName != "Die").ToList();
                if (movers.Count == 0)
                {
                    yield break;
                }
                var beams = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "OnOffLaser").SelectMany(e => e.SubEntities()).ToList();
                var rects = beams.Select(b => new Rectangle((int)b.Position.X, (int)b.Position.Y, b.width, b.height)).ToList();
                var m = movers.OrderBy(e => (e.Position - Pos).Length()).First();
                var mc = m.Position + new Vector2(8, 8);
                var here = BotNav.TileOf(Pos);
                var cands = new List<(Point t, int cost)>();
                for (int tx = Grid.X * 10; tx < Grid.X * 10 + 10; tx++)
                {
                    for (int ty = Grid.Y * 10; ty < Grid.Y * 10 + 10; ty++)
                    {
                        if (!BotNav.Walkable(new Point(tx, ty)))
                        {
                            continue; // FindPath(a, b) forces its goal, so a wall would pass for a spot
                        }
                        var c = new Vector2(tx * 16 + 8, ty * 16 + 8);
                        var box = new Rectangle(tx * 16 - 20, ty * 16 - 20, 56, 56);
                        if (rects.Any(r => r.Intersects(box)))
                        {
                            continue;
                        }
                        bool crosses = false;
                        var dir = c - mc;
                        int steps = (int)(dir.Length() / 4);
                        for (int i = 1; i < steps && !crosses; i++)
                        {
                            var q = mc + dir * i / steps;
                            crosses = rects.Any(r => r.Contains((int)q.X, (int)q.Y));
                        }
                        if (!crosses)
                        {
                            continue;
                        }
                        var path = BotNav.FindPath(here, new Point(tx, ty));
                        if (path != null)
                        {
                            bool through = path.Any(pt => rects.Any(r => r.Intersects(new Rectangle(pt.X * 16 - 6, pt.Y * 16 - 6, 28, 28))));
                            cands.Add((new Point(tx, ty), path.Count + (int)(dir.Length() / 16) + (through ? 30 : 0)));
                        }
                    }
                }
                if (cands.Count == 0)
                {
                    BotRunner.Status = "lure: no spot";
                    yield return Wait(30);
                    continue;
                }
                var pick = cands.OrderBy(c => c.cost).ElementAt(Math.Min(attempt, cands.Count - 1)).t;
                BotRunner.Status = $"lure mover to beam: stand at {pick.X},{pick.Y}";
                if (pick != here)
                {
                    var pickPath = BotNav.FindPath(here, pick);
                    if (pickPath != null && pickPath.Any(pt => rects.Any(r => r.Intersects(new Rectangle(pt.X * 16 - 6, pt.Y * 16 - 6, 28, 28)))))
                    {
                        // the way crosses a steam jet: go the moment it has just gone down (it stays hidden for 2s, a crossing takes under a second)
                        BotRunner.Status = "lure: wait for the jet to go down";
                        // "up" includes the flicker of emerging/receding: a flickering jet is invisible every other 3 frames, which once
                        // passed for "just gone down" while it was rising, and the crossing then met the burn
                        bool wasUp = beams.Any(b => b.visible || b._flickering);
                        for (int w = 0; w < 600; w++)
                        {
                            bool up = beams.Any(b => b.visible || b._flickering);
                            if (wasUp && !up)
                            {
                                break;
                            }
                            wasUp = up;
                            yield return Wait(1);
                        }
                    }
                    yield return GoTo(pick.X * 16, pick.Y * 16);
                }
                for (int w = 0; w < 4; w++)
                {
                    yield return Wait(30);
                    f += 30;
                    if (!movers.Any(e => e.exists && e.CurAnimName != "Die"))
                    {
                        break;
                    }
                }
                if (movers.Any(e => e.exists && e.CurAnimName != "Die"))
                {
                    attempt++;
                }
            }
        }

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
        // Seer fight state (observed positions of the boss, its orbs and its wave from the previous frames)
        private static readonly Dictionary<Entity, Vector2> _seerPrev = new();
        private static readonly Dictionary<Entity, Vector2> _seerVel = new();
        private static readonly Dictionary<Entity, float> _seerOmega = new();
        private static readonly Dictionary<Entity, Vector2> _seerPrevRel = new();

        private static Vector2 CentreOfBox(Entity e) => new(e.Hitbox.Center.X, e.Hitbox.Center.Y);

        /// <summary>
        /// Learns each boss part's velocity (and the orbs' angular speed around the boss) from how it moved over the last frames, then
        /// predicts where it will be for the next Horizon frames: the boss and wave on straight lines, the orbs on circles round the boss.
        /// </summary>
        private static readonly System.Reflection.FieldInfo SeerPresetField = typeof(AnodyneSharp.Entities.Enemy.Bedroom.Seer).GetField("_preset", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);

        /// <summary>Where a boss part will be after f frames moving at v, bouncing between the vertical limits of the boss's dive (preset Y-32 .. Y+80).</summary>
        private static float _seerMinX = float.MinValue / 4, _seerMaxX = float.MaxValue / 4;

        private static Vector2 SeerPredict(Vector2 c, Vector2 v, int f, float minY, float maxY)
        {
            float x = c.X, vx = v.X;
            float y = c.Y, vy = v.Y;
            for (int k = 0; k < f; k++)
            {
                x += vx;
                // it slides sideways at 50px/s between two fixed limits
                if (x > _seerMaxX) { x = 2 * _seerMaxX - x; vx = -MathF.Abs(vx); }
                else if (x < _seerMinX) { x = 2 * _seerMinX - x; vx = MathF.Abs(vx); }
                y += vy;
                // it dives at 90px/s and climbs back at 120px/s
                if (y > maxY) { y = 2 * maxY - y; vy = -2.0f; }
                else if (y < minY) { y = 2 * minY - y; vy = 1.5f; }
            }
            return new Vector2(x, y);
        }

        private static List<Threat> SeerThreats(Entity seer, bool stage2, bool charge = false)
        {
            float minY = float.MinValue / 4, maxY = float.MaxValue / 4;
            if (stage2 && SeerPresetField?.GetValue(seer) is AnodyneSharp.Entities.EntityPreset preset)
            {
                // Position.Y limits of the dive -> centre limits
                float off = seer.Hitbox.Center.Y - seer.Position.Y;
                minY = preset.Position.Y - 32 + off;
                maxY = preset.Position.Y + 80 + off;
            }
            if (SeerPresetField?.GetValue(seer) is AnodyneSharp.Entities.EntityPreset preset2)
            {
                float offX = seer.Hitbox.Center.X - seer.Position.X;
                _seerMinX = preset2.Position.X - 50 + offX;
                _seerMaxX = preset2.Position.X + 60 + offX;
            }
            var parts = BotRunner.Entities().Where(e => e.exists && (e == seer || e.GetType().Name is "SeerOrb" or "SeerWave")).ToList();
            var seerC = CentreOfBox(seer);
            foreach (var e in parts)
            {
                var c = CentreOfBox(e);
                if (_seerPrev.TryGetValue(e, out var pc))
                {
                    var v = c - pc;
                    var smooth = _seerVel.TryGetValue(e, out var ov) ? ov * 0.6f + v * 0.4f : v;
                    // a dive moves 1.5-2px a frame: its direction is plain at once, no need to wait for the average to turn
                    if (e == seer && MathF.Abs(v.Y) >= 1f) { smooth.Y = v.Y; }
                    _seerVel[e] = smooth;
                }
                _seerPrev[e] = c;
                if (e.GetType().Name == "SeerOrb")
                {
                    var rel = c - seerC;
                    if (_seerPrevRel.TryGetValue(e, out var pr) && pr.Length() > 1f && rel.Length() > 1f)
                    {
                        float da = MathF.Atan2(rel.Y, rel.X) - MathF.Atan2(pr.Y, pr.X);
                        while (da > MathF.PI) { da -= 2 * MathF.PI; }
                        while (da < -MathF.PI) { da += 2 * MathF.PI; }
                        _seerOmega[e] = _seerOmega.TryGetValue(e, out var oo) ? oo * 0.7f + da * 0.3f : da;
                    }
                    _seerPrevRel[e] = rel;
                }
            }
            var threats = new List<Threat>();
            var seerV = _seerVel.TryGetValue(seer, out var sv) ? sv : Vector2.Zero;
            int waveStopBottom = int.MaxValue;
            foreach (var d in BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Dust"))
            {
                waveStopBottom = Math.Min(waveStopBottom, d.Hitbox.Top - 1);
            }
            foreach (var e in parts)
            {
                string name = e.GetType().Name;
                var th = new Threat { Name = name, Margin = name == "SeerWave" ? 1 : 6, Source = e };
                var vel = _seerVel.TryGetValue(e, out var vv) ? vv : Vector2.Zero;
                for (int f = 0; f <= Horizon; f++)
                {
                    Rectangle r = e.Hitbox;
                    if (name == "SeerOrb" && !stage2 && _seerOmega.TryGetValue(e, out var om))
                    {
                        var rel = CentreOfBox(e) - seerC;
                        float ang = om * f;
                        var rot = new Vector2(rel.X * MathF.Cos(ang) - rel.Y * MathF.Sin(ang), rel.X * MathF.Sin(ang) + rel.Y * MathF.Cos(ang));
                        var c = SeerPredict(seerC, seerV, f, minY, maxY) + rot;
                        r = new Rectangle((int)(c.X - r.Width / 2f), (int)(c.Y - r.Height / 2f), r.Width, r.Height);
                    }
                    else if (name == "SeerOrb" && charge)
                    {
                        // charging its wave: the orbs are pulled out sideways along the top at a steady 18px/s, away from the boss
                        r.Offset((int)(vel.X * f), (int)(vel.Y * f));
                    }
                    else if (name == "SeerOrb")
                    {
                        // stage 2: the orbs whirl round the boss at 8px: its whole neighbourhood is the hazard
                        var c = SeerPredict(seerC, seerV, f, minY, maxY);
                        r = new Rectangle((int)(c.X - 8), (int)(c.Y - 8), 32, 32); // 16px orbs on a ring of 8 round the centre, anchored by their corner: -8..+24 each way
                    }
                    else if (e == seer)
                    {
                        var c = SeerPredict(CentreOfBox(e), vel, f, minY, maxY);
                        r = new Rectangle((int)(c.X - r.Width / 2f), (int)(c.Y - r.Height / 2f), r.Width, r.Height);
                    }
                    else
                    {
                        r.Offset((int)(vel.X * f), (int)(vel.Y * f));
                        if (name == "SeerWave" && waveStopBottom < int.MaxValue && r.Bottom > waveStopBottom)
                        {
                            r.Offset(0, waveStopBottom - r.Bottom); // the wave dies on the first dust it meets: it never goes lower than that
                        }
                    }
                    th.At[f] = r;
                }
                threats.Add(th);
            }
            return threats;
        }

        /// <summary>
        /// The Red Cave boss: it sits still in the middle of its pool and every 3s fires splash bullets and raises tentacles. Both land in the
        /// left half of the room, except that tentacles rise beside us instead when one of its proximity sensors touches us at that moment,
        /// and the second such time it starts its wave and dash phases. So stay below it on the raft, strike between the 3s ticks
        /// (it is invincible for 1.3s after each hit: two hits a tick) and drop back out of its lower sensor for each tick.
        /// </summary>
        protected static IEnumerable<object> RedBossFight(int timeoutFrames = 6000)
        {
            const int Tick = 180;
            var healthField = typeof(AnodyneSharp.Entities.Enemy.Redcave.Red_Boss).GetField("health", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            BotRunner.BossDamageBudget = 0;
            int t0 = -1, lastSwing = -100, lastHit = -200;
            bool wasFlickering = false;
            for (int i = 0; i < timeoutFrames; i++)
            {
                if (Talking || GlobalState.ScreenTransition)
                {
                    yield return AdvanceKeys(i);
                    continue;
                }
                var boss = BotRunner.Entities().FirstOrDefault(e => e.exists && e.GetType().Name == "Red_Boss");
                if (boss == null)
                {
                    yield break;
                }
                BotRunner.NotStuck(); // the fight has its own timeout
                int hp = (int)healthField.GetValue(boss);
                if (hp == 0)
                {
                    yield return K(); // its death throes and the dialogue after it
                    continue;
                }
                var bb = boss.Hitbox;
                var pb = Player.Hitbox;
                if (boss._flickering && !wasFlickering)
                {
                    lastHit = i;
                }
                wasFlickering = boss._flickering;
                if (t0 < 0)
                {
                    if (BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "SplashBullet"))
                    {
                        t0 = i; // the fight's first splash: its timers run from here
                    }
                    else
                    {
                        // ride in far enough to wake it, level with it but well clear of it
                        yield return pb.X < bb.X - 30 ? K(KeyFunctions.Right) : K();
                        continue;
                    }
                }
                int phase = (i - t0) % Tick;
                bool window = phase >= 3 && phase < Tick - 22;
                BotRunner.Status = $"Red boss fight: boss hp {hp}, tick phase {phase}";
                // under it, right of where its bullets and tentacles can land; out of its lower sensor's reach at each tick
                var goal = new Point(bb.X + 8, window ? bb.Bottom + 8 : bb.Bottom + 22);
                if (pb.Right > bb.X - 2 && pb.Y < bb.Bottom + 4)
                {
                    goal = new Point(bb.X + 8, bb.Bottom + 8); // still beside it (coming in from the doorway): get under it first
                    if (pb.Bottom > bb.Bottom + 4 || pb.Right <= bb.X)
                    {
                        goal = new Point(pb.X, bb.Bottom + 8);
                    }
                }
                else if (pb.Right <= bb.X - 2 && pb.Y < bb.Bottom + 4)
                {
                    goal = new Point(pb.X + 1, bb.Bottom + 8); // left of it: drop below its bottom before going right
                    if (pb.X >= bb.X - 14)
                    {
                        goal = new Point(pb.X, bb.Bottom + 8);
                    }
                }
                int dx = goal.X - pb.X, dy = goal.Y - pb.Y;
                if (window && Math.Abs(dx) <= 2 && Math.Abs(dy) <= 2)
                {
                    if (Player.facing != Facing.UP)
                    {
                        yield return K(KeyFunctions.Up);
                        continue;
                    }
                    if (!boss._flickering && i - lastHit > 80 && i - lastSwing > 26 && phase < Tick - 40)
                    {
                        lastSwing = i;
                        yield return K(KeyFunctions.Accept);
                        continue;
                    }
                    yield return K();
                    continue;
                }
                var keys = new List<KeyFunctions>();
                if (dx > 1) { keys.Add(KeyFunctions.Right); }
                else if (dx < -1) { keys.Add(KeyFunctions.Left); }
                if (dy > 1) { keys.Add(KeyFunctions.Down); }
                else if (dy < -1) { keys.Add(KeyFunctions.Up); }
                yield return K(keys.ToArray());
            }
            throw new InvalidOperationException($"bot: Red boss fight timed out at {Pos}");
        }

        private static readonly System.Reflection.FieldInfo WallFaceInvincibleField =
            typeof(AnodyneSharp.Entities.Enemy.Crowd.Face).GetField("invincible", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        private static readonly System.Reflection.FieldInfo WallFaceTimeoutField =
            typeof(AnodyneSharp.Entities.Enemy.Crowd.Face).GetField("hit_timeout", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);

        /// <summary>
        /// The Crowd's Wall boss (9,6): a face in the wall (12 hits) between two hands. The face can be hit for 3s of every 4s cycle; it then
        /// charges for 1s and sprays 8 bullets down over the top half of the room, and stays invincible until they are gone (1s between
        /// hits). The hands take turns at: stomps (the left one hunts us, waits 0.7s and drops; the right one bounces along the top), pushes
        /// (both drop fast to the middle: they only shove) and a laser (64px wide under the face, sweeping down, up, down and back, harmless
        /// to a player in the air). The left hand's stomps from phase 1 on crack, then hole, row 6. So: hit the face from right under it
        /// when it is open and nothing is coming, wait between rows 3 and 5 otherwise, dodge with the space-time planner and jump the laser.
        /// </summary>
        protected static IEnumerable<object> WallBossFight(int timeoutFrames = 12000)
        {
            BotRunner.BossDamageBudget = 0;
            var tl = new Vector2(Grid.X * 160, Grid.Y * 160);
            var attack = tl + new Vector2(75, 33);
            var waitAt = tl + new Vector2(75, 78);
            int lastSwing = -100;
            var lastShadow = new Dictionary<Entity, Vector2>();
            int chargeSeen = -1, laserSeen = -1, laserJump = -100;
            // It wakes when we are in the top half of the room; the fight starts once it has faded in.
            yield return GoTo(waitAt.X + BotNav.HalfW, tl.Y + 70 + BotNav.HalfH); // (it wakes once our top is 80px or less down the room)
            yield return WalkTo(waitAt.X, tl.Y + 70, tol: 1.5f);
            for (int i = 0; i < timeoutFrames; i++)
            {
                if (Talking || GlobalState.ScreenTransition)
                {
                    yield return AdvanceKeys(i);
                    continue;
                }
                var boss = BotRunner.Entities().FirstOrDefault(e => e.exists && e is AnodyneSharp.Entities.Enemy.Crowd.WallBoss);
                if (boss == null)
                {
                    yield break;
                }
                BotRunner.NotStuck(); // the fight has its own timeout
                var face = BotRunner.Entities().OfType<AnodyneSharp.Entities.Enemy.Crowd.Face>().FirstOrDefault();
                var pb = Player.Hitbox;
                var here = new Vector2(pb.X, pb.Y);
                if (face == null || face.opacity < 1f)
                {
                    BotRunner.Status = "Wall boss: wait for it to wake";
                    yield return K();
                    continue;
                }
                if (face.Health == 0)
                {
                    BotRunner.Status = "Wall boss: dying";
                    yield return K();
                    continue;
                }
                bool invincible = WallFaceInvincibleField?.GetValue(face) is bool b && b;
                float timeout = WallFaceTimeoutField?.GetValue(face) is float f ? f : 0f;
                var bullets = BotRunner.Entities().OfType<AnodyneSharp.Entities.Enemy.Crowd.Face.Bullet>().Where(e => e.exists && e.CurAnimName == "move").ToList();
                var laser = BotRunner.Entities().OfType<AnodyneSharp.Entities.Enemy.Crowd.Laser>().FirstOrDefault(e => e.exists);
                var hands = BotRunner.Entities().OfType<AnodyneSharp.Entities.Enemy.Crowd.Hand>().Where(e => e.exists).ToList();
                if (face.CurAnimName == "charge")
                {
                    if (chargeSeen < 0)
                    {
                        chargeSeen = i;
                    }
                }
                else
                {
                    chargeSeen = -1;
                }

                var threats = new List<Threat>();
                foreach (var bu in bullets)
                {
                    var th = new Threat { Name = "WallBoss bullet", Source = bu, Margin = 2 };
                    for (int k = 0; k <= Horizon; k++)
                    {
                        var d = bu.velocity * k / 60f;
                        th.At[k] = new Rectangle(bu.Hitbox.X + (int)d.X, bu.Hitbox.Y + (int)d.Y, bu.Hitbox.Width, bu.Hitbox.Height);
                    }
                    threats.Add(th);
                }
                if (chargeSeen >= 0)
                {
                    // The volley to come: 8 bullets from the face's centre, 30px/s down and up to 30px/s sideways, for up to 1.5s.
                    int spawnIn = Math.Max(0, 60 - (i - chargeSeen));
                    var c = face.Center;
                    var th = new Threat { Name = "WallBoss volley", Source = face, Margin = 2 };
                    for (int k = spawnIn; k <= Math.Min(Horizon, spawnIn + 90); k++)
                    {
                        float t = (k - spawnIn) / 60f;
                        th.At[k] = new Rectangle((int)(c.X - 30 * t), (int)(c.Y + 30 * t), (int)(60 * t) + 8, 8);
                    }
                    threats.Add(th);
                }
                bool rightStomping = false;
                foreach (var h in hands)
                {
                    var sh = h.shadow;
                    bool shadowOn = sh != null && sh.visible;
                    var shadowRect = sh?.Hitbox ?? h.Hitbox;
                    if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && i % 6 == 0)
                    {
                        System.Console.Error.WriteLine($"  wallboss f{i} {h.GetType().Name} pos={h.Position} off={h.offset} v={h.velocity} anim={h.CurAnimName} shadow={(sh == null ? "-" : sh.Hitbox + ":" + sh.visible)} me={pb}");
                    }
                    bool moved = lastShadow.TryGetValue(h, out var lp) && (lp - h.Position).Length() > 0.1f;
                    lastShadow[h] = h.Position;
                    if (shadowOn)
                    {
                        if (h is AnodyneSharp.Entities.Enemy.Crowd.RHand)
                        {
                            rightStomping = true;
                            var th = new Threat { Name = "WallBoss right stomp", Source = h, Margin = 3 };
                            for (int k = h.offset.Y > 12 ? 6 : 0; k <= Horizon; k++)
                            {
                                var r = shadowRect;
                                r.Offset((int)(h.velocity.X * k / 60f), 0);
                                th.At[k] = r;
                            }
                            threats.Add(th);
                        }
                        else if (!moved || h.offset.Y < 20)
                        {
                            // (while it still hunts us it moves with us: dodge only where it has settled)
                            // (it only hurts as it lands, and it is still high: from a few frames on, so a way out is always planned)
                            var th = new Threat { Name = "WallBoss left stomp", Source = h, Margin = 4, Stationary = true };
                            for (int k = h.offset.Y > 20 ? 12 : 0; k <= Horizon; k++)
                            {
                                th.At[k] = shadowRect;
                            }
                            threats.Add(th);
                        }
                    }
                    else if (h.velocity.Y > 0)
                    {
                        // A push: it drops until its top is 96px down the room (it only shoves, but into the holes on row 6/8).
                        var th = new Threat { Name = "WallBoss push", Source = h, Margin = 2 };
                        for (int k = 0; k <= Horizon; k++)
                        {
                            var d = h.velocity * k / 60f;
                            float y = Math.Min(h.Position.Y + d.Y, tl.Y + 96);
                            th.At[k] = new Rectangle((int)(h.Position.X + d.X), (int)y, h.Hitbox.Width, h.Hitbox.Height);
                        }
                        threats.Add(th);
                    }
                }

                // The laser: dodge it where we can; where it sweeps over us, jump it (in the air it can't hurt us).
                if (laser != null && laserSeen < 0)
                {
                    laserSeen = i;
                }
                else if (laser == null)
                {
                    laserSeen = -1;
                }
                var laserAt = laser == null ? null : WallLaserPath(laser, tl, Math.Max(0, laserSeen + 78 - i));
                if (laserAt != null)
                {
                    threats.Add(new Threat { Name = "WallBoss laser", Source = laser, Margin = 1, At = laserAt });
                    if (Player.state == PlayerState.GROUND && WallJumpLaserNow(laserAt, pb))
                    {
                        BotRunner.Status = "Wall boss: jump the laser";
                        laserJump = i;
                        yield return K(KeyFunctions.Cancel);
                        continue;
                    }
                    if (Player.state == PlayerState.AIR && i - laserJump < 40)
                    {
                        // Over it: against a laser coming down (it is past sooner), still against one going up (behind us are the holes).
                        // (and clear of the bullets, which hurt in the air too)
                        BotRunner.Status = "Wall boss: over the laser";
                        var prefer = laser.velocity.Y > 0 ? new[] { KeyFunctions.Up, (KeyFunctions)0, KeyFunctions.Left, KeyFunctions.Right }
                            : new[] { (KeyFunctions)0, KeyFunctions.Left, KeyFunctions.Right, KeyFunctions.Up };
                        var shots = threats.Where(t => t.Name.StartsWith("WallBoss bullet") || t.Name == "WallBoss volley").ToList();
                        KeyFunctions pick = prefer[0];
                        foreach (var kf in prefer)
                        {
                            var d = kf == (KeyFunctions)0 ? Vector2.Zero : DirVector(kf);
                            bool clear = true;
                            for (int k = 0; k <= 30 && clear; k++)
                            {
                                if (!SafeAtExactThreats(here + d * k, shots.Select(t => new Threat { At = new[] { k <= Horizon ? t.At[k] : null }, Margin = t.Margin }).ToList(), 0))
                                {
                                    clear = false;
                                }
                            }
                            if (clear)
                            {
                                pick = kf;
                                break;
                            }
                        }
                        yield return pick == (KeyFunctions)0 ? K() : K(pick);
                        continue;
                    }
                }

                bool open = !invincible && face.CurAnimName != "charge" && bullets.Count == 0 && laser == null && !rightStomping;
                var goal = open ? attack : waitAt;
                if (!SafeAtExactThreats(goal, threats, 45))
                {
                    // Somewhere else to stand for now: the nearest clear floor between the hands, above row 6.
                    var best = (Vector2?)null;
                    float bestCost = float.MaxValue;
                    for (int gx = 34; gx <= 116; gx += 6)
                    {
                        for (int gy = 34; gy <= 82; gy += 6)
                        {
                            var c = tl + new Vector2(gx, gy);
                            var r = new Rectangle((int)c.X, (int)c.Y, pb.Width, pb.Height);
                            if (!StandsSafe(r) || !SafeAtExactThreats(c, threats, 45))
                            {
                                continue;
                            }
                            float cost = (c - here).Length() + 0.5f * (c - goal).Length();
                            if (cost < bestCost)
                            {
                                bestCost = cost;
                                best = c;
                            }
                        }
                    }
                    if (best.HasValue)
                    {
                        goal = best.Value;
                    }
                }
                BotRunner.Status = $"Wall boss: face hp {face.Health}{(open ? " open" : "")}{(invincible ? " invincible" : "")} bullets {bullets.Count} {(laser != null ? "laser " : "")}{(rightStomping ? "stomp" : "")}";
                if (open && Math.Abs(pb.X - attack.X) <= 2 && Math.Abs(pb.Y - attack.Y) <= 2 && SafeAtExactThreats(here, threats, 20))
                {
                    if (Player.facing != Facing.UP)
                    {
                        yield return K(KeyFunctions.Up);
                        continue;
                    }
                    if (timeout <= 0f && i - lastSwing > 20 && !(Player.broom != null && Player.broom.exists))
                    {
                        lastSwing = i;
                        yield return K(KeyFunctions.Accept);
                        continue;
                    }
                    yield return K(KeyFunctions.Up);
                    continue;
                }
                var keys = SpaceTimeMove(goal, threats, useCache: false);
                bool survived = keys == null;
                if (keys == null)
                {
                    keys = SpaceTimeMove(here, threats, useCache: false, survive: true);
                }
                if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && i % 6 == 0)
                {
                    System.Console.Error.WriteLine($"  wallboss f{i} goal={goal - tl} here={here - tl} safeHere={SafeAtExactThreats(here, threats, 45)} survive={survived} keys={(keys == null ? "null" : string.Join("+", keys))} threats={string.Join(",", threats.Select(t => t.Name))}");
                }
                yield return keys ?? K();
            }
            throw new InvalidOperationException($"bot: Wall boss fight timed out at {Pos}");
        }

        /// <summary>
        /// The Wall boss's laser, frame by frame: it charges (harmless) for flickerLeft frames, then sweeps down at 50px/s to 107px into
        /// the room, up at 65px/s to 45px, down at 80px/s to 107px and back up to its start at 110px/s.
        /// </summary>
        private static Rectangle?[] WallLaserPath(Entity laser, Vector2 tl, int flickerLeft)
        {
            var at = new Rectangle?[Horizon + 1];
            float y = laser.Position.Y - tl.Y;
            float v = laser.velocity.Y;
            int phase = laser._flickering ? 0 : v > 0 && v < 60 ? 1 : v < 0 ? 2 : v > 60 ? 3 : 4;
            int wait = laser._flickering ? flickerLeft : 0;
            var hb = laser.Hitbox;
            for (int k = 0; k <= Horizon; k++)
            {
                if (phase == 0)
                {
                    if (wait-- <= 0)
                    {
                        phase = 1;
                    }
                    else
                    {
                        continue;
                    }
                }
                at[k] = new Rectangle(hb.X, (int)(tl.Y + y), hb.Width, hb.Height);
                switch (phase)
                {
                    case 1: y += 50 / 60f; if (y >= 107) { phase = 2; } break;
                    case 2: y -= 65 / 60f; if (y <= 45) { phase = 3; } break;
                    case 3: y += 80 / 60f; if (y >= 107) { phase = 4; } break;
                    default:
                        y = Math.Max(43, y - 110 / 60f);
                        if (y <= 43)
                        {
                            return at;
                        }
                        break;
                }
            }
            return at;
        }

        /// <summary>
        /// Jump now if the laser will be on us before a later jump could clear it: a jump keeps us in the air for about 29 frames, so it has
        /// to start just before the laser reaches us and land after it has gone past.
        /// </summary>
        private static bool WallJumpLaserNow(Rectangle?[] laserAt, Rectangle me)
        {
            const int Air = 28;
            bool Hits(int k) => k <= Horizon && laserAt[k] is Rectangle r && r.Intersects(me);
            int first = -1;
            for (int k = 0; k <= Horizon; k++)
            {
                if (Hits(k))
                {
                    first = k;
                    break;
                }
            }
            if (first < 0 || first > 6)
            {
                return false;
            }
            // The latest start j (taking off on frame j+1) with nothing on us at or before j and nothing on us once we land.
            for (int j = first - 1; j >= 0; j--)
            {
                bool landClear = true;
                for (int k = j + Air + 1; k <= j + Air + 8; k++)
                {
                    if (Hits(k))
                    {
                        landClear = false;
                        break;
                    }
                }
                if (landClear)
                {
                    return j == 0;
                }
            }
            return first <= 1; // no clean jump: in the air for as much of it as we can
        }

        /// <summary>True if standing still at p (top-left) stays clear of the threats for the next frames.</summary>
        private static bool SafeAtExactThreats(Vector2 p, List<Threat> threats, int frames)
        {
            var me = new Rectangle((int)p.X, (int)p.Y, Player.Hitbox.Width, Player.Hitbox.Height);
            foreach (var t in threats)
            {
                for (int k = 0; k <= Math.Min(frames, Horizon); k++)
                {
                    if (t.At[k] is Rectangle r)
                    {
                        r.Inflate(t.Margin, t.Margin);
                        if (r.Intersects(me))
                        {
                            return false;
                        }
                    }
                }
            }
            return true;
        }

        private static readonly System.Reflection.BindingFlags Priv = System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance;
        private static readonly System.Reflection.FieldInfo SplitStateField = typeof(AnodyneSharp.Entities.Enemy.Apartment.SplitBoss).GetField("state", Priv);
        private static readonly System.Reflection.FieldInfo SplitHealthField = typeof(AnodyneSharp.Entities.Enemy.Apartment.SplitBoss).GetField("health", Priv);
        private static readonly System.Reflection.FieldInfo SplitVulnField = typeof(AnodyneSharp.Entities.Enemy.Apartment.SplitBoss).GetField("vulnerable", Priv);
        private static readonly System.Reflection.FieldInfo SplitInvulnField = typeof(AnodyneSharp.Entities.Enemy.Apartment.SplitBoss).GetField("invuln_time", Priv);
        private static readonly System.Reflection.FieldInfo SplitBulletActiveField = typeof(AnodyneSharp.Entities.Enemy.Apartment.SplitBoss.Bullet).GetField("active", Priv);

        /// <summary>
        /// The Apartment's Watcher (SplitBoss, 13 hits, 0.8s apart). Hit it to start; it then takes turns at:
        ///   Shoot: it floats over the holes above the arena (vulnerable: reachable from the arena's top edge by a broom swung up) while fire
        ///     balls appear in a row just above the arena (they hurt from the moment they appear), sit there for 0.2-0.8s and fall at 40px/s;
        ///   Split: it floats lower, harmless, until hit; then two copies appear 25px either side of us (stand still: they fly outward) while it
        ///     follows 24px above us, and at last it drops down through where we are (vulnerable): step aside and hit it as it passes;
        ///   Dash: it lines up with us left of the arena and dashes right (80-120px/s) dropping fire balls, which then fly at us.
        /// Everything that moves is predicted for the space-time planner (a ball that sits is taken as falling any time from now on, the
        /// Split drop as starting now); a ball that will hit us anyway is jumped (they skip a player in the air; the Watcher does not).
        /// </summary>
        protected static IEnumerable<object> WatcherFight(int timeoutFrames = 30000)
        {
            BotRunner.BossDamageBudget = 3; // TODO: the fire balls it drops on a dash (aimed at us all at once) still land 0-2 times a fight
            var room = new Vector2(Grid.X * 160, Grid.Y * 160);
            var tl = room + new Vector2(32, 48); // the arena's top-left (6x5 tiles of floor)
            int lastSwing = -100, jumpAt = -100;
            for (int i = 0; i < timeoutFrames; i++)
            {
                if (Talking || GlobalState.ScreenTransition)
                {
                    yield return AdvanceKeys(i);
                    continue;
                }
                var boss = BotRunner.Entities().OfType<AnodyneSharp.Entities.Enemy.Apartment.SplitBoss>().FirstOrDefault(e => e.exists);
                if (boss == null)
                {
                    yield break;
                }
                BotRunner.NotStuck(); // the fight has its own timeout
                string state = SplitStateField.GetValue(boss)?.GetType().Name ?? "";
                state = state.Length > 1 && state[0] == '<' ? state.Substring(1, state.IndexOf('>') - 1) : state;
                int hp = (int)SplitHealthField.GetValue(boss);
                bool vulnerable = (bool)SplitVulnField.GetValue(boss);
                float invuln = (float)SplitInvulnField.GetValue(boss);
                var pb = Player.Hitbox;
                var here = new Vector2(pb.X, pb.Y);
                var bb = boss.Hitbox;
                if (state == "Die")
                {
                    BotRunner.Status = "Watcher: dying";
                    yield return K();
                    continue;
                }

                var threats = new List<Threat>();
                foreach (var bu in boss.SubEntities().OfType<AnodyneSharp.Entities.Enemy.Apartment.SplitBoss.Bullet>().Where(e => e.exists && e.opacity > 0))
                {
                    if (!(SplitBulletActiveField.GetValue(bu) is bool on && on))
                    {
                        continue; // a dropped ball before it is let go
                    }
                    var th = new Threat { Name = "Watcher fire ball", Source = bu, Margin = 2 };
                    var h = bu.Hitbox;
                    for (int k = 0; k <= Horizon; k++)
                    {
                        if (bu.velocity == Vector2.Zero)
                        {
                            th.At[k] = new Rectangle(h.X, h.Y, h.Width, h.Height + (int)Math.Ceiling(40f * k / 60f)); // sits, then falls some time
                        }
                        else
                        {
                            var d = bu.velocity * k / 60f;
                            th.At[k] = new Rectangle(h.X + (int)d.X, h.Y + (int)d.Y, h.Width, h.Height);
                        }
                    }
                    threats.Add(th);
                }
                var copyList = boss.SubEntities().OfType<AnodyneSharp.Entities.Enemy.Apartment.Copy>().Where(c => c.exists).ToList();
                bool copies = copyList.Count > 0;
                var bodies = new List<AnodyneSharp.Entities.Enemy.Apartment.BaseSplitBoss> { boss };
                bodies.AddRange(copyList);
                foreach (var b in bodies)
                {
                    bool splitDrop = b == boss && state == "Split" && boss.damage_player && !copies;
                    bool copyWarn = b != boss && state == "Split" && (b._flickering || b.velocity != Vector2.Zero);
                    if (b == boss && state == "Split" && copies)
                    {
                        continue; // it keeps 24px above us while its copies are out
                    }
                    if (!copyWarn && (!b.damage_player || b.opacity <= 0 || (!b.visible && !splitDrop)))
                    {
                        continue;
                    }
                    var th = new Threat { Name = b == boss ? "Watcher" : "Watcher copy", Source = b, Margin = splitDrop ? 1 : 3, HurtsInAir = true };
                    var h = b.Hitbox;
                    // (a copy flies at the other one, through where we were when they stopped following us: 40-60px/s)
                    var v = copyWarn && b.velocity == Vector2.Zero ? new Vector2(Math.Sign(pb.Center.X - h.Center.X) * 60f, 0) : b.velocity;
                    for (int k = 0; k <= Horizon; k++)
                    {
                        if (splitDrop)
                        {
                            th.At[k] = new Rectangle(h.X, h.Y, h.Width, h.Height + (int)Math.Ceiling(60f * k / 60f)); // its drop, any time now
                        }
                        else
                        {
                            var d = v * k / 60f;
                            th.At[k] = new Rectangle(h.X + (int)d.X, h.Y + (int)d.Y, h.Width, h.Height);
                        }
                    }
                    threats.Add(th);
                }

                // Where to be: under it to strike (it floats above the arena's top edge), beside its drop in a Split, above or below its
                // dash, else in the middle (room to dodge both ways).
                Vector2 goal = tl + new Vector2(43, 34);
                Facing face = Facing.UP;
                bool strike = false;
                bool overHoles = false;
                bool swingNow = true;
                bool near = false; // the last few pixels are walked straight (the planner keeps off the holes' edge we strike from)
                if (state == "Intro" && !boss.damage_player && boss.visible && boss.opacity >= 1f)
                {
                    goal = new Vector2(bb.Center.X - pb.Width / 2, bb.Bottom + 2); // wake it
                    strike = true;
                }
                else if (state == "Split" && !boss.damage_player)
                {
                    goal = new Vector2(bb.Center.X - pb.Width / 2, bb.Bottom + 5); // it waits to be hit (and hurts from then on: not from inside it)
                    strike = pb.Top >= bb.Bottom + 2 || Player.facing == Facing.UP && pb.Top >= bb.Bottom; // (turning up steps us 1px up)
                    near = true;
                }
                else if (state == "Split" && copies && !copyList.Any(c => c._flickering || c.velocity != Vector2.Zero))
                {
                    BotRunner.Status = "Watcher: split, the copies follow us: stand still";
                    yield return K();
                    continue;
                }
                else if (state == "Split" && copies)
                {
                    // They are about to fly at each other through our row: down (or up) out of it (they are 20px tall at our top), straight away.
                    var cy = copyList[0].Hitbox.Y;
                    var down = new Vector2(here.X, cy + 20 + 4);
                    goal = down.Y + pb.Height <= tl.Y + 80 ? down : new Vector2(here.X, Math.Max(tl.Y + 1, cy - pb.Height - 4));
                    BotRunner.Status = $"Watcher: split, out of the copies' row to {goal - room}";
                    yield return K(Math.Abs(goal.Y - here.Y) <= 1 ? new KeyFunctions[0] : new[] { goal.Y > here.Y ? KeyFunctions.Down : KeyFunctions.Up });
                    continue;
                }
                else if (state == "Split")
                {
                    // Beside the column it drops down, level with it as it passes.
                    float rowY = bb.Bottom > pb.Top + 2 && bb.Top < pb.Bottom - 2 ? here.Y : Math.Max(tl.Y + 1, bb.Top + 8); // (it moves: not after it)
                    var left = new Vector2(bb.X - pb.Width - 5, rowY);
                    var right = new Vector2(bb.Right + 5, rowY);
                    bool leftOk = StandsSafe(new Rectangle((int)left.X, (int)left.Y, pb.Width, pb.Height)) && left.X >= tl.X;
                    bool rightOk = StandsSafe(new Rectangle((int)right.X, (int)right.Y, pb.Width, pb.Height)) && right.X + pb.Width <= tl.X + 96;
                    bool goLeft = leftOk && (!rightOk || Math.Abs(left.X - here.X) <= Math.Abs(right.X - here.X));
                    goal = goLeft ? left : right;
                    face = goLeft ? Facing.RIGHT : Facing.LEFT;
                    strike = true; // (turned towards it ahead of time)
                    swingNow = vulnerable && bb.Bottom + boss.velocity.Y * 3 / 60f > pb.Top - 2 && bb.Top < pb.Bottom + 2;
                    near = true;
                }
                else if (state == "Dash")
                {
                    // It locks onto our row, then dashes right through it: be just above (or below) the row then and sweep it as it goes by.
                    bool dashing = boss.CurAnimName == "dash_r" && boss.velocity.X > 0;
                    if (dashing && bb.Bottom > pb.Top - 6 && bb.Top < pb.Bottom + 6 || dashing && Math.Abs(here.Y - goal.Y) > 2 && bb.Right < pb.Left)
                    {
                        var above = new Vector2(here.X, bb.Top - pb.Height - 5);
                        var below = new Vector2(here.X, bb.Bottom + 5);
                        bool aboveOk = above.Y >= tl.Y + 1 && StandsSafe(new Rectangle((int)above.X, (int)above.Y, pb.Width, pb.Height));
                        goal = aboveOk ? above : below;
                        face = aboveOk ? Facing.DOWN : Facing.UP;
                        strike = true; // (turned towards it ahead of time: the broom sweeps 11px either side of us, 2-3 frames after the press)
                        swingNow = vulnerable && bb.Right + boss.velocity.X * 3 / 60f >= pb.Left - 11 && bb.Left <= pb.Right + 3;
                        near = true;
                    }
                    else if (!dashing)
                    {
                        goal = new Vector2(here.X, Math.Clamp(here.Y, tl.Y + 20, tl.Y + 50)); // it follows our row: keep room above and below
                        var sitting = boss.SubEntities().OfType<AnodyneSharp.Entities.Enemy.Apartment.SplitBoss.Bullet>()
                            .Where(e => e.exists && e.opacity > 0 && !(SplitBulletActiveField.GetValue(e) is bool on && on)).Select(e => e.Hitbox.Center.ToVector2()).ToList();
                        if (sitting.Count > 0 && !boss.visible || sitting.Count > 0 && bb.Right < tl.X)
                        {
                            // The dropped balls will fly at us all at once: the farther from them, the longer to sidestep each.
                            float bestScore = float.MinValue;
                            for (int gx = 2; gx <= 84; gx += 6)
                            {
                                for (int gy = 1; gy <= 66; gy += 5)
                                {
                                    var c = tl + new Vector2(gx, gy);
                                    if (!StandsSafe(new Rectangle((int)c.X, (int)c.Y, pb.Width, pb.Height)))
                                    {
                                        continue;
                                    }
                                    var cc = c + new Vector2(pb.Width / 2f, pb.Height / 2f);
                                    float score = sitting.Min(q => (q - cc).Length()) - 0.3f * (c - here).Length();
                                    if (score > bestScore)
                                    {
                                        bestScore = score;
                                        goal = c;
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        goal = here;
                    }
                }
                else if (vulnerable && bb.Bottom <= tl.Y)
                {
                    // Over the holes (Shoot): from the arena's top edge, under it, between the rows of fire balls if it can be.
                    Vector2? best = null;
                    float bestCost = float.MaxValue;
                    for (int x = bb.X - pb.Width + 4; x <= bb.Right - 4; x++)
                    {
                        var c = new Vector2(x, tl.Y + 1);
                        if (!StandsSafe(new Rectangle(x, (int)c.Y, pb.Width, pb.Height)) || !SafeAtExactThreats(c, threats, 30))
                        {
                            continue;
                        }
                        float cost = Math.Abs(c.X - here.X) + Math.Abs(c.X + pb.Width / 2 - bb.Center.X) * 0.3f;
                        if (cost < bestCost)
                        {
                            bestCost = cost;
                            best = c;
                        }
                    }
                    if (best.HasValue)
                    {
                        goal = best.Value;
                        strike = near = overHoles = true;
                    }
                    else
                    {
                        goal = new Vector2(bb.Center.X - pb.Width / 2, tl.Y + 26); // below the fire for now
                    }
                }
                // Solid things in the arena (the health cicada's sentinel sits in it): stand beside them, not into them.
                var solids = BotRunner.Entities().Where(e => e.exists && e.Solid && e.GetType().Namespace != "AnodyneSharp.Entities" && !(e is AnodyneSharp.Entities.Enemy.Apartment.SplitBoss.Bullet)
                    && e.Hitbox.Intersects(new Rectangle((int)tl.X - 16, (int)tl.Y - 16, 128, 112))).Select(e => e.Hitbox).ToList();
                bool Blocked(Vector2 p) => solids.Any(r => r.Intersects(new Rectangle((int)p.X - 2, (int)p.Y - 2, pb.Width + 4, pb.Height + 4))); // (2px clear: walking pressed against it sticks)
                if (Blocked(goal))
                {
                    for (int dx = 1; dx <= 24; dx++)
                    {
                        if (!Blocked(goal + new Vector2(dx, 0)))
                        {
                            goal.X += dx;
                            break;
                        }
                        if (!Blocked(goal - new Vector2(dx, 0)))
                        {
                            goal.X -= dx;
                            break;
                        }
                    }
                }
                bool atGoal = Math.Abs(here.X - goal.X) <= 2.5f && Math.Abs(here.Y - goal.Y) <= 2.5f;
                BotRunner.Status = $"Watcher: {state} hp {hp}{(vulnerable ? " vulnerable" : "")}{(invuln > 0 ? " invuln" : "")} threats {threats.Count} goal {goal - room}{(atGoal ? " (there)" : "")}";
                if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && i % 6 == 0)
                {
                    System.Console.Error.WriteLine($"  watcher f{i}/{BotRunner.Frame} {BotRunner.Status} boss={bb} {boss.CurAnimName} me={pb} face={Player.facing} threats={string.Join(",", threats.Select(t => t.Name + t.At[0]))}");
                }
                if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && Player.broom != null && Player.broom.exists)
                {
                    System.Console.Error.WriteLine($"  watcher broom f{i} {Player.broom.Hitbox} boss {bb} vis={boss.visible} {state}");
                }
                // (from under the holes the broom just reaches it: from the arena's top pixel row only)
                bool Reach(Vector2 p) => Math.Abs(p.X - goal.X) <= 4f && Math.Abs(p.Y - goal.Y) <= 4f && (!overHoles || p.Y <= tl.Y + 1.5f);
                bool inReach = Reach(here);
                if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && state == "Split" && !boss.damage_player)
                {
                    System.Console.Error.WriteLine($"  watcher pre f{i} here={here} goal={goal} strike={strike} reach={inReach} at={atGoal} face={Player.facing} v={Player.velocity} inv={invuln} since={i - lastSwing} broom={Player.broom?.exists} safe={SafeAtExactThreats(here, threats, 12)}");
                }
                if (strike && inReach && SafeAtExactThreats(here, threats, 12))
                {
                    if (Player.facing != face)
                    {
                        // Turning is a step that way (~2px): if that step leaves the spot, step back the other way first.
                        var fk = face switch { Facing.UP => KeyFunctions.Up, Facing.LEFT => KeyFunctions.Left, Facing.RIGHT => KeyFunctions.Right, _ => KeyFunctions.Down };
                        var back = face switch { Facing.UP => KeyFunctions.Down, Facing.LEFT => KeyFunctions.Right, Facing.RIGHT => KeyFunctions.Left, _ => KeyFunctions.Up };
                        yield return K(Reach(here + DirVector(fk) * 2.5f) ? fk : back);
                        continue;
                    }
                    if (swingNow && invuln <= 0f && i - lastSwing > 16 && !(Player.broom != null && Player.broom.exists))
                    {
                        lastSwing = i;
                        yield return K(KeyFunctions.Accept);
                        continue;
                    }
                    yield return K();
                    continue;
                }
                if (atGoal && SafeAtExactThreats(here, threats, 20))
                {
                    yield return K();
                    continue;
                }
                KeyFunctions[] Straight()
                {
                    var dk = new List<KeyFunctions>();
                    float dx = goal.X - here.X, dy = goal.Y - here.Y;
                    if (dx > 0.5f) { dk.Add(KeyFunctions.Right); }
                    else if (dx < -0.5f) { dk.Add(KeyFunctions.Left); }
                    if (dy > 1) { dk.Add(KeyFunctions.Down); }
                    else if (dy < -1) { dk.Add(KeyFunctions.Up); }
                    return dk.ToArray();
                }
                if (threats.Count == 0 || near && (goal - here).Length() < 26 && SafeAtExactThreats(goal, threats, 30) && SafeAtExactThreats(here, threats, 10))
                {
                    // Nothing to dodge (or we stand in the doorway, which the planner does not walk), or a short safe step: straight there.
                    yield return K(Straight());
                    continue;
                }
                var keys = SpaceTimeMove(goal, threats, useCache: false);
                string how = "plan";
                if (keys == null)
                {
                    keys = SpaceTimeMove(here, threats, useCache: false, survive: true);
                    how = "survive";
                }
                if (keys == null && Player.state == PlayerState.GROUND && i - jumpAt > 40 && !threats.Any(t => t.HurtsInAir && SafeAtExactThreats(here, new List<Threat> { t }, 30) == false))
                {
                    // Nowhere to go and only fire balls on the way: over them.
                    jumpAt = i;
                    how = "jump";
                    keys = new[] { KeyFunctions.Cancel };
                }
                if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                {
                    System.Console.Error.WriteLine($"  watcher move f{i} {how} {(keys == null ? "none" : string.Join("+", keys))} me={pb}");
                }
                yield return keys ?? K();
            }
            throw new InvalidOperationException($"bot: Watcher fight timed out at {Pos}");
        }

        protected static IEnumerable<object> SeerFight(int timeoutFrames = 9000)
        {
            var healthField = typeof(AnodyneSharp.Entities.Enemy.Bedroom.Seer).GetField("_health", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            int seerWait = int.Parse(Environment.GetEnvironmentVariable("SEER_WAIT") ?? "0"); // dev: shifts the boss fight's timing (a stand-in for a pseudo-random seed)
            BotRunner.BossDamageBudget = 0; // the strikes are timed to the boss's pauses (wave charge, settling after the wave): the fight costs no hearts
            _seerPrev.Clear();
            _seerVel.Clear();
            _seerOmega.Clear();
            _seerPrevRel.Clear();
            int lastSwing = -100;
            bool seerStarted = false;
            int awake = 0;
            int seerPlanI = -100;
            KeyFunctions[] seerCachedKeys = null;
            bool seerCachedWasVulnerable = false;
            int seerTraceA = int.Parse(Environment.GetEnvironmentVariable("SEER_TRACE_A") ?? "-1"), seerTraceB = int.Parse(Environment.GetEnvironmentVariable("SEER_TRACE_B") ?? "-2");
            int seerLastHearts = GlobalState.CUR_HEALTH;
            string seerLastKeys = "";
            Vector2 seerStart0 = BotRunner.Entities().FirstOrDefault(e => e.exists && e.GetType().Name == "Seer")?.Position ?? Vector2.Zero;
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
                    BotRunner.BossDamageBudget = 0;
                    yield break;
                }
                BotRunner.NotStuck(); // the fight has its own timeout; the boss's waves and dives keep us still for long stretches
                int hp = healthField != null ? (int)healthField.GetValue(seer) : -1;
                BotRunner.Status = $"Seer fight: boss hp {hp}";
                if (GlobalState.CUR_HEALTH < seerLastHearts && Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                {
                    System.Console.Error.WriteLine($"  SEER HURT f{i} hearts {seerLastHearts}->{GlobalState.CUR_HEALTH} bosshp={hp} P={Player.Hitbox} face={Player.facing} keysLast={seerLastKeys} boss={seer.Hitbox} v={seer.velocity} orbs={string.Join(",", BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "SeerOrb").Select(e => e.Hitbox.ToString()))} wave={string.Join(",", BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "SeerWave").Select(e => e.Hitbox.ToString()))} dusts={string.Join(",", BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Dust").Select(e => e.Hitbox.ToString()))}");
                }
                seerLastHearts = GlobalState.CUR_HEALTH;
                var sb = seer.Hitbox;
                var pb = Player.Hitbox;
                bool vulnerable = !seer._flickering && seer.opacity >= 1f;
                if (!seerStarted && (seer.velocity != Vector2.Zero || seer.Position != seerStart0))
                {
                    seerStarted = true;
                }
                if (!seerStarted && Player.Position.Y > seer.Position.Y + 46)
                {
                    // The boss sleeps until the player is within 48px below it (its intro dialogue starts then).
                    yield return K(KeyFunctions.Up);
                    continue;
                }
                if (seer.opacity < 1f)
                {
                    // Not in the fight yet: walk up until it notices us (it starts once we are close), then let the dialogue run.
                    yield return Player.Position.Y > seer.Position.Y + 44 ? K(KeyFunctions.Up) : K();
                    continue;
                }
                if (awake++ < seerWait)
                {
                    yield return K(); // dev: idle this many frames once the intro is over (a stand-in for a pseudo-random seed of the fight)
                    continue;
                }
                bool stage2 = hp <= 6;
                var threats = SeerThreats(seer, stage2);
                var waveE = BotRunner.Entities().FirstOrDefault(e => e.exists && e.GetType().Name == "SeerWave");
                // Stage 2 plan: it dives through the whole room, so never strike while it moves. After its wave it drifts back to its post
                // (nearly still, high up): that is when to hit it, from just under its orbs, then step out of the column before it dives.
                var seerSpeed = _seerVel.TryGetValue(seer, out var seerV0) ? seerV0.Length() : 9f;
                bool settle = stage2 && waveE == null && seerSpeed < 0.45f && seer.Hitbox.Center.Y <= 66;
                if (settle)
                {
                    foreach (var th in threats) { th.Margin = 1; } // it is not going anywhere: no buffer needed for prediction error
                }
                // The wave appears on the boss and hangs there still (and flickering) while the orbs are drawn out to the sides: the boss itself
                // is then a stationary target up top with nothing under it. Hit it from just below before the wave starts to fall.
                bool charge = stage2 && waveE != null && waveE.velocity == Vector2.Zero;
                if (charge)
                {
                    threats = SeerThreats(seer, stage2, charge: true);
                    foreach (var th in threats) { th.Margin = 2; }
                }
                bool wantStrike = vulnerable && (!stage2 || settle || charge);
                // Where the broom would meet the boss if we swung now facing the way we face (the swing takes ~8 frames to reach full length).
                var seerAt8 = threats.First(t => t.Source == seer).At[8] ?? seer.Hitbox;
                Rectangle BroomRect(Facing f)
                {
                    return f switch
                    {
                        Facing.UP => new Rectangle(pb.X - 5, pb.Y - 17, 24, 12),
                        Facing.DOWN => new Rectangle(pb.X - 11, pb.Bottom + 4, 24, 12),
                        Facing.LEFT => new Rectangle(pb.X - 13, pb.Y - 5, 12, 24),
                        _ => new Rectangle(pb.Right + 3, pb.Y - 5, 12, 24)
                    };
                }
                // A swing roots us for ~22 frames: only start one if standing here is safe for that long.
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
                if (wantStrike && BroomRect(Player.facing).Intersects(seerAt8) && StandingSafe(24) && i - lastSwing > 26)
                {
                    lastSwing = i;
                    if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                    {
                        var st = threats.First(t => t.Source == seer);
                        System.Console.Error.WriteLine($"  seer SWING f{i} P={pb} face={Player.facing} S={sb} v={(_seerVel.TryGetValue(seer, out var vv0) ? vv0 : Vector2.Zero)} pred@8={st.At[8]} @16={st.At[16]} @24={st.At[24]} limits={SeerPresetField?.GetValue(seer)}");
                    }
                    yield return K(KeyFunctions.Accept);
                    continue;
                }
                if (i - seerPlanI < 3 && seerCachedKeys != null && seerCachedWasVulnerable == vulnerable)
                {
                    // Planning is the expensive part (a handful of space-time searches): redo it every third frame and keep walking the plan in between.
                    yield return seerCachedKeys.Length == 0 ? K() : K(seerCachedKeys);
                    continue;
                }
                // Choose where to be: beside/below the boss at striking distance while it is vulnerable, else the safest spot we can find.
                Vector2 goal;
                Facing seerFace = Facing.UP;
                if (wantStrike)
                {
                    // Strike from whichever side we can reach safely soonest: below (facing up), left or right of it, or above.
                    var cands = new[]
                    {
                        (new Vector2(seerAt8.Center.X - 5, seerAt8.Bottom + (charge ? 5 : stage2 ? 14 : 7)), Facing.UP), // stage 2: just clear of the orbs, which hang 12px under the body
                        (new Vector2(seerAt8.Right + 7, seerAt8.Center.Y - 6), Facing.LEFT),
                        (new Vector2(seerAt8.Left - 17, seerAt8.Center.Y - 6), Facing.RIGHT),
                        (new Vector2(seerAt8.Center.X - 5, seerAt8.Top - 19), Facing.DOWN),
                    }.Where(c => !stage2 || c.Item2 == Facing.UP) // its orbs whirl out to the right of and below the body: only its left side and top are safe to strike from
                     .OrderBy(c => (c.Item1 - new Vector2(pb.X, pb.Y)).LengthSquared()).ToList();
                    goal = new Vector2(pb.X, pb.Y); // no strike spot can be reached safely right now: keep clear and wait for one
                    seerFace = cands[0].Item2;
                    foreach (var cand in cands)
                    {
                        var k = SpaceTimeMove(cand.Item1, threats, useCache: false);
                        if (k != null && SpaceTime.Reached)
                        {
                            goal = cand.Item1;
                            seerFace = cand.Item2;
                            break;
                        }
                    }
                }
                else
                {
                    goal = new Vector2(pb.X, pb.Y);
                }
                if (waveE != null && !charge && waveE.Hitbox.Bottom < pb.Top + 60 && BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "Dust"))
                {
                    // The wave sweeps down the whole floor-plan width and dies on the first dust it meets: the only safe place is below the dust line.
                    goal = new Vector2(Math.Clamp(pb.X, 338, 450), 131);
                    BotRunner.Status = "Seer fight: ducking the wave";
                }
                var keys = SpaceTimeMove(goal, threats, useCache: false);
                if (wantStrike && keys != null && keys.Length == 0 && Player.facing != seerFace && (new Vector2(pb.X, pb.Y) - goal).Length() < 6)
                {
                    // arrived at the striking spot: turn to face it
                    keys = new[] { seerFace == Facing.UP ? KeyFunctions.Up : seerFace == Facing.DOWN ? KeyFunctions.Down : seerFace == Facing.LEFT ? KeyFunctions.Left : KeyFunctions.Right };
                }
                if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && (i % 10 == 0 || (i >= seerTraceA && i <= seerTraceB)))
                {
                    System.Console.Error.WriteLine($"  seer f{i} hp={hp} vuln={vulnerable} settle={settle} spd={seerSpeed:0.00} wave={(waveE != null ? 1 : 0)} P={pb} S={sb} goal={goal} keys=[{(keys == null ? "none" : string.Join("+", keys))}]");
                }
                if ((keys == null || keys.Length == 0) && !StandingSafe(12) && !(waveE != null && pb.Y >= 129))
                {
                    // Nothing safe was found and the boss is about to reach us: standing still is the one sure way to be hit. Run for the
                    // direction that keeps us clear of the predicted boss/orb boxes the longest, and furthest from them.
                    KeyFunctions[][] opts =
                    {
                        new[] { KeyFunctions.Up }, new[] { KeyFunctions.Down }, new[] { KeyFunctions.Left }, new[] { KeyFunctions.Right },
                        new[] { KeyFunctions.Up, KeyFunctions.Left }, new[] { KeyFunctions.Up, KeyFunctions.Right },
                        new[] { KeyFunctions.Down, KeyFunctions.Left }, new[] { KeyFunctions.Down, KeyFunctions.Right },
                    };
                    float bestScore = float.MinValue;
                    KeyFunctions[] bestOpt = null;
                    foreach (var opt in opts)
                    {
                        var mv = Vector2.Zero;
                        foreach (var k in opt) { mv += DirVector(k); }
                        mv *= WalkPxPerFrame * (opt.Length > 1 ? 0.7071f : 1f);
                        float score = 0;
                        var pos = new Vector2(pb.X, pb.Y);
                        bool ok = true;
                        for (int f = 1; f <= 36 && ok; f++)
                        {
                            pos += mv;
                            var mine = new Rectangle((int)pos.X, (int)pos.Y, pb.Width, pb.Height);
                            if (!CornersWalkable(mine)) { ok = false; break; }
                            foreach (var th in threats)
                            {
                                var r = th.At[Math.Min(f, Horizon)];
                                if (r == null) { continue; }
                                var rr = r.Value;
                                rr.Inflate(th.Margin, th.Margin);
                                if (mine.Intersects(rr)) { score -= 100 + (37 - f) * 6; }
                                else
                                {
                                    float dx = Math.Max(0, Math.Max(rr.Left - mine.Right, mine.Left - rr.Right));
                                    float dy = Math.Max(0, Math.Max(rr.Top - mine.Bottom, mine.Top - rr.Bottom));
                                    score += Math.Min(30f, MathF.Sqrt(dx * dx + dy * dy)) * 0.2f;
                                }
                            }
                        }
                        if (ok && score > bestScore) { bestScore = score; bestOpt = opt; }
                    }
                    if (bestOpt != null) { keys = bestOpt; }
                }
                seerPlanI = i;
                seerCachedKeys = keys;
                seerCachedWasVulnerable = vulnerable;
                seerLastKeys = keys == null ? "none" : string.Join("+", keys);
                yield return keys ?? K();
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
            Swinging = Player.broom != null && Player.broom.exists,
            T = target.Hitbox,
            TV = target.velocity,
            Fast = target is AnodyneSharp.Entities.Enemy.Slime && target.SubEntities().OfType<AnodyneSharp.Entities.Enemy.Slime.Bullet>().Any(),
            Walk = CornersWalkable,
            Hazard = LaneHazard(),
            Others = Enemies().Where(e => e != target).Select(e => e.Hitbox).ToList()
        };

        /// <summary>A player-sized rectangle that touches a live beam lane (or an emitter's own tile).</summary>
        private static Func<Rectangle, bool> LaneHazard()
        {
            var lanes = BotNav.LaneTiles();
            if (lanes.Count == 0)
            {
                return _ => false;
            }
            return r =>
            {
                for (int ty = r.Top / 16; ty <= (r.Bottom - 1) / 16; ty++)
                {
                    for (int tx = r.Left / 16; tx <= (r.Right - 1) / 16; tx++)
                    {
                        if (lanes.Contains(new Point(tx, ty)))
                        {
                            return true;
                        }
                    }
                }
                return false;
            };
        }

        /// <summary>A policy's keys as a frame: a plain step goes through the danger-aware SafeMove, anything else (swings, standing) is sent as is.</summary>
        private static KeyFunctions[] PolicyFrame(PKey[] keys, Entity target)
        {
            if (keys.Length == 0)
            {
                return K();
            }
            KeyFunctions Map(PKey k) => k switch { PKey.Up => KeyFunctions.Up, PKey.Down => KeyFunctions.Down, PKey.Left => KeyFunctions.Left, PKey.Right => KeyFunctions.Right, _ => KeyFunctions.Accept };
            if (DodgeEnabled && keys.Length == 1 && keys[0] != PKey.Accept && BotNav.LaneTiles().Count > 0)
            {
                // beams about: a plain step must time its crossing of them (the policies know the enemy, not the beams)
                return SafeMove(Map(keys[0]), target, 12f);
            }
            return K(keys.Select(Map).ToArray()); // the policies do their own avoiding: SafeMove would second-guess a retreat and stand still
        }

        /// <summary>The walkable tiles connected to `start` (within `limit` tiles), ignoring enemies.</summary>
        private static HashSet<Point> FloodRegion(Point start, int limit)
        {
            var g = new Point(start.X / 10, start.Y / 10); // stay on this screen: the question is what can be reached without leaving it
            return FloodRegion(start, limit, g);
        }

        private static HashSet<Point> FloodRegion(Point start, int limit, Point screen)
        {
            var seen = new HashSet<Point> { start };
            var q = new Queue<Point>();
            q.Enqueue(start);
            while (q.Count > 0 && seen.Count < limit)
            {
                var t = q.Dequeue();
                foreach (var o in new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) })
                {
                    var n = new Point(t.X + o.X, t.Y + o.Y);
                    if (!seen.Contains(n) && n.X / 10 == screen.X && n.Y / 10 == screen.Y && n.X >= 0 && n.Y >= 0 && BotNav.Walkable(n))
                    {
                        seen.Add(n);
                        q.Enqueue(n);
                    }
                }
            }
            return seen;
        }

        /// <summary>
        /// Keys to stand off a still target ~5px short of its box, line up with it, face it and swing (gap up to 11px: the broom reaches ~12
        /// beyond us). `swingWait` counts frames since the last swing; `cooldown` is the frames between swings.
        /// </summary>
        /// <summary>
        /// One frame of a fight with a Crowd frog. Its three bullets are lobbed at our centre (15/30/45 px/s, 0.8-1.8s flights) and only
        /// hurt while low: just after leaving its top-left corner (up to 12px out) and where they come down (see PredictFrogBullet). It fires
        /// whenever we are within 64px and its last volley is gone (gone = landed and splashed, 18 frames). So: strike from a spot 4-9px
        /// off one of its sides (outside the reach of a fresh volley where the floor allows, inside the broom's), picking a side no landing
        /// bullet is about to cover, and walk between spots round the predicted landings (the space-time planner, with the next volley as
        /// a danger round the frog from the frame it will fire). A spot across a pit is jumped to and back (FrogJumpKeys).
        /// </summary>
        private static string _frogLastStatus;

        private static KeyFunctions[] FrogStrikeKeys(Entity frog, ref int swingWait)
        {
            var keys = FrogStrikeKeysCore(frog, ref swingWait);
            if (Environment.GetEnvironmentVariable("ANODYNE_KEYTRACE") != null)
            {
                System.Console.Error.WriteLine($"  frogkeys f{BotRunner.Frame} [{string.Join("+", keys)}] {BotRunner.Status}");
            }
            if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && BotRunner.Status != _frogLastStatus)
            {
                _frogLastStatus = BotRunner.Status;
                var hf = frog.GetType().GetField("_health", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
                System.Console.Error.WriteLine($"  frogstat f{BotRunner.Frame} {BotRunner.Status} at {Player.Hitbox.X},{Player.Hitbox.Y} frog@{frog.Position} hp={hf?.GetValue(frog)}");
            }
            return keys;
        }

        private static KeyFunctions[] FrogStrikeKeysCore(Entity frog, ref int swingWait)
        {
            swingWait++;
            // Note the frame each bullet splashed as it happens: first seen only when a plan is next made, the splash looked younger than
            // it was and the next volley came early, into a plan that thought itself clear.
            foreach (var f in BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Enemy.Crowd.Frog))
            {
                foreach (var b in f.SubEntities().Where(b => b.exists && b.velocity == Vector2.Zero && b is AnodyneSharp.Entities.Enemy.Crowd.Frog.BurstBullet))
                {
                    _frogSplashSeen.TryAdd(b, BotRunner.Frame);
                }
            }
            var pb = Player.Hitbox;
            // Every other danger counts too (a Rotator's fireball circling past), but frogs themselves are modelled below.
            var threats = PredictThreats(frog).Where(t => !(t.Source is AnodyneSharp.Entities.Enemy.Crowd.Frog)).ToList();
            var me = new Vector2(pb.Center.X, pb.Center.Y);
            foreach (var f in BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Enemy.Crowd.Frog))
            {
                var fc = new Vector2(f.Hitbox.Center.X, f.Hitbox.Center.Y);
                if ((fc - me).Length() > 110)
                {
                    continue;
                }
                // Touching a frog hurts.
                var body = new Threat { Name = "Frog", Margin = 1, Source = f, Stationary = true };
                for (int k = 0; k <= Horizon; k++)
                {
                    body.At[k] = f.Hitbox;
                }
                threats.Add(body);
                // The next volley: now if it is ready, else once the last bullet has landed and splashed. Aimed at wherever we are then,
                // so it is a danger all round its corner (12px out) for the 16 frames it is low.
                int fireAt = 0;
                foreach (var b in f.SubEntities().Where(b => b.exists && b is AnodyneSharp.Entities.Enemy.Crowd.Frog.BurstBullet))
                {
                    int left = 18;
                    if (b.velocity != Vector2.Zero && FrogParabolaField?.GetValue(b) is object para)
                    {
                        left += (int)Math.Ceiling(((float)ParabolaPeriodField.GetValue(para) - (float)ParabolaTField.GetValue(para)) * 60f);
                    }
                    fireAt = Math.Max(fireAt, left);
                }
                var volley = new Threat { Name = "FrogVolley", Margin = 1, Source = f, Launch = true };
                var corner = new Rectangle((int)f.Position.X, (int)f.Position.Y, 8, 8);
                if (fireAt == 0)
                {
                    var dir = me - f.Position;
                    dir = dir.LengthSquared() < 1 ? new Vector2(0, 1) : Vector2.Normalize(dir);
                    for (int k = 0; k <= 18; k++)
                    {
                        var o = f.Position + dir * Math.Min(12f, k * 45f / 60f);
                        volley.At[k] = new Rectangle((int)o.X, (int)o.Y, 8, 8);
                    }
                }
                else
                {
                    for (int k = fireAt; k <= fireAt + 18 && k <= Horizon; k++)
                    {
                        var r = corner;
                        int grow = (int)Math.Min(12f, (k - fireAt) * 45f / 60f);
                        r.Inflate(grow, grow);
                        volley.At[k] = r;
                    }
                    // ...and where that volley comes down (flight times unknown yet: anywhere its three speeds can carry it in 0.8-1.8s),
                    // aimed at about where we are now.
                    var adir = me - f.Position;
                    adir = adir.LengthSquared() < 1 ? new Vector2(0, 1) : Vector2.Normalize(adir);
                    foreach (float v in new[] { 15f, 30f, 45f })
                    {
                        var a = f.Position + adir * (v * 0.8f * 0.85f);
                        var b2 = f.Position + adir * (v * 1.8f);
                        var zone = new Rectangle((int)Math.Min(a.X, b2.X), (int)Math.Min(a.Y, b2.Y), (int)Math.Abs(a.X - b2.X) + 8, (int)Math.Abs(a.Y - b2.Y) + 8);
                        var land = new Threat { Name = "FrogVolley", Margin = 1, Source = f };
                        for (int k = fireAt + 40; k <= fireAt + 108 && k <= Horizon; k++)
                        {
                            land.At[k] = zone;
                        }
                        threats.Add(land);
                    }
                }
                threats.Add(volley);
            }
            var fh = frog.Hitbox;
            var fcen = new Vector2(fh.Center.X, fh.Center.Y);
            (Vector2, KeyFunctions, Facing) Spot(int side)
            {
                // A spot a jump's length from floor beats a closer one with only pit to take off from.
                var first = SpotAt(side, false);
                return float.IsNaN(first.Item1.X) ? first : (SpotAt(side, true) is var withTko && !float.IsNaN(withTko.Item1.X) ? withTko : first);
            }
            (Vector2, KeyFunctions, Facing) SpotAt(int side, bool needTakeoff)
            {
                // (above it and left of it are beside the corner its volleys leave from: there the most room the broom allows)
                foreach (int gap in side is 1 or 2 ? new[] { 9, 8, 7, 6, 5, 4 } : new[] { 7, 6, 8, 5, 9, 4 })
                {
                    var at = side switch
                    {
                        0 => new Vector2(fcen.X - pb.Width / 2f, fh.Bottom + gap),
                        1 => new Vector2(fcen.X - pb.Width / 2f, fh.Top - gap - pb.Height),
                        2 => new Vector2(fh.Left - gap - pb.Width, fcen.Y - pb.Height / 2f),
                        _ => new Vector2(fh.Right + gap, fcen.Y - pb.Height / 2f),
                    };
                    var ctr = at + new Vector2(pb.Width / 2f, pb.Height / 2f);
                    if (StandsSafe(new Rectangle((int)at.X, (int)at.Y, pb.Width, pb.Height)) && !BotNav.IsHole(new Point((int)Math.Floor(ctr.X / 16f), (int)Math.Floor(ctr.Y / 16f))))
                    {
                        if (needTakeoff)
                        {
                            var away = side switch { 0 => new Vector2(0, 1), 1 => new Vector2(0, -1), 2 => new Vector2(-1, 0), _ => new Vector2(1, 0) };
                            var tko = at + away * 27f;
                            if (!StandsSafe(new Rectangle((int)tko.X, (int)tko.Y, pb.Width, pb.Height)))
                            {
                                continue;
                            }
                        }
                        return side switch
                        {
                            0 => (at, KeyFunctions.Up, Facing.UP),
                            1 => (at, KeyFunctions.Down, Facing.DOWN),
                            2 => (at, KeyFunctions.Right, Facing.RIGHT),
                            _ => (at, KeyFunctions.Left, Facing.LEFT),
                        };
                    }
                }
                return (new Vector2(float.NaN, float.NaN), KeyFunctions.Up, Facing.UP);
            }
            var spots = new (Vector2 pos, KeyFunctions face, Facing facing)[] { Spot(0), Spot(1), Spot(2), Spot(3) };
            bool SafeAt(Vector2 at, int from, int to)
            {
                var r = new Rectangle((int)at.X, (int)at.Y, pb.Width, pb.Height);
                foreach (var th in threats)
                {
                    for (int k = Math.Max(0, from); k <= to && k <= Horizon; k++)
                    {
                        if (th.At[k] is Rectangle tr)
                        {
                            tr.Inflate(th.Margin, th.Margin);
                            if (r.Intersects(tr))
                            {
                                return false;
                            }
                        }
                    }
                }
                return true;
            }
            // The same without the volleys not fired yet (a jump plan models those exactly, see FrogVolleyHitFrame).
            bool SafeAtExact(Vector2 at, int from, int to)
            {
                var r = new Rectangle((int)at.X, (int)at.Y, pb.Width, pb.Height);
                foreach (var th in threats.Where(t => t.Name != "FrogVolley"))
                {
                    for (int k = Math.Max(0, from); k <= to && k <= Horizon; k++)
                    {
                        if (th.At[k] is Rectangle tr)
                        {
                            tr.Inflate(th.Margin, th.Margin);
                            if (r.Intersects(tr))
                            {
                                _lastHitThreat = th;
                                return false;
                            }
                        }
                    }
                }
                return true;
            }
            var here = new Vector2(pb.X, pb.Y);
            if (Player.state == PlayerState.AIR)
            {
                // Mid-jump (over a pit): steer onto the landing point and let go there (the air speed is 0.83 of walking).
                // Only along the jump's axis: a key across it would turn us (and the swing on landing needs us facing the frog).
                var aim = _frogAirTarget;
                var dk = new List<KeyFunctions>();
                bool horiz = Math.Abs(aim.X - _frogAirFrom.X) >= Math.Abs(aim.Y - _frogAirFrom.Y);
                if (horiz && aim.X - here.X > 1f) { dk.Add(KeyFunctions.Right); } else if (horiz && here.X - aim.X > 1f) { dk.Add(KeyFunctions.Left); }
                if (!horiz && aim.Y - here.Y > 1f) { dk.Add(KeyFunctions.Down); } else if (!horiz && here.Y - aim.Y > 1f) { dk.Add(KeyFunctions.Up); }
                BotRunner.Status = "fight/frog: jump";
                return dk.ToArray();
            }
            // Standing on a spot we jumped to: strike while it stays clear long enough for the swing (the broom holds us ~20 frames), and
            // jump back the way we came before a landing bullet or the next volley gets there.
            for (int i = 0; i < spots.Length; i++)
            {
                if (float.IsNaN(spots[i].pos.X) || Math.Abs(spots[i].pos.X - here.X) > 2f || Math.Abs(spots[i].pos.Y - here.Y) > 2f)
                {
                    continue;
                }
                var sp = spots[i];
                bool overPit = _frogTakeoff is Vector2 tk && (tk - here).Length() < 40 && FrogPitBetween(here, tk);
                // (on a spot over the pit we came by an exact plan: volleys not fired yet are judged by that, not the rough all-round guess.
                // On floor: clear for the swing (~20 frames, it holds us) and the steps out from under a bullet coming down after it.)
                Func<Vector2, int, int, bool> safeHere = overPit || BotRunner.Frame <= _frogGroundCommit + 40 ? SafeAtExact : SafeAt;
                if (!frog._flickering && swingWait >= 20 && !(Player.broom != null && Player.broom.exists) && Player.facing == sp.facing && safeHere(here, 1, overPit || BotRunner.Frame <= _frogGroundCommit + 40 ? 26 : 36))
                {
                    swingWait = 0;
                    BotRunner.Status = "fight/frog: swing";
                    return K(KeyFunctions.Accept);
                }
                if (Player.broom != null && Player.broom.exists)
                {
                    BotRunner.Status = "fight/frog: swinging";
                    return K();
                }
                // (judged as the swing is: a spot we walked onto by a plan checked against the volleys keeps to that plan)
                if (Player.facing != sp.facing && (safeHere(here, 1, 30) || overPit))
                {
                    BotRunner.Status = "fight/frog: face it";
                    return K(sp.face);
                }
                if (overPit && BotRunner.Frame >= _frogBackAt)
                {
                    // The planned moment to jump home (the plan checked that flight against every bullet).
                    var home = _frogTakeoff.Value;
                    var hk = Math.Abs(home.X - here.X) >= Math.Abs(home.Y - here.Y) ? (home.X > here.X ? KeyFunctions.Right : KeyFunctions.Left) : (home.Y > here.Y ? KeyFunctions.Down : KeyFunctions.Up);
                    _frogAirTarget = home;
                    _frogAirFrom = here;
                    _frogTakeoff = null;
                    BotRunner.Status = "fight/frog: jump home (planned)";
                    if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                    {
                        System.Console.Error.WriteLine($"  frogjump HOME f{BotRunner.Frame} from {here} to {home}");
                    }
                    return K(hk, KeyFunctions.Cancel);
                }
                if (overPit)
                {
                    if (!safeHere(here, 1, 34) || frog._flickering || swingWait < 20)
                    {
                        var back = FrogJumpKeys(_frogTakeoff.Value, threats, SafeAt, takeoffOnly: true);
                        if (back != null)
                        {
                            return back;
                        }
                        if (!SafeAt(here, 1, 12))
                        {
                            // Staying is worse than a bad jump home.
                            var hk = _frogTakeoff.Value.X > here.X + 4 ? KeyFunctions.Right : _frogTakeoff.Value.X < here.X - 4 ? KeyFunctions.Left : _frogTakeoff.Value.Y > here.Y ? KeyFunctions.Down : KeyFunctions.Up;
                            _frogAirTarget = _frogTakeoff.Value;
                            _frogAirFrom = here;
                            _frogTakeoff = null;
                            BotRunner.Status = "fight/frog: jump home now";
                            return K(hk, KeyFunctions.Cancel);
                        }
                    }
                    BotRunner.Status = "fight/frog: hold the spot over the pit";
                    return K();
                }
                if (safeHere(here, 1, 30))
                {
                    BotRunner.Status = "fight/frog: hold the spot";
                    return K();
                }
                break;
            }
            int best = -1;
            float bestCost = float.MaxValue;
            for (int i = 0; i < spots.Length; i++)
            {
                if (float.IsNaN(spots[i].pos.X))
                {
                    continue;
                }
                float dist = (spots[i].pos - here).Length();
                int eta = (int)(dist / WalkPxPerFrame);
                float cost = dist + (SafeAt(spots[i].pos, eta, eta + 40) ? 0 : 150);
                // A spot across a pit is only any good with floor to take off from a jump's length back along its line, on our side of the pit.
                var tko = spots[i].pos - DirVector(spots[i].face) * 27f;
                if (FrogPitBetween(here, spots[i].pos) && (!StandsSafe(new Rectangle((int)tko.X, (int)tko.Y, pb.Width, pb.Height)) || FrogPitBetween(here, tko)))
                {
                    cost += 2000;
                }
                // The far side of a frog (or of the one stacked beside it) is no spot: the way there runs past it.
                var spC = spots[i].pos + new Vector2(pb.Width / 2f, pb.Height / 2f);
                foreach (var th in threats.Where(t => t.Name == "Frog"))
                {
                    var box = th.At[0].Value;
                    box.Inflate(6, 6);
                    for (float u = 0.1f; u < 0.95f; u += 0.05f)
                    {
                        var q = Vector2.Lerp(me, spC, u);
                        if (box.Contains((int)q.X, (int)q.Y))
                        {
                            cost += 1000;
                            break;
                        }
                    }
                }
                if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && BotRunner.Frame % 20 == 0)
                {
                    System.Console.Error.WriteLine($"  frogspot f{BotRunner.Frame} side={i} at={spots[i].pos} cost={cost:0}");
                }
                if (cost < bestCost)
                {
                    bestCost = cost;
                    best = i;
                }
            }
            if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && BotRunner.Frame % 20 == 0)
            {
                System.Console.Error.WriteLine($"  frog f{BotRunner.Frame} me={here} spot={(best < 0 ? "-" : spots[best].pos.ToString())} cost={bestCost:0} threats={threats.Count} last='{BotRunner.Status}'");
            }
            if (best < 0)
            {
                BotRunner.Status = "fight/frog: no spot beside it, standoff";
                return StandoffKeys(fh, pb, ref swingWait, 40);
            }
            var spot = spots[best];
            // (a pit just short of the spot: it is jumped from a take-off point a jump's length back along the line we face the frog on)
            bool pitOnWay = GlobalState.inventory.CanJump && (FrogPitBetween(here, spot.pos) || FrogPitBetween(spot.pos - DirVector(spot.face) * 27f, spot.pos));
            if (!pitOnWay && FrogGroundKeys(spot.pos, spot.face, threats, SafeAtExact) is KeyFunctions[] gk)
            {
                return gk;
            }
            // A bullet aimed at us while we struck comes down on the spot (the slow one, a second or so later): keep off it till it has.
            int etaSpot = (int)((spot.pos - here).Length() / WalkPxPerFrame);
            if (!pitOnWay && !SafeAtExact(spot.pos, etaSpot, etaSpot + 24))
            {
                if (!SafeAtExact(here, 1, 30))
                {
                    if (FrogDodge(here, threats) is KeyFunctions[] away)
                    {
                        BotRunner.Status = "fight/frog: off the spot till its bullet is down";
                        return K(away);
                    }
                }
                else
                {
                    BotRunner.Status = "fight/frog: wait for the bullet on the spot to come down";
                    return K();
                }
            }
            var mv = pitOnWay ? null : SpaceTimeMove(spot.pos, threats, useCache: false);
            if (mv != null)
            {
                BotRunner.Status = $"fight/frog: to the {spot.face} spot round its bullets";
                return K(mv);
            }
            if (pitOnWay && FrogJumpKeys(spot.pos, threats, SafeAt, along: spot.face, safeAtExact: SafeAtExact) is KeyFunctions[] jk)
            {
                return jk;
            }
            if (!SafeAt(here, 1, 40))
            {
                var dodge = SpaceTimeMove(here, threats, useCache: false, survive: true);
                if (dodge != null)
                {
                    BotRunner.Status = "fight/frog: dodge its bullets";
                    return K(dodge);
                }
                // Nowhere is clear of the rough guess at the next volley too: dodge the bullets that are really flying.
                if (!SafeAtExact(here, 1, 40) && FrogDodge(here, threats) is KeyFunctions[] dodge2)
                {
                    BotRunner.Status = "fight/frog: dodge its flying bullets";
                    return K(dodge2);
                }
            }
            if (OffTheFrog(threats) is KeyFunctions[] off)
            {
                return off;
            }
            BotRunner.Status = "fight/frog: wait";
            if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
            {
                foreach (var th in threats.Where(t => t.Name == "FrogBullet"))
                {
                    var sb = new System.Text.StringBuilder();
                    for (int k = 0; k <= 12; k++)
                    {
                        sb.Append(th.At[k] is Rectangle tr ? $" {k}:{tr.X},{tr.Y}" : "");
                    }
                    System.Console.Error.WriteLine($"    wait f{BotRunner.Frame} me={pb} bullet {th.Source?.Position} v={th.Source?.velocity}{sb}");
                }
            }
            return K();
        }

        private static long _frogGroundCommit = -1;
        private static long _frogVolleyWaitFrom = -1000, _frogVolleyWaitLast = -1000;
        private static int _frogGroundStall;

        /// <summary>
        /// A spot on our side of the floor: walk in, strike and back off by a plan checked frame by frame against the bullets flying and the
        /// volleys the frogs will fire at us on the way (see FrogVolleyHitFrame), as the jumps over pits are. Null: no straight way in, or
        /// no clear plan for a long while (the rough rules take over then).
        /// </summary>
        private static KeyFunctions[] FrogGroundKeys(Vector2 spot, KeyFunctions face, List<Threat> threats, Func<Vector2, int, int, bool> safeExact)
        {
            var pb = Player.Hitbox;
            var here = new Vector2(pb.X, pb.Y);
            if ((spot - here).Length() <= 2f)
            {
                return null; // on it: the strike itself is decided above
            }
            KeyFunctions[] Towards(Vector2 to)
            {
                var rk = new List<KeyFunctions>();
                if (to.X - here.X > 1f) { rk.Add(KeyFunctions.Right); } else if (here.X - to.X > 1f) { rk.Add(KeyFunctions.Left); }
                if (to.Y - here.Y > 1f) { rk.Add(KeyFunctions.Down); } else if (here.Y - to.Y > 1f) { rk.Add(KeyFunctions.Up); }
                return rk.ToArray();
            }
            if (BotRunner.Frame <= _frogGroundCommit && safeExact(here, 1, 2))
            {
                BotRunner.Status = "fight/frog: in to strike (planned)";
                return K(Towards(spot));
            }
            int walk = (int)Math.Ceiling((spot - here).Length() / WalkPxPerFrame);
            var plan = new List<Vector2>();
            bool straight = true;
            for (int k = 1; k <= walk; k++)
            {
                var at = Vector2.Lerp(here, spot, k / (float)walk);
                straight &= StandsSafe(new Rectangle((int)at.X, (int)at.Y, pb.Width, pb.Height));
                plan.Add(at);
            }
            if (!straight || walk > 90)
            {
                return null;
            }
            for (int k = 0; k < 26; k++)
            {
                plan.Add(spot); // turn and swing (the broom holds us ~20 frames)
            }
            // ...then back the way we came.
            var back = here - spot;
            back = back.LengthSquared() < 1 ? -DirVector(face) : Vector2.Normalize(back);
            var last = spot;
            for (int k = 0; k < 24; k++)
            {
                var next = last + back * WalkPxPerFrame;
                if (StandsSafe(new Rectangle((int)next.X, (int)next.Y, pb.Width, pb.Height)))
                {
                    last = next;
                }
                plan.Add(last);
            }
            string why = "";
            bool clear = true;
            for (int k = 0; k < plan.Count && clear; k++)
            {
                if (!safeExact(plan[k], k + 1, k + 1))
                {
                    clear = false;
                    why = $"plan {k + 1}/{plan.Count} {_lastHitThreat?.Name}";
                }
            }
            if (clear && FrogVolleyHitFrame(plan, out string who) is int hit && hit > 0)
            {
                clear = false;
                why = $"volley {hit}/{plan.Count} {who}";
            }
            if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && (clear || BotRunner.Frame % 10 == 0))
            {
                System.Console.Error.WriteLine($"  frogground f{BotRunner.Frame} at={here} spot={spot} walk={walk} clear={clear} {why}");
            }
            if (clear)
            {
                _frogGroundCommit = BotRunner.Frame + walk;
                _frogGroundStall = 0;
                BotRunner.Status = "fight/frog: in to strike (planned)";
                return K(Towards(spot));
            }
            if (++_frogGroundStall > 400)
            {
                return null;
            }
            if (!safeExact(here, 1, 30) && FrogDodge(here, threats) is KeyFunctions[] dodge)
            {
                BotRunner.Status = "fight/frog: out of the way, waiting for a clear way in";
                return K(dodge);
            }
            // Waiting where the next volley leaves from takes it the frame it fires (a wall can leave OffTheFrog nowhere to go).
            if (FrogVolleyHitFrame(Enumerable.Repeat(here, 40).ToList(), out _) > 0
                && SpaceTimeMove(here, threats.Where(t => t.Name != "FrogVolley" || t.Launch).ToList(), useCache: false, survive: true) is KeyFunctions[] clear2 && clear2.Length > 0)
            {
                BotRunner.Status = "fight/frog: off where its volley leaves from";
                return K(clear2);
            }
            if (OffTheFrog(threats) is KeyFunctions[] off)
            {
                return off;
            }
            BotRunner.Status = "fight/frog: wait for a clear way in";
            BotRunner.NotStuck(); // waiting for the volleys to line up is progress (the stall count above gives up on it)
            return K();
        }

        /// <summary>
        /// Keys dodging the bullets really flying, and off the corner the next volley leaves from where that can be done too: a dodge that
        /// ends beside the frog is hit by that volley the moment it fires. Null if no dodge is clear.
        /// </summary>
        private static KeyFunctions[] FrogDodge(Vector2 here, List<Threat> threats)
        {
            var withLaunch = threats.Where(t => t.Name != "FrogVolley" || t.Launch).ToList();
            return SpaceTimeMove(here, withLaunch, useCache: false, survive: true)
                ?? SpaceTimeMove(here, threats.Where(t => t.Name != "FrogVolley").ToList(), useCache: false, survive: true);
        }

        /// <summary>
        /// Keys stepping away from a frog we stand on or right beside (its volley's bullets start on top of it, and touching it hurts
        /// too): never wait there. Null when no frog is that close or no way away is walkable.
        /// </summary>
        private static KeyFunctions[] OffTheFrog(List<Threat> threats = null)
        {
            var pc = Player.Hitbox.Center;
            var near = BotRunner.Entities().FirstOrDefault(e => e.exists && e is AnodyneSharp.Entities.Enemy.Crowd.Frog
                    && Math.Abs(e.Hitbox.Center.X - pc.X) < 18 && Math.Abs(e.Hitbox.Center.Y - pc.Y) < 18);
            if (near == null)
            {
                return null;
            }
            float ax = pc.X - near.Hitbox.Center.X, ay = pc.Y - near.Hitbox.Center.Y;
            var keys = Math.Abs(ax) >= Math.Abs(ay)
                ? new[] { ax < 0 ? KeyFunctions.Left : KeyFunctions.Right, ay < 0 ? KeyFunctions.Up : KeyFunctions.Down }
                : new[] { ay < 0 ? KeyFunctions.Up : KeyFunctions.Down, ax < 0 ? KeyFunctions.Left : KeyFunctions.Right };
            // (then the other two ways: a wall can block both of those)
            keys = keys.Concat(new[] { KeyFunctions.Left, KeyFunctions.Right, KeyFunctions.Up, KeyFunctions.Down }.Except(keys)
                    .Where(k => Vector2.Dot(DirVector(k), new Vector2(ax, ay)) >= 0)).ToArray();
            var pb = Player.Hitbox;
            bool IntoBullets(KeyFunctions k)
            {
                // A step straight into bullets flying, or into where the next volley leaves from, is no way off.
                if (threats == null)
                {
                    return false;
                }
                for (int f = 1; f <= 12; f++)
                {
                    var at = new Vector2(pb.X, pb.Y) + DirVector(k) * (WalkPxPerFrame * f);
                    var r = new Rectangle((int)at.X, (int)at.Y, pb.Width, pb.Height);
                    foreach (var th in threats.Where(t => t.Name is not ("FrogVolley" or "Frog") || t.Launch))
                    {
                        if (th.At[f] is Rectangle tr && r.Intersects(tr))
                        {
                            return true;
                        }
                    }
                }
                return false;
            }
            foreach (var k in keys)
            {
                var step = DirVector(k) * 16f;
                if (BotNav.Walkable(BotNav.TileOf(Pos + step)) && !IntoBullets(k))
                {
                    BotRunner.Status = "fight/frog: off the frog";
                    return K(k);
                }
            }
            return null;
        }

        private static Vector2? _frogTakeoff;
        private static long _frogCommit = -1, _frogBackAt = long.MaxValue, _frogDrawFrame = -1;
        private static int _frogDrawIdx;
        private static Vector2 _frogAirTarget, _frogAirFrom;

        private static bool FrogPitBetween(Vector2 a, Vector2 b)
        {
            var half = new Vector2(5, 6);
            var d = b - a;
            for (float t = 0; t <= d.Length(); t += 3)
            {
                if (BotNav.IsHole(BotNav.TileOf(a + (d.LengthSquared() < 0.01f ? Vector2.Zero : Vector2.Normalize(d)) * t)))
                {
                    return true;
                }
            }
            return false;
        }

        /// <summary>
        /// The spot lies across a pit in a straight line (the planner only walks): go to the take-off point a jump's length (~26px) short of
        /// it and jump when the flight, the landing and the swing there are clear of the bullets. `takeoffOnly`: we are at the take-off
        /// point already (jumping back off a spot). Null if the line is not straight.
        /// </summary>
        private static readonly System.Reflection.FieldInfo FrogMultiplierField =
            typeof(AnodyneSharp.Entities.Enemy.Crowd.Frog.BurstBullet).GetField("_multiplier", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
        private static readonly Dictionary<Entity, long> _frogSplashSeen = new();

        /// <summary>
        /// The first frame (1..) of a plan (our hitbox top-left per frame, staying at the last point after it) that a frog volley not fired
        /// yet hits, or 0. A frog fires its 3 bullets the frame we are within 64px (centre to centre) once its last volley has splashed
        /// (18 frames after landing), aimed from its top-left at our centre then, at 15/30/45px/s; each bullet's flight time is rolled once
        /// for the frog's life, so it is read off the bullet even while it is not flying. A bullet hurts while low: the first and last ~14.6%.
        /// </summary>
        private static int FrogVolleyHitFrame(IList<Vector2> plan, out string who)
        {
            who = "";
            var pb = Player.Hitbox;
            var cOff = Player.Center - new Vector2(pb.X, pb.Y);
            foreach (var k in _frogSplashSeen.Keys.Where(e => !e.exists || e.velocity != Vector2.Zero).ToList())
            {
                _frogSplashSeen.Remove(k);
            }
            var me = plan.Count > 0 ? plan[0] + cOff : Player.Center;
            int len = plan.Count;
            Vector2 At(int k) => plan[Math.Clamp(k, 1, len) - 1];
            foreach (var f in BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Enemy.Crowd.Frog).ToList())
            {
                if ((f.Center - me).Length() > 140)
                {
                    continue;
                }
                var bullets = f.SubEntities().Where(b => b is AnodyneSharp.Entities.Enemy.Crowd.Frog.BurstBullet).ToList();
                int fireAt = 1;
                foreach (var b in bullets.Where(b => b.exists))
                {
                    int left;
                    if (b.velocity != Vector2.Zero && FrogParabolaField?.GetValue(b) is object para)
                    {
                        left = (int)Math.Ceiling(((float)ParabolaPeriodField.GetValue(para) - (float)ParabolaTField.GetValue(para)) * 60f) + 18;
                    }
                    else
                    {
                        if (!_frogSplashSeen.TryGetValue(b, out long seen))
                        {
                            seen = BotRunner.Frame;
                            _frogSplashSeen[b] = seen;
                        }
                        left = Math.Max(0, 18 - (int)(BotRunner.Frame - seen));
                    }
                    fireAt = Math.Max(fireAt, left + 1);
                }
                for (int volley = 0; volley < 3 && fireAt <= len; volley++)
                {
                    int fire = -1;
                    for (int k = fireAt; k <= len; k++)
                    {
                        if ((At(k) + cOff - f.Center).Length() < 64)
                        {
                            fire = k;
                            break;
                        }
                    }
                    if (fire < 0)
                    {
                        break;
                    }
                    var aim = At(fire) + cOff - f.Position;
                    aim = aim.LengthSquared() < 1 ? new Vector2(0, 1) : Vector2.Normalize(aim);
                    float maxPeriod = 0;
                    foreach (var b in bullets)
                    {
                        if (FrogParabolaField?.GetValue(b) is not object para)
                        {
                            continue;
                        }
                        float period = (float)ParabolaPeriodField.GetValue(para);
                        maxPeriod = Math.Max(maxPeriod, period);
                        float v = 15f * (FrogMultiplierField?.GetValue(b) is int m ? m : 3);
                        var hb = b.Hitbox;
                        for (int j = 0; j <= (int)Math.Ceiling(period * 60f) && fire + j <= len; j++)
                        {
                            float u = j / 60f / period;
                            // (a frame's slack each end of the low part and a 3px margin: our aim at the frame it fires is a guess a
                            // pixel or two out, and a plan that grazed its bullets by that much took the hit)
                            if (u > 0.165f && u < 0.835f)
                            {
                                continue; // high: passes over us
                            }
                            var bp = f.Position + aim * (v * j / 60f);
                            var br = new Rectangle((int)bp.X, (int)bp.Y, hb.Width, hb.Height);
                            br.Inflate(3, 3);
                            var p = At(fire + j);
                            if (br.Intersects(new Rectangle((int)p.X, (int)p.Y, pb.Width, pb.Height)))
                            {
                                who = $"{f.Position} v{v} j{j} fire@{fire}";
                                return fire + j;
                            }
                        }
                    }
                    fireAt = fire + (int)Math.Ceiling(maxPeriod * 60f) + 18 + 1;
                }
            }
            return 0;
        }

        /// <summary>Frames on the spot across a frog's pit before jumping back (the broom connects early in its 20-frame swing).</summary>
        private static long _frogStillSince;
        private static Vector2 _frogStillPos;

        private static readonly int FrogStay = int.TryParse(Environment.GetEnvironmentVariable("ANODYNE_BOT_FROG_STAY"), out int fst) ? fst : 22;

        private static KeyFunctions[] FrogJumpKeys(Vector2 spot, List<Threat> threats, Func<Vector2, int, int, bool> safeAt, bool takeoffOnly = false, KeyFunctions? along = null, Func<Vector2, int, int, bool> safeAtExact = null)
        {
            var pb = Player.Hitbox;
            var here = new Vector2(pb.X, pb.Y);
            var d = spot - here;
            Vector2 dir;
            KeyFunctions key;
            if (along.HasValue)
            {
                dir = DirVector(along.Value);
                key = along.Value;
            }
            else if (Math.Abs(d.Y) <= 8 && Math.Abs(d.X) > 8)
            {
                dir = new Vector2(Math.Sign(d.X), 0);
                key = d.X > 0 ? KeyFunctions.Right : KeyFunctions.Left;
            }
            else if (Math.Abs(d.X) <= 8 && Math.Abs(d.Y) > 8)
            {
                dir = new Vector2(0, Math.Sign(d.Y));
                key = d.Y > 0 ? KeyFunctions.Down : KeyFunctions.Up;
            }
            else
            {
                return null;
            }
            const float Air = 0.83f * 70f / 60f;
            const int AirFrames = 27;
            var origin = takeoffOnly ? here : spot - dir * (Air * AirFrames);
            if (!StandsSafe(new Rectangle((int)origin.X, (int)origin.Y, pb.Width, pb.Height)))
            {
                return null;
            }
            int stay = takeoffOnly ? 8 : FrogStay; // after landing: out of reach (back home), or the swing (20 frames)
            // Is the run from here to the take-off point, the flight and the stay on the spot clear of every predicted bullet?
            int walk = (int)Math.Ceiling((origin - here).Length() / WalkPxPerFrame);
            bool Clear(out string why)
            {
                why = "";
                // The whole plan, frame by frame (top-left of our hitbox at frame k+1): the run, the flight, the swing on the spot,
                // the flight back and a while at home.
                var plan = new List<Vector2>();
                for (int k = 1; k <= walk; k++)
                {
                    plan.Add(Vector2.Lerp(here, origin, k / (float)walk));
                }
                for (int k = 1; k <= AirFrames; k++)
                {
                    plan.Add(origin + dir * (Air * k));
                }
                for (int k = 0; k < stay; k++)
                {
                    plan.Add(spot);
                }
                if (!takeoffOnly)
                {
                    for (int k = 1; k <= AirFrames; k++)
                    {
                        plan.Add(spot - dir * (Air * k));
                    }
                    // (back home: on along the way back, out from under where the volley it fired at the spot comes down)
                    var last = origin;
                    for (int k = 1; k <= 24; k++)
                    {
                        var next = last - dir * WalkPxPerFrame;
                        if (StandsSafe(new Rectangle((int)next.X, (int)next.Y, pb.Width, pb.Height)))
                        {
                            last = next;
                        }
                        plan.Add(last);
                    }
                }
                if (safeAtExact != null)
                {
                    // Exact: every frog's next volleys are known once we know where we are when it fires (its bullets' flight times never change).
                    for (int k = 0; k < walk; k++)
                    {
                        if (!StandsSafe(new Rectangle((int)plan[k].X, (int)plan[k].Y, pb.Width, pb.Height)))
                        {
                            why = "no straight run";
                            return false;
                        }
                    }
                    for (int k = 0; k < plan.Count; k++)
                    {
                        if (!safeAtExact(plan[k], k + 1, k + 1))
                        {
                            why = $"plan {k + 1}/{plan.Count} {_lastHitThreat?.Name} {_lastHitThreat?.Source?.Position}";
                            return false;
                        }
                    }
                    int hit = FrogVolleyHitFrame(plan, out string who);
                    if (hit > 0)
                    {
                        why = $"volley {hit}/{plan.Count} {who}";
                        return false;
                    }
                    return true;
                }
                // A frog that is ready to fire looses a volley the moment we come within 64px: its bullets are unknown until then.
                var meNow = new Vector2(pb.Center.X, pb.Center.Y);
                if (!takeoffOnly && BotRunner.Entities().Any(e => e.exists && e is AnodyneSharp.Entities.Enemy.Crowd.Frog f && (new Vector2(f.Hitbox.Center.X, f.Hitbox.Center.Y) - meNow).Length() < 110 &&
                    !f.SubEntities().Any(b => b.exists && b is AnodyneSharp.Entities.Enemy.Crowd.Frog.BurstBullet)))
                {
                    why = "a frog is ready to fire";
                    return false;
                }
                for (int k = 1; k <= walk; k++)
                {
                    var at = Vector2.Lerp(here, origin, k / (float)walk);
                    if (!StandsSafe(new Rectangle((int)at.X, (int)at.Y, pb.Width, pb.Height)))
                    {
                        why = "no straight run";
                        return false;
                    }
                    if (!safeAt(at, k, k))
                    {
                        why = $"run {k}/{walk}";
                        return false;
                    }
                }
                for (int k = 1; k <= AirFrames; k++)
                {
                    if (!safeAt(origin + dir * (Air * k), walk + k, walk + k))
                    {
                        why = $"flight {k}";
                        return false;
                    }
                }
                if (!safeAt(spot, walk + AirFrames, walk + AirFrames + stay))
                {
                    why = "spot";
                    return false;
                }
                if (!takeoffOnly)
                {
                    // ...and the jump back home after the swing.
                    int t0 = walk + AirFrames + stay;
                    for (int k = 1; k <= AirFrames; k++)
                    {
                        if (!safeAt(spot - dir * (Air * k), t0 + k, t0 + k))
                        {
                            why = $"back {k}";
                            return false;
                        }
                    }
                    if (!safeAt(origin, t0 + AirFrames, t0 + AirFrames + 6))
                    {
                        why = "home";
                        return false;
                    }
                }
                return true;
            }
            bool committed = !takeoffOnly && BotRunner.Frame <= _frogCommit && safeAt(here, 1, 2);
            string whyNot = "committed";
            bool clear = committed || Clear(out whyNot);
            if (clear && !takeoffOnly && !committed && Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
            {
                System.Console.Error.WriteLine($"  frogjump COMMIT f{BotRunner.Frame} here={here} walk={walk} land@+{walk + AirFrames} spot={spot}");
                foreach (var th in threats.Where(t => t.Name == "FrogBullet" || t.Name == "FrogVolley"))
                {
                    var sb = new System.Text.StringBuilder();
                    for (int k = 0; k <= Horizon; k++)
                    {
                        if (th.At[k] is Rectangle tr)
                        {
                            sb.Append($" {k}:{tr.X},{tr.Y}");
                        }
                    }
                    System.Console.Error.WriteLine($"    {th.Source?.GetType().Name} {th.Source?.Position}{sb}");
                }
            }
            if (clear && !takeoffOnly && !committed)
            {
                _frogCommit = BotRunner.Frame + walk + 2; // the run is planned: no second thoughts on the way (the plan is exact)
            }
            if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null && BotRunner.Frame % 5 == 0)
            {
                System.Console.Error.WriteLine($"  frogjump f{BotRunner.Frame} at={here} origin={origin} spot={spot} walk={walk} clear={clear} {whyNot}");
            }
            if (takeoffOnly)
            {
                if (!clear)
                {
                    return null;
                }
            }
            else if (!clear)
            {
                // Wait out of its range (64px: it does not fire) a little short of the take-off point, and try again from there.
                if (!safeAt(here, 1, 20) && SpaceTimeMove(here, threats, useCache: false, survive: true) is KeyFunctions[] dodge && dodge.Length > 0)
                {
                    BotRunner.Status = "fight/frog: dodge short of the pit";
                    return K(dodge);
                }
                var near = BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Enemy.Crowd.Frog f && (new Vector2(f.Hitbox.Center.X, f.Hitbox.Center.Y) - new Vector2(spot.X + 5, spot.Y + 6)).Length() < 40).ToList();
                var readyOnes = near.Where(f => !f.SubEntities().Any(b => b.exists && b is AnodyneSharp.Entities.Enemy.Crowd.Frog.BurstBullet)).ToList();
                var stepKey = StepKey(origin.X - here.X, origin.Y - here.Y, null);
                var stepTo = here + DirVector(stepKey) * 6f;
                bool stepOk = StandsSafe(new Rectangle((int)stepTo.X, (int)stepTo.Y, pb.Width, pb.Height));
                if (readyOnes.Count > 0 && (origin - here).Length() > 3f && stepOk && safeAt(stepTo, 1, 12))
                {
                    if (BotRunner.Frame - _frogDrawFrame > 30)
                    {
                        _frogDrawIdx++; // a new volley drawn: next time from the next waiting point
                    }
                    _frogDrawFrame = BotRunner.Frame;
                    // A frog is quiet: step in until it fires (every frog's volley has to be in the air, known, before we commit).
                    BotRunner.Status = "fight/frog: step in to draw a volley";
                    return K(stepKey);
                }
                if (BotRunner.Frame - _frogVolleyWaitLast > 2)
                {
                    _frogVolleyWaitFrom = BotRunner.Frame;
                }
                if (readyOnes.Count > 0 && BotRunner.Frame - _frogVolleyWaitFrom < 90)
                {
                    // (not for ever: a frog that is out of its 64px range never fires, so after a while draw it from elsewhere)
                    _frogVolleyWaitLast = BotRunner.Frame;
                    BotRunner.Status = "fight/frog: wait for its volley";
                    return K();
                }
                // Where we draw the volley from decides where its bullets come down: vary it (off the run line, one side then the other)
                // until a volley leaves the run, the jump and the spot clear.
                var perp = new Vector2(dir.Y, dir.X);
                var waits = new List<Vector2>();
                foreach (float side in new[] { -16f, 16f, 0f })
                {
                    foreach (float back in new[] { 24f, 14f })
                    {
                        var w = origin - dir * back + perp * side;
                        bool straight = true;
                        for (float u = 0; u <= 1.001f && straight; u += 0.1f)
                        {
                            var at = Vector2.Lerp(w, origin, u);
                            straight = StandsSafe(new Rectangle((int)at.X, (int)at.Y, pb.Width, pb.Height));
                        }
                        if (straight)
                        {
                            waits.Add(w);
                            break;
                        }
                    }
                }
                var wait = waits.Count > 0 ? waits[_frogDrawIdx % waits.Count] : origin - dir * 14f;
                if ((wait - here).Length() > 4f)
                {
                    var back = SpaceTimeMove(wait, threats, useCache: false);
                    if (back != null && back.Length > 0)
                    {
                        BotRunner.Status = "fight/frog: back to wait short of the pit";
                        return K(back);
                    }
                }
                BotRunner.Status = "fight/frog: wait short of the pit for its volley to end";
                return K();
            }
            if ((origin - here).Length() > 3f) // (the air steering makes up the last pixels)
            {
                BotRunner.Status = "fight/frog: run to the take-off point";
                var rk = new List<KeyFunctions>();
                if (origin.X - here.X > 1f) { rk.Add(KeyFunctions.Right); } else if (here.X - origin.X > 1f) { rk.Add(KeyFunctions.Left); }
                if (origin.Y - here.Y > 1f) { rk.Add(KeyFunctions.Down); } else if (here.Y - origin.Y > 1f) { rk.Add(KeyFunctions.Up); }
                return rk.ToArray();
            }
            BotRunner.Status = takeoffOnly ? "fight/frog: jump back over the pit" : "fight/frog: jump the pit";
            _frogTakeoff = takeoffOnly ? null : origin;
            _frogCommit = -1;
            if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
            {
                System.Console.Error.WriteLine($"  frogjump JUMP f{BotRunner.Frame} from {here} to {spot}");
            }
            _frogAirTarget = spot;
            _frogAirFrom = here;
            _frogBackAt = takeoffOnly ? long.MaxValue : BotRunner.Frame + AirFrames + stay;
            return K(key, KeyFunctions.Cancel);
        }

        private static KeyFunctions[] StandoffKeys(Rectangle tg, Rectangle pb, ref int swingWait, int cooldown)
        {
            float dx = tg.Center.X - pb.Center.X, dy = tg.Center.Y - pb.Center.Y;
            bool vert = Math.Abs(dy) >= Math.Abs(dx);
            var faceKey = vert ? (dy > 0 ? KeyFunctions.Down : KeyFunctions.Up) : (dx > 0 ? KeyFunctions.Right : KeyFunctions.Left);
            var backKey = vert ? (dy > 0 ? KeyFunctions.Up : KeyFunctions.Down) : (dx > 0 ? KeyFunctions.Left : KeyFunctions.Right);
            int gap = vert ? (dy > 0 ? tg.Top - pb.Bottom : pb.Top - tg.Bottom) : (dx > 0 ? tg.Left - pb.Right : pb.Left - tg.Right);
            float cross = vert ? dx : dy;
            var wantFacing = faceKey == KeyFunctions.Up ? Facing.UP : faceKey == KeyFunctions.Down ? Facing.DOWN : faceKey == KeyFunctions.Left ? Facing.LEFT : Facing.RIGHT;
            swingWait++;
            if (gap < 3)
            {
                return K(backKey);
            }
            if (Math.Abs(cross) > 4)
            {
                var slide = vert ? (cross > 0 ? KeyFunctions.Right : KeyFunctions.Left) : (cross > 0 ? KeyFunctions.Down : KeyFunctions.Up);
                return K(WalkableAhead(slide, 4) ? slide : faceKey);
            }
            if (Player.facing != wantFacing)
            {
                return K(gap >= 5 ? faceKey : backKey); // turning nudges ~1px towards it
            }
            if (gap > 11)
            {
                return K(faceKey);
            }
            if (swingWait >= cooldown && !(Player.broom != null && Player.broom.exists))
            {
                swingWait = 0;
                return K(KeyFunctions.Accept);
            }
            return K();
        }

        /// <summary>
        /// Where the dog's hitbox will be f frames from now, given that its attack began `e` frames ago aimed at `tPos` (our position then):
        /// three one-second dashes towards (tPos.X-44, tPos.Y-8), (tPos.X+36, tPos.Y-8), (tPos.X-44, tPos.Y-8) at 66, 84, 84 px/s.
        /// </summary>
        private static Func<int, Rectangle?> DogPath(Entity dog, int e, Vector2 tPos)
        {
            var start = dog.Position;
            var hb0 = dog.Hitbox;
            float[] speeds = { 66f, 84f, 84f };
            float[] dxs = { -44f, 36f, -44f };
            var pos = start;
            var vel = dog.velocity;
            var cache = new List<Rectangle?>();
            for (int f = 0; f <= Horizon; f++)
            {
                int t = e + f;
                if (f > 0 && t % 60 == 0)
                {
                    int phase = t / 60; // 1.. (phase 0 started the attack)
                    if (phase <= 2)
                    {
                        var target = new Vector2(tPos.X + dxs[phase], tPos.Y - 8);
                        var dir = target - pos;
                        vel = dir.Length() < 0.5f ? Vector2.Zero : Vector2.Normalize(dir) * speeds[phase];
                    }
                    else
                    {
                        vel = Vector2.Zero; // pacing again
                    }
                }
                if (f > 0)
                {
                    pos += vel / 60f;
                }
                var r = hb0;
                r.Offset((int)(pos.X - start.X), (int)(pos.Y - start.Y));
                cache.Add(r);
            }
            return f => cache[Math.Min(f, cache.Count - 1)];
        }

        /// <summary>The direction (towards a beam lane that is at most 4 shoves away over open floor) in which to push a Shieldy, or null.</summary>
        private static KeyFunctions? BeamPushDirection(Entity shieldy, HashSet<Point> lanes)
        {
            var c = new Vector2(shieldy.Hitbox.Center.X - 5, shieldy.Hitbox.Center.Y - 6);
            var t0 = BotNav.TileOf(c);
            if (lanes.Contains(t0))
            {
                return null; // already in a beam: let it burn
            }
            KeyFunctions? best = null;
            int bestN = 99;
            foreach (var (d, dir) in new[] { (new Point(1, 0), KeyFunctions.Right), (new Point(-1, 0), KeyFunctions.Left), (new Point(0, 1), KeyFunctions.Down), (new Point(0, -1), KeyFunctions.Up) })
            {
                // we have to be able to stand behind it (the side opposite the push) on floor
                var behind = new Point(t0.X - d.X, t0.Y - d.Y);
                if (!BotNav.Walkable(behind))
                {
                    continue;
                }
                for (int n = 1; n <= 4; n++)
                {
                    var t = new Point(t0.X + d.X * n, t0.Y + d.Y * n);
                    if (!BotNav.Walkable(t))
                    {
                        break;
                    }
                    if (lanes.Contains(t))
                    {
                        if (n < bestN)
                        {
                            bestN = n;
                            best = dir;
                        }
                        break;
                    }
                }
            }
            return best;
        }

        protected static IEnumerable<object> Fight(int timeoutFrames = 2400, bool soft = false, Func<Entity, bool> only = null)
        {
            if (!soft && only == null)
            {
                IgnoredEnemies.Clear(); // an explicit fight is for every enemy here, also the ones a soft fight on the way gave up on
            }
            yield return FightCore(timeoutFrames, soft, only);
            yield return DodgeLeftoverBullets();
        }

        /// <summary>A dead frog's last volley is still in the air: get out from under it before walking on (or standing about).</summary>
        private static IEnumerable<object> DodgeLeftoverBullets()
        {
            for (int i = 0; i < 150; i++)
            {
                var threats = PredictThreats(null).Where(t => t.Name == "FrogBullet").ToList();
                if (threats.Count == 0)
                {
                    yield break;
                }
                var pb = Player.Hitbox;
                bool safe = !threats.Any(t => Enumerable.Range(1, 20).Any(k => t.At[k] is Rectangle r && RectInflated(r, t.Margin).Intersects(pb)));
                if (safe)
                {
                    BotRunner.Status = "fight over: wait for its last bullets to come down";
                    yield return K();
                    continue;
                }
                BotRunner.Status = "fight over: out from under its last bullets";
                yield return SpaceTimeMove(new Vector2(pb.X, pb.Y), threats, useCache: false, survive: true) is KeyFunctions[] k ? k : K();
            }
        }

        private static Rectangle RectInflated(Rectangle r, int m)
        {
            r.Inflate(m, m);
            return r;
        }

        private static IEnumerable<object> FightCore(int timeoutFrames, bool soft, Func<Entity, bool> only)
        {
            int beamPushes = 0, ambushWaited = 0;
            bool dogDashing = false;
            int slFloat = -1000; string slPrev = ""; Point? slGoal = null;
            int dogAttackFrame = 0, dogSwingWait = 70, plantSwingWait = 60, frogLaunch = -100, annoyerStill = 0;
            Vector2 annoyerLastPos = Vector2.Zero;
            bool frogWasUp = false;
            Vector2 frogAim = Vector2.Zero;
            Vector2 dogTPos = Vector2.Zero;
            if (Enemies().Any(e => e.GetType().Name == "Seer"))
            {
                yield return SeerFight();
                yield break;
            }
            if (Enemies().Any(e => e.GetType().Name == "Red_Boss"))
            {
                yield return RedBossFight();
                yield break;
            }
            if (Enemies().Any(e => e is AnodyneSharp.Entities.Enemy.Apartment.SplitBoss))
            {
                yield return WatcherFight();
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
                if (emittersNear.Count > 0 && !BotRunner.NoAutoPlug)
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
            var ignoredTargets = IgnoredEnemies;
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
            int forcedFrames = 0, aggroFrames = 0, relocFrames = 0, ambushFrames = 0;
            Point? reloc = null;
            List<Point> heldPlan = null; // the standoff route being followed (re-searching every frame flips between clearance margins)
            int heldAge = 0;
            int planHold = 0; // frames during which a chosen route around a Shieldy is followed without re-deciding (no flip-flopping)
            float bestDist = float.MaxValue; // closest we have got to the current target (hits knock us about, so "still" is not enough)
            KeyFunctions[] safeKeep = null; int safeKeepLeft = 0;
            int noProgress = 0;
            Entity lastTarget = null;
            Entity ambushFor = null;
            var pmem = new PolicyMem();
            Entity routedFor = null;
            Vector2? annPrev = null;
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
                if (ignoredTargets.Count > 0)
                {
                    enemies = enemies.Where(e => !ignoredTargets.Contains(e)).ToList();
                    if (enemies.Count == 0)
                    {
                        yield break; // what is left cannot be reached from here (across water, behind a wall): a soft fight leaves it
                    }
                }
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
                    // a Slasher takes ~3.5s per hit (float, wind-up, cut): waiting out its cycle is not "no progress"
                    noKill = enemies.Any(en => en.GetType().Name == "Slasher") && noKill > 0 && noKill < 900 ? Math.Min(noKill + 1, 100) : noKill + 1;
                }
                if (forcedFrames > 0 && --forcedFrames == 0)
                {
                    forced = null;
                }
                if (aggroFrames > 0)
                {
                    aggroFrames--;
                }
                if (soft && noKill >= 220 && (committed ?? lastTarget) is Entity unreachable && !ignoredTargets.Contains(unreachable))
                {
                    committed = unreachable;
                    // Nothing waiting-on-purpose (those reset the count) and nothing dying for this long: this one is out of reach.
                    if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                    {
                        System.Console.Error.WriteLine($"  soft fight: giving up on the unreachable {committed.GetType().Name}@{committed.Hitbox.X},{committed.Hitbox.Y}");
                    }
                    ignoredTargets.Add(committed);
                    committed = null;
                    noKill = 1;
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
                if (stillFrames > 100 && lastTarget != null && lastTarget.GetType().Name == "Annoyer" && !ignoredTargets.Contains(lastTarget))
                {
                    // Walking up to a sleeping Annoyer and not getting anywhere (something between us): it is not worth it, leave it asleep.
                    if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                    {
                        System.Console.Error.WriteLine($"  fight: cannot get to the Annoyer at {lastTarget.Hitbox.X},{lastTarget.Hitbox.Y}: leaving it");
                    }
                    ignoredTargets.Add(lastTarget);
                    committed = null;
                    stillFrames = 0;
                    continue;
                }
                if (stillFrames > 150 && lastTarget is AnodyneSharp.Entities.Enemy.Crowd.Frog)
                {
                    stillFrames = 0; // waiting for a frog's volley to pass is the plan, not being wedged
                }
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
                    yield return WalkableAhead(unstickKey, 4) ? K(unstickKey) : K(); // never shake loose into a pit
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
                {
                    // Whatever the plan (even a forced one): a Slime that is right beside us is the one that touches us next.
                    var bx = Player.Hitbox;
                    int GapTo(Entity e) => Math.Max(Math.Max(e.Hitbox.Left - bx.Right, bx.Left - e.Hitbox.Right), Math.Max(e.Hitbox.Top - bx.Bottom, bx.Top - e.Hitbox.Bottom));
                    var besideUs = enemies.Where(e => e.GetType().Name == "Slime" && GapTo(e) < 40).OrderBy(GapTo).FirstOrDefault();
                    if (besideUs != null)
                    {
                        target = besideUs;
                    }
                }
                if (enemies.FirstOrDefault(e => e.GetType().Name == "Annoyer" && (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - me).Length() < (forced == null ? 80 : 40)) is Entity nearAnnoyer)
                {
                    target = nearAnnoyer; // one hit kills it and it is the one that comes at us: deal with it before a patroller
                }
                if (enemies.FirstOrDefault(e => e.GetType().Name == "GasGuy" && Grid == new Point((int)(e.Hitbox.Center.X / 160), (int)(e.Hitbox.Center.Y / 160))) is Entity gasAny
                    && (enemies.Any(e => e.GetType().Name == "Frog") || BotRunner.Entities().Any(e => e.exists && e is AnodyneSharp.Entities.Enemy.Apartment.OnSightDashTrap))
                    && GasWaitSpot() is Point gasSpot)
                {
                    // A Gas Guy comes to us (through walls, at 12px/s), so going after it among frogs and dash traps only collects their hits
                    // (and its gas turns our keys round, which walks us into them): wait for it where neither reaches, facing it, and swing
                    // when it is in front of us. A hit throws it back; it comes again until its third.
                    BotRunner.Status = $"fight: wait for the Gas Guy at {gasSpot}";
                    noKill = 0; // waiting on purpose
                    BotRunner.NotStuck();
                    var gsc = BotNav.CentreOf(gasSpot);
                    if ((Pos - gsc).Length() > 3f)
                    {
                        yield return GoTo(gsc.X, gsc.Y);
                        yield return WalkTo(gsc.X, gsc.Y, tol: 1.5f, timeout: 60);
                        continue;
                    }
                    float gdx = gasAny.Hitbox.Center.X - Player.Hitbox.Center.X, gdy = gasAny.Hitbox.Center.Y - Player.Hitbox.Center.Y;
                    var gface = Math.Abs(gdx) > Math.Abs(gdy) ? (gdx < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (gdy < 0 ? KeyFunctions.Up : KeyFunctions.Down);
                    if (Player.facing != FacingOf(gface))
                    {
                        yield return K(gface);
                        continue;
                    }
                    bool inReach = Math.Abs(gdx) > Math.Abs(gdy) ? Math.Abs(gdx) < 24 && Math.Abs(gdy) < 16 : Math.Abs(gdy) < 26 && Math.Abs(gdx) < 14;
                    if (inReach && i - lastSwing > 12)
                    {
                        lastSwing = i;
                        yield return K(KeyFunctions.Accept);
                        continue;
                    }
                    yield return K();
                    continue;
                }
                if (enemies.Where(e => e.GetType().Name == "GasGuy").OrderBy(e => (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - me).Length()).FirstOrDefault() is Entity nearGas &&
                    (new Vector2(nearGas.Hitbox.Center.X, nearGas.Hitbox.Center.Y) - me).Length() < 40)
                {
                    target = nearGas; // it homes in on us through anything: the one about to touch us comes first
                }
                if (forced == null && enemies.Any(e => e.GetType().Name == "Slime") && (target.GetType().Name == "Slime" ||
                    enemies.Where(e => e.GetType().Name == "Slime").Any(e => Math.Max(Math.Max(e.Hitbox.Left - Player.Hitbox.Right, Player.Hitbox.Left - e.Hitbox.Right), Math.Max(e.Hitbox.Top - Player.Hitbox.Bottom, Player.Hitbox.Top - e.Hitbox.Bottom)) < 44)))
                {
                    // (a Slime that has come up beside us comes first whatever we were doing: a plant or a patroller is not what touches us now)
                    // Slimes drift: whichever is nearest is the one that can touch us, so it is always the one to deal with (not the one chosen earlier).
                    var meBox = Player.Hitbox;
                    int SlimeGap(Entity e) => Math.Max(Math.Max(e.Hitbox.Left - meBox.Right, meBox.Left - e.Hitbox.Right), Math.Max(e.Hitbox.Top - meBox.Bottom, meBox.Top - e.Hitbox.Bottom));
                    var nearestSlime = enemies.Where(e => e.GetType().Name == "Slime").OrderBy(SlimeGap).First();
                    if (SlimeGap(nearestSlime) < (target.GetType().Name == "Slime" ? 60 : 44))
                    {
                        target = nearestSlime; // within reach of us: it is the one to deal with
                    }
                }
                if (target != routedFor)
                {
                    routedFor = target;
                    // An enemy walled off from where we stand (the slime in the pocket whose door is on the other screen): the fight is
                    // there, so go round to the side it can be reached from first.
                    var tTile = BotNav.TileOf(new Vector2(target.Hitbox.Center.X - 5, target.Hitbox.Center.Y - 6));
                    var mine = FloodRegion(BotNav.TileOf(Pos), 900);
                    bool reachableHere = mine.Contains(tTile) || new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) }.Any(o => mine.Contains(new Point(tTile.X + o.X, tTile.Y + o.Y)));
                    if (!reachableHere && only == null && target.GetType().Name == "Slime")
                    {
                        var theirs = FloodRegion(tTile, 900);
                        BotRunner.Status = $"go round to {target.GetType().Name}";
                        int farEnough = theirs.Any(t => Math.Max(Math.Abs(t.X - tTile.X), Math.Abs(t.Y - tTile.Y)) >= 3) ? 3 : 2; // a small pocket has nothing farther than 2 tiles from it
                        var sideLanes = BotNav.LaneTiles(); // never end the walk standing in a beam
                        yield return GoToWhere(t => theirs.Contains(t) && !sideLanes.Contains(t) && Math.Max(Math.Abs(t.X - tTile.X), Math.Abs(t.Y - tTile.Y)) >= farEnough, "the enemy's side");
                        startGrid = Grid;
                        routedFor = null;
                        committed = null;
                        continue;
                    }
                }
                var c = new Vector2(target.Hitbox.Center.X, target.Hitbox.Center.Y);
                float dx = c.X - me.X, dy = c.Y - me.Y;
                BotRunner.Status = $"fight {target.GetType().Name} ({enemies.Count} left{(only != null ? ", only" : "")}{(soft ? ", soft" : "")}) d=({dx:0},{dy:0})";
                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_SHOTS") is string shotDir && i % 10 == 0 && i >= int.Parse(Environment.GetEnvironmentVariable("ANODYNE_BOT_SHOTS_FROM") ?? "0") && i < int.Parse(Environment.GetEnvironmentVariable("ANODYNE_BOT_SHOTS_FROM") ?? "0") + 300)
                {
                    AnodyneSharp.Drawing.SpriteDrawer.SaveRender($"{shotDir}/fight_{i:0000}.png");
                }
                if (i % 120 == 0 && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    System.Console.Error.WriteLine($"fight {target.GetType().Name} f{i} me={me} target={c} d=({dx:0},{dy:0}) hp={GlobalState.CUR_HEALTH} enemies={enemies.Count}");
                }
                // Whatever the plan is, a patrolling Shieldy that is about to walk into where we stand comes first: clear of it, then carry on.
                if (!(Player.broom != null && Player.broom.exists))
                {
                    // (a bouncing dash trap too: it sweeps its lane at 80px/s, so look a little further ahead for it)
                    var walkers = PredictThreats(null).Where(t => t.Source != null && t.Source.GetType().Name is "Shieldy" or "BounceDashTrap" && !t.Stationary).ToList();
                    if (walkers.Count > 0 && PlanHitFrameStanding(walkers.Any(t => t.Name == "BounceDashTrap") ? 24 : 14, KeyFunctions.Right, 0, walkers) != int.MaxValue)
                    {
                        // (clear of everything else too where there is a way: stepping out of a trap's lane into a slime is no better)
                        var clear = SpaceTimeMove(new Vector2(Player.Hitbox.X, Player.Hitbox.Y), PredictThreats(null), useCache: false, survive: true)
                            ?? SpaceTimeMove(new Vector2(Player.Hitbox.X, Player.Hitbox.Y), walkers, useCache: false, survive: true);
                        if (clear != null && clear.Length > 0)
                        {
                            BotRunner.Status = "fight/step clear of a patroller";
                            yield return K(clear);
                            continue;
                        }
                    }
                }
                // A Slasher drifts after us at 20px/s, winds up for 0.5s (still tracking) and then cuts once: a thin strip in front of it (long, 36px
                // beyond its body) when we are further than 36px, a strip across its front otherwise. The cut is only there for 0.25s, fixed where it
                // was made. Neither strip reaches a spot diagonal to it (30px off on both axes), so during the wind-up step out to such a spot,
                // and hit it while it floats or after its cut has gone.
                // A Gas Guy on top of us hurts every time our blink runs out (it homes in, and nothing else here keeps off it): it comes
                // first, whatever we were after (a TeleGuy far off kept us standing there while it took 7 hearts).
                if (target.GetType().Name != "GasGuy" && Map == "APARTMENT"
                    && enemies.FirstOrDefault(e => e.GetType().Name == "GasGuy" && (e.Center - Player.Center).Length() < 30) is Entity gasNear)
                {
                    target = gasNear;
                }
                SwingTarget = target;
                SwingTargetFrame = BotRunner.Frame;
                if (target is AnodyneSharp.Entities.Enemy.Apartment.TeleGuy)
                {
                    yield return KillTeleGuy(target);
                    if (target.exists)
                    {
                        IgnoredEnemies.Add(target);
                    }
                    continue;
                }
                if (target is AnodyneSharp.Entities.Enemy.Apartment.Silverfish && PuzzleGateShut())
                {
                    IgnoredEnemies.Add(target); // the buttons of a puzzle gate want it alive (see SilverfishOnButton)
                    continue;
                }
                if (target is AnodyneSharp.Entities.Enemy.Apartment.Rat)
                {
                    if (BotRunner.Entities().Any(e => e.exists && e is AnodyneSharp.Entities.Gadget.Button))
                    {
                        IgnoredEnemies.Add(target); // a rat in a room with buttons is likely the puzzle's tool (see RatOnButton): spare it
                        continue;
                    }
                    yield return KillRat(target);
                    if (target.exists)
                    {
                        IgnoredEnemies.Add(target); // no ambush spot worked: leave it to the walker's dodging
                    }
                    continue;
                }
                if (target.GetType().Name != "Slasher" && Map == "APARTMENT" && enemies.Any(e => e.GetType().Name == "Slasher"))
                {
                    // Whatever we are after, a Slasher near us about to cut comes first: the dodge below plans round it.
                    SlasherThreats(); // (keeps each one's phase up to date)
                    var pcNow = Player.Center;
                    if (enemies.Where(e => e.GetType().Name == "Slasher" && (e.Center - pcNow).Length() < 72)
                            .FirstOrDefault(e => SlasherCutIn(e) is int cin && cin >= 0 && cin <= 50 || e.SubEntities().Any(sub => sub.exists && sub.visible && sub.GetType().Name.EndsWith("Slash"))) is Entity soon)
                    {
                        target = soon;
                    }
                }
                if (target.GetType().Name == "Slasher")
                {
                    // All of them at once (one floats while another winds up): plan round every coming cut whenever one is near.
                    var slThreats = SlasherThreats();
                    var meTL = new Vector2(Player.Hitbox.X, Player.Hitbox.Y);
                    if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                    {
                        System.Console.Error.WriteLine($"  slasher f{BotRunner.Frame} me={Player.Hitbox} safe={SafeAtExactThreats(meTL, slThreats, 45)} " + string.Join(" ", BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Slasher").Select(e => $"S{e.Hitbox}{e.CurAnimName}{e.facing}")) + " | " + string.Join(" ", slThreats.Select(t => t.Name + (Array.FindIndex(t.At, r => r != null)) + t.At.FirstOrDefault(r => r != null))));
                    }
                    {
                        // Our walk one way (held: re-planning every frame dithers while it drifts on top of us) and each winding-up Slasher's
                        // drift after us, frame by frame; its cut is made from where it and we are then, so it is checked exactly.
                        var slList = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "Slasher").ToList();
                        var cutsNow = slThreats.Where(t => t.Name == "Slasher cut").ToList();
                        var vcOff = Player.VisualCenter - Player.Position;
                        var hbOff = new Vector2(Player.Hitbox.X, Player.Hitbox.Y) - Player.Position;
                        var winding = slList.Select(e => (e, left: SlasherCutIn(e))).Where(w => w.left >= 0 && w.left <= 64).ToList();
                        Vector2 Vel(KeyFunctions[] keys)
                        {
                            var v0 = Vector2.Zero;
                            foreach (var kf in keys ?? new KeyFunctions[0])
                            {
                                v0 += DirVector(kf);
                            }
                            return v0 == Vector2.Zero ? v0 : Vector2.Normalize(v0) * WalkPxPerFrame;
                        }
                        // (then: a second leg after the first walk, round a corner, instead of standing)
                        (bool clear, Vector2 end, int hitAt) Sim(KeyFunctions[] runDir, int walk = 66, KeyFunctions[] then = null)
                        {
                            var v = Vel(runDir);
                            var v2 = Vel(then);
                            var q = Player.Position;
                            var sp = winding.Select(w => w.e.Position).ToArray();
                            // (what hurts to walk into on the way: a Gas Guy, drifting after us at 12px/s on each axis; its cloud only swaps the
                            // d-pad. A TeleGuy standing about is left out: counting it boxes us in with the cuts more often than it saves a touch)
                            var bodies = new List<Rectangle>();
                            var gasBoxes = BotRunner.Entities().Where(e => e.exists && e.GetType().Name == "GasGuy").Select(e => e.Hitbox).ToList();
                            var gasOff = gasBoxes.Select(_ => Vector2.Zero).ToArray();
                            var cut = new Rectangle?[winding.Count];
                            for (int k = 1; k <= 66; k++)
                            {
                                var nq = q + (k <= walk ? v : v2);
                                if (CornersWalkable(new Rectangle((int)(nq.X + hbOff.X), (int)(nq.Y + hbOff.Y), Player.Hitbox.Width, Player.Hitbox.Height))
                                    && BotNav.TileOf(nq + hbOff).X / 10 == Grid.X && BotNav.TileOf(nq + hbOff).Y / 10 == Grid.Y)
                                {
                                    q = nq;
                                }
                                var me2 = new Rectangle((int)(q.X + hbOff.X), (int)(q.Y + hbOff.Y), Player.Hitbox.Width, Player.Hitbox.Height);
                                if (k <= 30 && bodies.Any(b => b.Intersects(me2)))
                                {
                                    return (false, q, k);
                                }
                                for (int g = 0; g < gasBoxes.Count && k <= 40; g++)
                                {
                                    var gc = new Vector2(gasBoxes[g].Center.X, gasBoxes[g].Center.Y) + gasOff[g];
                                    var mc = new Vector2(me2.Center.X, me2.Center.Y);
                                    AnodyneSharp.Utilities.MathUtilities.MoveTo(ref gasOff[g].X, gasOff[g].X + (mc.X - gc.X), 12);
                                    AnodyneSharp.Utilities.MathUtilities.MoveTo(ref gasOff[g].Y, gasOff[g].Y + (mc.Y - gc.Y), 12);
                                    var gb = gasBoxes[g];
                                    gb.Offset((int)gasOff[g].X, (int)gasOff[g].Y);
                                    gb.Inflate(1, 1);
                                    if (gb.Intersects(me2))
                                    {
                                        return (false, q, k);
                                    }
                                }
                                for (int w = 0; w < winding.Count; w++)
                                {
                                    if (k < winding[w].left)
                                    {
                                        AnodyneSharp.Utilities.MathUtilities.MoveTo(ref sp[w].X, q.X, 20);
                                        AnodyneSharp.Utilities.MathUtilities.MoveTo(ref sp[w].Y, q.Y, 20);
                                    }
                                    else if (k == Math.Max(1, winding[w].left))
                                    {
                                        cut[w] = SlasherCutRect(sp[w], q, q + vcOff);
                                    }
                                    if (cut[w] is Rectangle cr && k <= winding[w].left + 16)
                                    {
                                        cr.Inflate(2, 2);
                                        if (cr.Intersects(me2))
                                        {
                                            return (false, q, k);
                                        }
                                    }
                                }
                                foreach (var t in cutsNow)
                                {
                                    if (k <= 16 && t.At[k] is Rectangle r && r.Intersects(me2))
                                    {
                                        return (false, q, k);
                                    }
                                }
                            }
                            return (true, q, 99);
                        }
                        // (Apartments only: in Red Cave's narrow ledges a straight run is rarely clear, and the path-planned spot off both
                        // strips below costs fewer hits there; TODO: one way that does well in both)
                        if ((winding.Count > 0 || cutsNow.Count > 0) && Map == "APARTMENT")
                        {
                            bool stayOk = Sim(StActions[0]).clear;
                            // (the one we are after: towards it while it still floats a while, else away)
                            bool strikeable = SlasherCutIn(target) > 64;
                            if (!stayOk)
                            {
                                // the shortest dodge that works (walk that way for a while, then stand): it keeps us close for the next strike
                                KeyFunctions[] best = null;
                                float bestScore = float.MinValue;
                                foreach (var runDir in StActions.Skip(1))
                                {
                                    foreach (int walk in new[] { 10, 18, 28, 40, 66 })
                                    {
                                        var (clear, q, hitAt) = Sim(runDir, walk);
                                        var qc = q + vcOff;
                                        // (no clear way: the one that is hit latest, where it may still turn out clear as things move)
                                        float score = clear ? 1000f - walk - 0.5f * (target.Center - qc).Length() : hitAt * 8f + slList.Min(e => (e.Center - qc).Length()) * 0.2f;
                                        score += _slasherRun != null && runDir.SequenceEqual(_slasherRun) ? 6f : 0f;
                                        if (score > bestScore)
                                        {
                                            bestScore = score;
                                            best = runDir;
                                        }
                                        if (clear)
                                        {
                                            break;
                                        }
                                    }
                                }
                                if (bestScore < 500)
                                {
                                    // no straight run is clear: try one turn on the way (along a wall, round a corner)
                                    foreach (var runDir in StActions.Skip(1))
                                    {
                                        foreach (int walk in new[] { 8, 16, 26 })
                                        {
                                            foreach (var then in StActions.Skip(1))
                                            {
                                                if (then.SequenceEqual(runDir))
                                                {
                                                    continue;
                                                }
                                                var (clear, q, _) = Sim(runDir, walk, then);
                                                if (!clear)
                                                {
                                                    continue;
                                                }
                                                float score = 900f - walk - 0.5f * (target.Center - (q + vcOff)).Length() + (_slasherRun != null && runDir.SequenceEqual(_slasherRun) ? 6f : 0f);
                                                if (score > bestScore)
                                                {
                                                    bestScore = score;
                                                    best = runDir;
                                                }
                                            }
                                        }
                                    }
                                }
                                _slasherRun = best;
                                BotRunner.Status = $"fight/slasher: run from the coming cuts ({string.Join("+", best)}{(bestScore >= 500 ? "" : ", no clear way")})";
                                yield return K(best);
                                continue;
                            }
                        }
                    }
                    _slasherRun = null;
                    string anim = target.CurAnimName ?? "";
                    bool cutting = anim.StartsWith("attack") && target.SubEntities().Any(sub => sub.exists);
                    bool floating = anim.StartsWith("float");
                    if (floating && !slPrev.StartsWith("float"))
                    {
                        slGoal = null;
                        slFloat = i; // the float lasts 1.4s: strike in the first second, then get clear before the wind-up ends
                    }
                    slPrev = anim;
                    if (anim.StartsWith("warning") || cutting || (floating && i - slFloat >= SlasherStrikeFrames))
                    {
                        var sc = new Vector2(target.Hitbox.Center.X, target.Hitbox.Center.Y);
                        var pcen = new Vector2(Player.Hitbox.Center.X, Player.Hitbox.Center.Y);
                        float ddx = pcen.X - sc.X, ddy = pcen.Y - sc.Y;
                        bool safe = Math.Min(Math.Abs(ddx), Math.Abs(ddy)) >= 30 || Math.Sqrt(ddx * ddx + ddy * ddy) >= 70;
                        BotRunner.Status = safe ? "fight/slasher: clear of the cut" : "fight/slasher: out of line of the cut";
                        if (safe)
                        {
                            yield return K();
                            continue;
                        }
                        // the nearest tile (by path) that is out of both strips; the greedy step below is the fallback
                        var hereT = BotNav.TileOf(Pos);
                        if (slGoal == null)
                        {
                            // The Slasher drifts 10px closer in the wind-up (on both axes), so aim for ~48px along the axis it faces and 24px off it:
                            // it ends up beyond 36px (a long cut, 10px wide, down its own row) and still 12px off that row.
                            var cands = new HashSet<Point>();
                            foreach (var (ax, ay) in new[] { (1, 1), (1, -1), (-1, 1), (-1, -1) })
                            {
                                foreach (var (lx, ly) in new[] { (48, 24), (24, 48), (64, 0), (0, 64) })
                                {
                                    var ct = BotNav.TileOf(new Vector2(sc.X + ax * lx - 5, sc.Y + ay * ly - 6));
                                    if (BotNav.Walkable(ct) && ct.X / 10 == Grid.X && ct.Y / 10 == Grid.Y) // (not off the screen: that ends the fight with it still alive)
                                    {
                                        cands.Add(ct);
                                    }
                                }
                            }
                            // (round the others in the room: an idle TeleGuy hurts to touch, and a Gas Guy homes in)
                            var others = OthersBySlashers();
                            cands.ExceptWith(others);
                            BotNav.Avoid = others;
                            var found = BotNav.FindPath(hereT, t => cands.Contains(t), hereT);
                            BotNav.Avoid = null;
                            if (found != null && found.Count > 0)
                            {
                                slGoal = found[found.Count - 1]; // chosen once per wind-up, so the dash does not dither as the Slasher drifts
                            }
                        }
                        BotNav.Avoid = OthersBySlashers();
                        var goalPath = slGoal.HasValue && slGoal.Value != hereT ? BotNav.FindPath(hereT, t => t == slGoal.Value, slGoal.Value) : null;
                        BotNav.Avoid = null;
                        if (goalPath != null && goalPath.Count > 0)
                        {
                            var nc2 = BotNav.CentreOf(goalPath[0]) + new Vector2(5, 6);
                            var me2 = Pos + new Vector2(5, 6);
                            float qx = nc2.X - me2.X, qy = nc2.Y - me2.Y;
                            yield return K(Math.Abs(qx) > 2f ? (qx < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (qy < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                            continue;
                        }
                        var kx = ddx >= 0 ? KeyFunctions.Right : KeyFunctions.Left;
                        var ky = ddy >= 0 ? KeyFunctions.Down : KeyFunctions.Up;
                        var order = Math.Abs(ddx) < Math.Abs(ddy)
                            ? new[] { kx, ky, ddx >= 0 ? KeyFunctions.Left : KeyFunctions.Right, ddy >= 0 ? KeyFunctions.Up : KeyFunctions.Down }
                            : new[] { ky, kx, ddy >= 0 ? KeyFunctions.Up : KeyFunctions.Down, ddx >= 0 ? KeyFunctions.Left : KeyFunctions.Right };
                        var pick = order.FirstOrDefault(k => WalkableAhead(k, 4));
                        yield return pick == default ? K() : K(pick);
                        continue;
                    }
                    // striking: walk the path to it (it can be stuck behind a corner while we stand in a straight line to it)
                    var slHere = BotNav.TileOf(Pos);
                    var slCen = new Vector2(target.Hitbox.Center.X, target.Hitbox.Center.Y);
                    var slNear = new HashSet<Point>();
                    for (int ox = -16; ox <= 16; ox += 8)
                    {
                        for (int oy = -16; oy <= 16; oy += 8)
                        {
                            var nt = BotNav.TileOf(new Vector2(slCen.X + ox - 5, slCen.Y + oy - 6));
                            if (BotNav.Walkable(nt))
                            {
                                slNear.Add(nt);
                            }
                        }
                    }
                    // (already in broom reach, lined up with it: strike from here; walking on to a tile beside it dithered on a tile's edge
                    // 21px out, never swinging, while two stacked Slashers cut us every round)
                    var tb = target.Hitbox;
                    var mb = Player.Hitbox;
                    int gapX = Math.Max(tb.Left - mb.Right, mb.Left - tb.Right), gapY = Math.Max(tb.Top - mb.Bottom, mb.Top - tb.Bottom);
                    bool inReach = (gapX <= 10 && gapY <= -4) || (gapY <= 10 && gapX <= -4);
                    var toSlS = slCen - (Pos + new Vector2(5, 6));
                    KeyFunctions? straightIn = Math.Abs(toSlS.Y) < 6 ? (toSlS.X > 0 ? KeyFunctions.Right : KeyFunctions.Left)
                        : Math.Abs(toSlS.X) < 6 ? (toSlS.Y > 0 ? KeyFunctions.Down : KeyFunctions.Up) : null;
                    if (!inReach && straightIn is KeyFunctions sIn && WalkableAhead(sIn, 4) && enemies.All(e => e.GetType().Name == "Slasher"))
                    {
                        // lined up but out of reach: straight in (a tile "beside it" can still be 20px short of the broom). Only with
                        // Slashers alone: with a TeleGuy or a Gas Guy about the way straight in ran into them.
                        BotRunner.Status = "fight/slasher: straight in";
                        yield return K(sIn);
                        continue;
                    }
                    if (!inReach && (target.Position - Pos).Length() > 20 && !slNear.Contains(slHere))
                    {
                        var approach = BotNav.FindPath(slHere, t => slNear.Contains(t), slHere);
                        // (a way round that starts by backing off from a Slasher close by and in a straight line is no way in: the
                        // strike below walks straight at it. Stepping back and forth between the two never closed in.)
                        if (approach != null && approach.Count > 0)
                        {
                            var stepTo = BotNav.CentreOf(approach[0]) - Pos;
                            var toSl = slCen - (Pos + new Vector2(5, 6));
                            if (Vector2.Dot(stepTo, toSl) < 0 && toSl.Length() < 48 && (Math.Abs(toSl.X) < 6 || Math.Abs(toSl.Y) < 6))
                            {
                                approach = null;
                            }
                        }
                        if (approach != null && approach.Count > 0)
                        {
                            BotRunner.Status = "fight/slasher: round to it";
                            if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                            {
                                System.Console.Error.WriteLine($"  slround f{i} me={mb} sl={tb} gap={gapX},{gapY}");
                            }
                            var ncA = BotNav.CentreOf(approach[0]) + new Vector2(5, 6);
                            var meA = Pos + new Vector2(5, 6);
                            float apx = ncA.X - meA.X, apy = ncA.Y - meA.Y;
                            yield return K(Math.Abs(apx) > 2f ? (apx < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (apy < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                            continue;
                        }
                    }
                }
                // A Dog notices us inside its 96px square, barks for a second (standing still), then runs three one-second dashes: each heads
                // for a point beside where we stood when the attack began (44px left, 36px right, 44px left, on the row 8px above us), at
                // 66/84/84px/s, for exactly one second whether it gets there or not; then it paces again. The whole attack is therefore
                // known in advance: predict it, keep out of its way (the planner), and hit it while it barks or paces.
                if (target.GetType().Name == "Dog")
                {
                    var dg = target.Hitbox;
                    float dspeed = Math.Abs(target.velocity.X) + Math.Abs(target.velocity.Y);
                    if (noKill > 0 && noKill < 1200)
                    {
                        noKill = 1; // three hits take three barks and three charges: waiting out the charges is not "no progress"
                    }
                    if (i % 15 == 0 && Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                    {
                        var dhf0 = target.GetType().GetField("_health", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
                        System.Console.Error.WriteLine($"  dogstate f{i} health={dhf0?.GetValue(target)} dashing={dogDashing} P={Player.Hitbox} dog={dg} v={target.velocity} status={BotRunner.Status}");
                    }
                    if (dspeed > 40f && !dogDashing)
                    {
                        dogDashing = true;
                        dogAttackFrame = 0;
                        dogTPos = Player.Position; // the position the dog aims at (it samples ours as the attack begins)
                    }
                    if (dogDashing)
                    {
                        dogAttackFrame++;
                        // Its attack is over when it leaves the Attack state (then it barks a second before the next one, which is the time
                        // to hit it: guessing the end from the dash speed lost most of that second). 190 frames when that cannot be read.
                        var dogInAttack = DogAttacking(target);
                        if (dogInAttack == false || (dogInAttack == null && dogAttackFrame > 190) || dogAttackFrame > 260)
                        {
                            dogDashing = false;
                        }
                    }
                    if (dogDashing)
                    {
                        var path = DogPath(target, dogAttackFrame, dogTPos);
                        var dogThreat = new Threat { Name = "Dog", Margin = 5, Source = target };
                        for (int f = 0; f <= Horizon; f++)
                        {
                            dogThreat.At[f] = path(f);
                        }
                        BotRunner.Status = "fight/dog: out of the way of its dashes";
                        var pbd = Player.Hitbox;
                        // (and of anything else about: a frog's bullets coming down)
                        var dogAll = PredictThreats(target).Where(t => t.Source != target && t.Name != "Dog").Append(dogThreat).ToList();
                        var away = SpaceTimeMove(new Vector2(pbd.X, pbd.Y), dogAll, useCache: false, survive: true)
                            ?? SpaceTimeMove(new Vector2(pbd.X, pbd.Y), new List<Threat> { dogThreat }, useCache: false, survive: true);
                        if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                        {
                            System.Console.Error.WriteLine($"  dog f{i} att={dogAttackFrame} P={pbd} dog={dg} v={target.velocity} plan={(away == null ? "none" : string.Join("+", away))}");
                        }
                        yield return away == null || away.Length == 0 ? K() : K(away);
                        continue;
                    }
                    // barking or pacing: close in to a standoff ~5px short of its box (touching it hurts), line up, face it and swing
                    var pbs = Player.Hitbox;
                    float sdx = dg.Center.X - pbs.Center.X, sdy = dg.Center.Y - pbs.Center.Y;
                    if (Math.Sqrt(sdx * sdx + sdy * sdy) < 60)
                    {
                        bool vert = Math.Abs(sdy) >= Math.Abs(sdx);
                        var faceKey = vert ? (sdy > 0 ? KeyFunctions.Down : KeyFunctions.Up) : (sdx > 0 ? KeyFunctions.Right : KeyFunctions.Left);
                        var backKey = vert ? (sdy > 0 ? KeyFunctions.Up : KeyFunctions.Down) : (sdx > 0 ? KeyFunctions.Left : KeyFunctions.Right);
                        int sgap = vert ? (sdy > 0 ? dg.Top - pbs.Bottom : pbs.Top - dg.Bottom) : (sdx > 0 ? dg.Left - pbs.Right : pbs.Left - dg.Right);
                        float cross = vert ? sdx : sdy;
                        var wantFacing = faceKey == KeyFunctions.Up ? Facing.UP : faceKey == KeyFunctions.Down ? Facing.DOWN : faceKey == KeyFunctions.Left ? Facing.LEFT : Facing.RIGHT;
                        dogSwingWait++;
                        BotRunner.Status = "fight/dog: standoff";
                        if (sgap < 3)
                        {
                            yield return K(backKey);
                            continue;
                        }
                        if (Math.Abs(cross) > 4)
                        {
                            var slide = vert ? (cross > 0 ? KeyFunctions.Right : KeyFunctions.Left) : (cross > 0 ? KeyFunctions.Down : KeyFunctions.Up);
                            yield return K(WalkableAhead(slide, 4) ? slide : faceKey);
                            continue;
                        }
                        if (Player.facing != wantFacing)
                        {
                            yield return K(sgap >= 5 ? faceKey : backKey); // turning nudges ~1px towards it
                            continue;
                        }
                        if (sgap > 11)
                        {
                            yield return K(faceKey);
                            continue;
                        }
                        if (dogSwingWait >= 70 && !(Player.broom != null && Player.broom.exists))
                        {
                            dogSwingWait = 0;
                            if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                            {
                                var dhf = target.GetType().GetField("_health", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
                                System.Console.Error.WriteLine($"  dog SWING f{i} health={dhf?.GetValue(target)} gap={sgap} cross={cross:0} face={Player.facing}");
                            }
                            yield return K(KeyFunctions.Accept);
                            continue;
                        }
                        yield return K();
                        continue;
                    }
                }
                // A Burst Plant cannot hurt by touch (the player walks straight through it); its bullets can. They spawn on the plant, one aimed at
                // us and seven lobbed round it, and are only dangerous while they fly. So: while any are in the air stay well away; when the air is
                // clear go in to a standoff beside it, line up, face it and swing (two hits, a second apart), and back out when it fires.
                if (target.GetType().Name is "Burst_Plant" or "Frog") // (a Frog also hurts by touch: the standoff keeps 3px clear)
                {
                    var pl = target.Hitbox;
                    var pbp = Player.Hitbox;
                    bool bulletsUp = BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "BurstBullet");
                    float pdist = (new Vector2(pl.Center.X, pl.Center.Y) - new Vector2(pbp.Center.X, pbp.Center.Y)).Length();
                    if (noKill > 0 && noKill < 1200 && (bulletsUp || pdist < 50))
                    {
                        noKill = 1; // waiting out its bullets / working at it from the standoff is progress; walking at it for ever is not
                    }
                    bool isFrog = target.GetType().Name == "Frog";
                    if (isFrog && (Pos - _frogStillPos).Length() > 3f)
                    {
                        _frogStillPos = Pos;
                        _frogStillSince = BotRunner.Frame;
                    }
                    else if (isFrog && BotRunner.Frame - _frogStillSince > 240)
                    {
                        // Four seconds rooted to one spot waiting for a volley that does not come (out of its reach, or a step in that is never
                        // safe): step back from it a little and let the plan start over from there.
                        var awayF = Math.Abs(pbp.Center.X - pl.Center.X) >= Math.Abs(pbp.Center.Y - pl.Center.Y)
                            ? (pbp.Center.X >= pl.Center.X ? KeyFunctions.Right : KeyFunctions.Left)
                            : (pbp.Center.Y >= pl.Center.Y ? KeyFunctions.Down : KeyFunctions.Up);
                        _frogStillSince = BotRunner.Frame;
                        if (soft && pdist >= 110)
                        {
                            // far from it and going nowhere (back at the room's door after a fall into its pit): give the fight back to the
                            // route, which walks round to the pit again
                            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                            {
                                System.Console.Error.WriteLine($"  soft fight: stuck {pdist:0}px from the Frog at {pl.X},{pl.Y}: back to the route");
                            }
                            yield break;
                        }
                        BotRunner.Status = "fight/frog: unstick";
                        yield return Hold(8, WalkableAhead(awayF, 8) ? awayF : (awayF is KeyFunctions.Left or KeyFunctions.Right ? KeyFunctions.Up : KeyFunctions.Left));
                        continue;
                    }
                    if (isFrog && pdist < 110)
                    {
                        // A frog fires again the moment its bullets land for as long as we are inside 64px: there is no gap to wait for. Go in at
                        // once, stepping round the bullets already flying, and swing from the standoff (touching it hurts too).
                        noKill = 1;
                        yield return FrogStrikeKeys(target, ref plantSwingWait);
                        continue;
                    }
                    if (!isFrog)
                    {
                        // The standoff and the waiting stand still, and a slime or an Annoyer about the plant does not wait: clear of them first.
                        var all = PredictThreats(target);
                        var others = all.Where(t => t.Source != null && !t.Stationary && !t.Name.Contains("Bullet") && !t.Name.Contains("Burst")).ToList();
                        if (others.Count > 0 && PlanHitFrameStanding(16, KeyFunctions.Right, 0, others) != int.MaxValue)
                        {
                            var clear = SpaceTimeMove(new Vector2(pbp.X, pbp.Y), all, useCache: false, survive: true)
                                ?? SpaceTimeMove(new Vector2(pbp.X, pbp.Y), others, useCache: false, survive: true);
                            if (clear != null && clear.Length > 0)
                            {
                                BotRunner.Status = "fight/plant: clear of the others about it";
                                yield return K(clear);
                                continue;
                            }
                        }
                    }
                    if (bulletsUp)
                    {
                        if (pdist < 70)
                        {
                            BotRunner.Status = "fight/plant: back off while its bullets fly";
                            var offKey = Math.Abs(pbp.Center.X - pl.Center.X) >= Math.Abs(pbp.Center.Y - pl.Center.Y)
                                ? (pbp.Center.X >= pl.Center.X ? KeyFunctions.Right : KeyFunctions.Left)
                                : (pbp.Center.Y >= pl.Center.Y ? KeyFunctions.Down : KeyFunctions.Up);
                            yield return SafeMove(offKey, target, 12f);
                            continue;
                        }
                        // Standing still is what the aimed bullet is looking for (it lands where we stood when it fired): if any is coming our way,
                        // step out of its line (the planner knows where each flying bullet will be); otherwise wait for them to land.
                        var comingThreats = PredictThreats(null).Where(t => t.Name.Contains("Bullet") || t.Name.Contains("Burst")).ToList();
                        if (comingThreats.Count > 0 && PlanHitFrameStanding(30, KeyFunctions.Right, 0, comingThreats) != int.MaxValue)
                        {
                            var stepAside = SpaceTimeMove(new Vector2(pbp.X, pbp.Y), comingThreats, useCache: false, survive: true);
                            if (stepAside != null && stepAside.Length > 0)
                            {
                                BotRunner.Status = "fight/plant: step out of its bullets' way";
                                yield return K(stepAside);
                                continue;
                            }
                        }
                        BotRunner.Status = "fight/plant: wait for its bullets to land";
                        yield return K();
                        continue;
                    }
                    if (pdist < 50)
                    {
                        var sk = StandoffKeys(pl, pbp, ref plantSwingWait, 45);
                        BotRunner.Status = "fight/plant: standoff";
                        yield return sk;
                        continue;
                    }
                }
                // Hit and owing hearts: a dropped heart that is near and not guarded by an enemy is worth more than the fight (they time out).
                if (BotRunner.DebtBaseline >= 0 || GlobalState.CUR_HEALTH < GlobalState.MAX_HEALTH)
                {
                    var heart = BotRunner.Entities().Where(e => e.exists && e.GetType().Name.Contains("HealthPickup") && !_unreachableHearts.Contains(e)).OrderBy(e => (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - new Vector2(Player.Hitbox.Center.X, Player.Hitbox.Center.Y)).Length()).FirstOrDefault();
                    if (heart != null && (new Vector2(heart.Hitbox.Center.X, heart.Hitbox.Center.Y) - new Vector2(Player.Hitbox.Center.X, Player.Hitbox.Center.Y)).Length() < 110 &&
                        !Enemies().Any(e => (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - new Vector2(heart.Hitbox.Center.X, heart.Hitbox.Center.Y)).Length() < 22) &&
                        !Dangers().Any(e => (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - me).Length() < 36)) // (not with one on us, even one the fight gave up on)
                    {
                        var ht = BotNav.TileOf(new Vector2(heart.Hitbox.Center.X - 5, heart.Hitbox.Center.Y - 6));
                        var hpath = BotNav.FindPath(BotNav.TileOf(Pos), t => t == ht, ht);
                        // (one dropped against a wall can sit out of reach in it: walking at it for a second without getting closer gives up on it)
                        float hd = (new Vector2(heart.Hitbox.Center.X, heart.Hitbox.Center.Y) - new Vector2(Player.Hitbox.Center.X, Player.Hitbox.Center.Y)).Length();
                        if (heart != _heartChased || hd < _heartBest - 1f)
                        {
                            _heartChased = heart;
                            _heartBest = hd;
                            _heartStall = 0;
                        }
                        else if (++_heartStall > 60)
                        {
                            _unreachableHearts.Add(heart);
                            continue;
                        }
                        if (hpath != null && hpath.Count > 0 && hpath.Count < 12 && BotNav.Walkable(ht))
                        {
                            BotRunner.Status = "fight/collect the dropped heart";
                            var hc = BotNav.CentreOf(hpath[0]) + new Vector2(5, 6);
                            yield return SafeMove(StepKey(hc.X - me.X, hc.Y - me.Y, null), null);
                            continue;
                        }
                    }
                }
                // A beam is the best weapon against a Shieldy (its shield only turns the broom, but bullets hurt it): a hit from the front shoves
                // it 8px the way we face, so line up on the side away from a nearby lane and push it into the beam.
                if (target.GetType().Name == "Shieldy" && BotNav.LaneTiles() is { Count: > 0 } pushLanes && beamPushes < 3)
                {
                    var pushDir = BeamPushDirection(target, pushLanes);
                    if (pushDir.HasValue)
                    {
                        beamPushes++;
                        BotRunner.Status = $"fight/push the Shieldy {pushDir.Value} into a beam";
                        yield return PushBlocker(target, pushDir.Value, 4);
                        continue;
                    }
                }
                KeyFunctions dir;
                bool attack = false;
                if (target.GetType().Name == "Shieldy" && target.velocity == Vector2.Zero && FightPolicies.BoomStationary(PolicyCtx(target), pmem) is PKey[] boomKeys)
                {
                    BotRunner.Status = "fight/boom";
                    if (DodgeEnabled && boomKeys.Length == 1 && boomKeys[0] != PKey.Accept && BotNav.LaneTiles().Count > 0)
                    {
                        // beams about: a plain step must time its crossing of them
                        yield return SafeMove(boomKeys[0] switch { PKey.Up => KeyFunctions.Up, PKey.Down => KeyFunctions.Down, PKey.Left => KeyFunctions.Left, _ => KeyFunctions.Right }, target, 12f);
                        continue;
                    }
                    yield return PolicyFrame(boomKeys, target);
                    continue;
                }
                if (target.GetType().Name == "Shieldy" && target.velocity.Y != 0 && target.velocity.X == 0 && aggroFrames <= 0 && ambushFrames < 900 && Target_ColumnAboveClear(target) &&
                    FightPolicies.VerticalShieldy(PolicyCtx(target), pmem) is PKey[] vKeys)
                {
                    // (waiting for it is on purpose, but not for ever: after 15s go at it the ordinary way)
                    ambushFrames++;
                    BotRunner.NotStuck();
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
                        // Never chase it: a Shieldy that patrols sideways at 30px/s cannot be caught, and following it only gets us walked into. Stand
                        // still facing down at the standoff and swing when its predicted position meets the broom (the swing takes ~8 frames to reach).
                        if (Player.facing != Facing.DOWN)
                        {
                            yield return K(KeyFunctions.Down); // turns us (and nudges ~1px towards it: the gap is >= 3)
                            continue;
                        }
                        var at8 = PredictPatroller(target).At[8] ?? target.Hitbox;
                        var downBroom = new Rectangle(pb.X - 11, pb.Bottom + 4, 24, 12);
                        ambushWaited++;
                        swing++;
                        if (downBroom.Intersects(at8) && swing - lastSwing >= SwingCooldown + 8)
                        {
                            lastSwing = swing;
                            ambushWaited = 0;
                            yield return K(KeyFunctions.Accept);
                            continue;
                        }
                        if (ambushWaited > 300)
                        {
                            // it does not pass under this spot (its beat is elsewhere): shift one step towards where it is, then wait again
                            ambushWaited = 0;
                            var shift = adx < 0 ? KeyFunctions.Left : KeyFunctions.Right;
                            var moved2 = pb;
                            moved2.Offset(adx < 0 ? -16 : 16, 0);
                            if (CornersWalkable(moved2))
                            {
                                yield return Hold(12, shift);
                                continue;
                            }
                        }
                        yield return K();
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
                if (target.GetType().Name == "Annoyer" && (distSq < 75 * 75 || AnnoyerAwake(target)))
                {
                    // An awake Annoyer flies to a point 20px right of us and circles us there for 3s before it swoops: chasing it is
                    // what gets us hit. Stand still facing right and swing; it is in reach every time round.
                    BotRunner.Status = "fight/annoyer hold";
                    // Not getting anywhere towards a sleeping Annoyer (something between us)? Leave it asleep.
                    annoyerStill = Pos == annoyerLastPos && target.velocity == Vector2.Zero ? annoyerStill + 1 : 0;
                    annoyerLastPos = Pos;
                    if (annoyerStill > 90)
                    {
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine($"  fight: cannot get to the Annoyer at {target.Hitbox.X},{target.Hitbox.Y}: leaving it");
                        }
                        ignoredTargets.Add(target);
                        committed = null;
                        annoyerStill = 0;
                        continue;
                    }
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
                                BotRunner.Status = "fight/annoyer hold: leave the beam";
                                var nc2 = BotNav.CentreOf(exit[0]) + new Vector2(5, 6);
                                yield return K(StepKey(nc2.X - me.X, nc2.Y - me.Y, target));
                                continue;
                            }
                        }
                    }
                    var safeFrame = safeKeepLeft > 0 ? safeKeep : StandSafe(target, ignoreAnnoyers: true);
                    if (safeFrame.Length > 0)
                    {
                        BotRunner.Status = "fight/annoyer hold: stand safe";
                        if (safeKeepLeft <= 0)
                        {
                            safeKeep = safeFrame;
                            safeKeepLeft = 8; // commit to the sidestep: flipping between two "safe" keys every frame goes nowhere (and never swings)
                        }
                        safeKeepLeft--;
                        yield return safeFrame; // something else (a patroller, a beam) is about to reach this spot: not here
                        continue;
                    }
                    safeKeepLeft = 0;
                    // It homes in on a spot beside us in a straight line (through us if it comes from the other side) and then circles at
                    // ~20px: face wherever it is and keep swinging, the broom reaches it before it touches.
                    var annCtx = PolicyCtx(target);
                    annCtx.Others = Enemies().Where(e => e != target && e.GetType().Name == "Annoyer").Select(e => e.Hitbox).ToList();
                    var annKeys = FightPolicies.AnnoyersHold(annCtx, pmem);
                    if (Environment.GetEnvironmentVariable("ANODYNE_FIGHT_TRACE") != null)
                    {
                        System.Console.Error.WriteLine($"  annoyers policy f{i}: keys=[{string.Join("+", annKeys)}] P={Player.Hitbox} face={Player.facing} T={annCtx.T} others={annCtx.Others.Count} hold={pmem.HoldFace}/{pmem.HoldUntil} swing={pmem.Swing}/{pmem.LastSwing}");
                    }
                    yield return PolicyFrame(annKeys, target);
                    continue;
                }
                // Close but not getting closer (a wall or water between us and a shooter): plan a tile route instead of pushing at it.
                bool blockedClose = DodgeEnabled && noProgress > 40 && target.GetType().Name is not ("Annoyer" or "GasGuy"); // (a Gas Guy comes to us: walking at it is walking into it)
                if (distSq > 30 * 30 || blockedClose)
                {
                    // Far away: follow the tile path (walls between us), re-planned every few frames.
                    var tt = BotNav.TileOf(new Vector2(c.X - 5, c.Y - 6));
                    // keep a berth round enemies that stand still (touching one hurts) when there is a way that does
                    var berthF = BotNav.NearEnemies(12, target, stationaryOnly: true);
                    berthF.Remove(BotNav.TileOf(Pos));
                    berthF.Remove(tt);
                    // and keep out of live beams (and the tiles right beside their emitters) when there is a way round: loitering there is how we get shot
                    var laneAvoid = new HashSet<Point>(berthF);
                    foreach (var lt in BotNav.LaneTiles())
                    {
                        for (int ox = -1; ox <= 1; ox++)
                        {
                            for (int oy = -1; oy <= 1; oy++)
                            {
                                laneAvoid.Add(new Point(lt.X + ox, lt.Y + oy));
                            }
                        }
                    }
                    laneAvoid.Remove(BotNav.TileOf(Pos));
                    laneAvoid.Remove(tt);
                    BotNav.Avoid = laneAvoid;
                    var path = BotNav.FindPath(BotNav.TileOf(Pos), t => t == tt, tt, strict: true);
                    if (path == null)
                    {
                        BotNav.Avoid = berthF;
                        path = BotNav.FindPath(BotNav.TileOf(Pos), t => t == tt, tt, strict: true);
                    }
                    BotNav.Avoid = null;
                    if (path == null)
                    {
                        path = BotNav.FindPath(BotNav.TileOf(Pos), t => t == tt, tt);
                    }
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
                        yield return (DodgeEnabled && IsShooter(target) && target.GetType().Name is not ("Burst_Plant" or "Frog") && GlobalState.CUR_HEALTH >= 3) ? K(dir) : SafeMove(dir, target);
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
                if ((target.GetType().Name == "Slime" || target.GetType().Name == "GasGuy") && distSq < 55 * 55 && !(DodgeEnabled && noProgress > 40 && target.GetType().Name != "GasGuy") && FightPolicies.Slime(PolicyCtx(target), pmem) is PKey[] slimeKeys)
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
                    yield return (DodgeEnabled && IsShooter(target) && target.GetType().Name is not ("Burst_Plant" or "Frog") && GlobalState.CUR_HEALTH >= 3) ? K(dir) : SafeMove(dir, target);
                }
            }
            if (!soft)
            {
                throw new TimeoutException($"bot: Fight timed out on {Map} screen {Grid}");
            }
        }
    }
}
