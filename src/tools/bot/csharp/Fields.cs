using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>Walkthrough Part 6: the Lake (FIELDS).</summary>
    public class Fields : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Fields: Miao, Nexus Point, the Nexus button");
            // Mitra's checkpoint on (5,4) first: deaths on the way then respawn here instead of far back in the Blue Forest.
            yield return Section("fields checkpoint", () => Exec("O1 X1 H5,2 H5,3 H5,4 V"));
            yield return Section("fields miao", () => Exec("O1 X1 H5,2 H5,3 H4,3 I672,528,d W20 T W30"));

            // The Nexus Point (activated by loading its screen); step on the pad and press Accept: Miao objects, then we are teleported.
            yield return Section("fields nexus", () => Exec("O1 X1 H5,3 H6,3 P1034,574 W20 A W60 T W200"));
            // Push the button (opens the gates south); the middle door leads back to the Fields.
            yield return Section("fields nexus back", () => Exec("B688,1072 W60 P712,1060 U30 A W300"));

            // Pick Miao up again, then east to Goldman, who hides in the box; the Icky in it is killed.
            yield return Section("fields miao 2", () => Exec("O1 X1 H5,3 H4,3 I672,528,d W20 T W30"));
            yield return Section("fields goldman", () => Exec("O1 X1 H7,3 H7,2 I1152,432,d W20 T W30 I1152,412,d W60 T W60 F"));

            // The button on (8,2) opens the gates; the cave door sits in the walled yard of (10,1).
            yield return Section("fields cave", () => Exec("O1 X1 H8,2 T B1296,384 W30 H9,2 H9,1 H10,1 P1686,248 U40 W120"));
            // Twelve dust go into the hole; Goldman then walks off the chest. The Goldman card.
            yield return Section("fields goldman card", () => Exec("k W60 T W30 I1360,16,d W120 T W30"));

            // Out of the cave, back to the Nexus Point and through the Nexus door (the world refreshes: Finty shows up).
            yield return Section("fields to nexus", () => Exec("O1 X1 P1344,128 D50 W200 H9,1 H9,2 H8,2 H7,2 H7,3 H6,3 P1034,574 W20 A W60 T W200"));
            yield return Section("fields from nexus", () => Exec("P712,1060 U30 A W300"));

            // Finty (the shopkeeper) is given the box: he hands over the biking shoes, which go to Mitra for the jump boots.
            // Mitra only trades once her first (introduction) chat is over and the screen has been reloaded.
            yield return Section("fields finty", () => Exec("O1 X1 H7,3 H7,4 I1184,704,u W20 T W30 I1184,704,u W20 T W60"));
            yield return Section("fields mitra", () => Exec("O1 X1 H6,4 H5,4 I896,704,d W20 T W60 T W30 H5,5 H5,4 I896,704,d W20 T W60 T W30 V"));

            // The lake: the planner's route is the walkthrough's (spike pocket on (7,6), dust rafts across the island chain down to
            // Mitra's south bank); the button there opens the gate in the channel.
            yield return Section("fields lake", () => Exec("O1 X1 >1056,1203 W90"));

            // The southern chests: the Miao the Cat card and the Mitra card (the walkthrough's long way over dust rafts, spikes and the
            // gate in the channel). `+` plans the whole map (rafts, jumps, buttons, shut gates) and opens every reachable card chest.
            yield return Section("fields southern cards", () => Exec("O1 X1 + W60"));

            // The other southern card (by the slimes, behind kill gates): the walkthrough's way round. West and south over the lake to
            // the west shore, north to (1,6), then (2,6), round by (3,6)/(3,7) into (2,7); its gate down to (2,8) wants all three of
            // its enemies dead in one visit (the dog comes back on re-entry), so (2,7) is fought again from its north side. The frog on
            // (2,9) opens the gates there; the chest is just east of (2,8). On (2,6) (a Slasher, three Annoyers that stay dead, spike
            // beds; all four dead for its gate) the Annoyers are fought first, from the safe west edge, before they can join a Slasher fight.
            yield return Section("fields slime card", () => Exec("O1 X0 H3,9 H3,10 H2,10 H1,10 H1,9 H1,8 H1,7 H1,6 H2,6 d1500 X1 F2000 X0 H3,6 H3,7 X1 H2,7 F2000 X0 >390,1180 X1 F W30 H2,8 F2000 H2,9 F H2,8 H3,8 I528,1344,d W60"));

            // The pause menu's map tab warps back to the Nexus; its middle door returns to the Fields pad (no trip back over the lake).
            yield return Section("fields menu to nexus", () => Exec("n0 W60"));
            yield return Section("fields nexus to pad", () => Exec("P712,1060 U30 A W300"));

            // With the boots, narrow rivers are jumped. West along the south shore to the left door into the Beach.
            yield return Section("fields to beach", () => Exec("O1 X1 >190,892 P175,890 L50 W300"));
        }
    }

    /// <summary>
    /// Walkthrough Part 10: from the Nexus to the Cliffs dungeon (CROWD). The Fields pad, the raft and button maze of the east Fields to
    /// the Forest door, the Forest's Nexus Point and its raft crossing to the Cliffs, the Cliffs' Nexus Point and the dungeon door.
    /// </summary>
    public class ForestCliff : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Forest and Cliffs: to the Crowd");
            yield return Section("nexus to the fields", () => Exec("O1 X1 P712,1060 U30 A W300 `fl_pad2"));
            // East Fields: with the (8,2) button pressed (Fields part) the way is north round (8,3)-(10,4) and down to the door; the
            // planner presses the (9,5) button on the way. f1: the rafts ride the one-tile rivers instead of jumping off across them.
            yield return Section("fields east to the forest", () => Exec("O1 X1 f1 b1730,872 f0 oFOREST W60 `fo_in"));
            yield return Section("forest nexus point", () => Exec("O1 X1 H1,6 W30 `fo_pad"));
            // A dust raft over the lake along the bottom of the Forest to the Cliffs door.
            // (no auto-fight: the Frog by the lake's west shore is out of reach from the raft, and going for it undoes every hop)
            yield return Section("forest to the cliffs", () => Exec("O1 X0 oCLIFF W60 `cl_in"));
            yield return Section("cliffs to the crowd", () => Exec("O1 X1 H3,4 W30 oCROWD@738,812 W60 `cr_in"));
        }
    }

    /// <summary>
    /// Walkthrough Part 11: the Cliffs dungeon (CROWD), from its door to the boss. Four keys (frogs, platforms ridden by sweeping dust
    /// onto them, buttons), the health chest, the keyblocks, the spike roller rooms (CrowdPlatforms routes t8/t9/t12 time the rollers),
    /// the frog room and the platform rides of the north-east, then the two fall doors down to the Wall boss, the Sage and the blue key.
    /// </summary>
    public class Crowd : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Crowd: keys, rollers and the fall doors");
            yield return Section("crowd in", () => Exec("O1 X1 H3,6 W30 `cr_36"));
            // (the Annoyer on (2,3) sleeps until we come within 64px, and the gate to the key needs it dead)
            // (2,3) is a Dog, two Annoyers and slimes in one small room: no line of play here takes no hits yet, so it has a budget of three
            yield return Section("crowd key 1", () => Exec("O1 X1 H3,5 F H2,5 H2,4 F g3 H2,3 F >404,540 W20 F W90 g0 I336,592,d W60 `cr_key1"));
            // (4,6): two frogs side by side behind a one-tile pit keep each other's volleys going, and the fight's patient wait for a way
            // in and out clear of them all can last for ever. Killing the upper one opens the corridor: the frog rush (wait out of reach,
            // run in, jump, two swings), with a budget for the hit or two it takes.
            yield return Section("crowd frogs", () => Exec("g3 O1 X0 >680,1060 <692,1074 W30 g0 `cr_frogs46"));
            // (5,6) is islands between pits: the frog sits on the middle one, the way to the strip up to (5,5), and another fills that
            // strip. Rush both from below (no auto-fight: chasing the first round the pits ends on a ledge with no way off).
            yield return Section("crowd key 2", () => Exec("g3 O1 X0 >835,1085 <851,1058,uj W30 >899,930 <899,884,u W30 g0 O1 X1 >890,724 W30 I896,704,d W60 `cr_key2b"));
            yield return Section("crowd west", () => Exec("O1 X1 >818,738 W20 >720,870 O1 X1 >408,980 `cr_26n"));
            // (2,6): the platforms to the chest side and back (t1, t2), then from the west floor to the island (t3).
            yield return Section("crowd key 3", () => Exec("O1 X1 t1 I496,976,d W30 `cr_key3"));
            yield return Section("crowd island", () => Exec("O1 X1 t2 O1 X1 >296,1030 >344,1030 t3 `cr_island"));
            yield return Section("crowd (2,7) platforms", () => Exec("O1 X1 t4 `cr_17"));
            yield return Section("crowd (0,6) buttons", () => Exec("O1 X1 >100,1028 W10 >68,1028 W30 >130,930 `cr_05"));
            yield return Section("crowd (1,5) frogs", () => Exec("O1 X0 H1,5 >248,856 X1 F3000 W60 `cr_15"));
            yield return Section("crowd (1,4) platform up", () => Exec("O1 X0 >258,790 t5 `cr_14"));
            yield return Section("crowd to (2,2)", () => Exec("O1 X1 H1,3 F2000 X0 H2,3 H2,2 X1 F2000 `cr_22"));
            yield return Section("crowd key 4", () => Exec("O1 X1 H3,2 F3000 I608,336,d W60 `cr_key4"));
            yield return Section("crowd (3,3) button", () => Exec("O1 X1 >498,498 W20 >404,430 `cr_btn33"));
            yield return Section("crowd back to (1,4)", () => Exec("O1 X0 H1,3 X1 >232,660 F2000 `cr_14t"));
            yield return Section("crowd (1,4) platforms west", () => Exec("O1 X1 t6 `cr_04e"));
            yield return Section("crowd (0,4)", () => Exec("O1 X1 H0,4 F3000 O1 X1 >128,768 W10 >16,768 W10 F3000 t7 >50,630 `cr_03"));
            yield return Section("crowd (0,3) keyblock", () => Exec("O1 X1 >131,610 R40 W20 `cr_13k"));
            // The two rollers on the way up to the health chest: hop the first, follow the second.
            yield return Section("crowd rollers", () => Exec("O1 X0 t8 X1 >296,470 W10 O1 X0 t9 `cr_12r"));
            yield return Section("crowd health", () => Exec("O1 X1 I352,336,d W60 `cr_hp"));
            yield return Section("crowd (3,3) door", () => Exec("O1 X1 >565,592 W10 O1 X0 U40 W20 U40 W90 `cr_52"));
            yield return Section("crowd (7,2) frogs", () => Exec("O1 X0 >838,352 U30 W90 O1 X1 F3000 W60 `cr_72f"));
            // (the crowd's people wander about the keyblock and can stand between us and it: push at it again a little later)
            yield return Section("crowd (7,1) keyblock", () => Exec("O1 X0 >1190,330 >1189,262 G1191,262 U40 W20 U60 W90 G1191,262 U60 W90 G1191,262 U60 >1190,140 W10 `cr_71"));
            yield return Section("crowd (8,0) (9,0) platforms", () => Exec("O1 X0 >1270,72 R30 W60 t10 >1432,72 R30 W60 t11 `cr_90b"));
            yield return Section("crowd (9,1) rollers", () => Exec("O1 X0 >1508,155 D8 t12 >1560,330 W30 `cr_92"));
            // The (9,2) button (opens the gate south), then the fall doors: (9,2) to (3,4) to the boss's door on (9,7).
            yield return Section("crowd fall doors", () => Exec("O1 X1 >1576,438 W30 O1 X0 >1516,340 D60 W400 `cr_97"));
            yield return Section("crowd to the boss", () => Exec("O1 X0 >1512,1110 W30 `cr_96a"));
            // The Wall boss (WallBossFight), its Health Cicada, the Sage (who walks up to talk), the blue key and the chest by it; then the
            // menu's warp to the dungeon's entrance and out to the Cliffs.
            yield return Section("crowd wall boss", () => Exec("O1 X0 x W60 `cr_boss"));
            yield return Section("crowd cicada", () => Exec("O1 X0 i W60 `cr_cic"));
            yield return Section("crowd sage and blue key", () => Exec("O1 X0 >1512,935 W60 U20 W400 T W60 >1538,900 I1536,864,d W60 T W60 T W30 I1488,832,d W60 T W30 `cr_key"));
            // North of the boss room is the dungeon's own Nexus pad (it lights up when its screen loads): take it to the Nexus.
            yield return Section("crowd north nexus point", () => Exec("O1 X0 >1514,770 W30 P1514,750 W20 A W60 T W200 `cr_out"));
        }
    }

    /// <summary>
    /// Walkthrough Part 12: the Windmill. From the Nexus to the Fields and east to the windmill's door (a trapdoor by the river on
    /// (10,8), walked into from the left), up through the three big-key gates (green, red, blue), the card chest, the climb to the top
    /// and the console that starts the windmill (a cutscene moves the three dungeon statues).
    /// </summary>
    public class Windmill : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Windmill: three big keys and the console");
            yield return Section("windmill nexus to the fields", () => Exec("O1 X1 P712,1060 U30 A W300 `wm_fields"));
            // (the door's 8px hitbox sits right of the floor in front of it, which is water: stand left of it and walk in)
            yield return Section("windmill fields to the door", () => Exec("O1 X1 >1729,1363 R20 W120 `wm_in"));
            yield return Section("windmill big key gates", () => Exec("O1 X0 U90 W60 U60 >232,1010 U90 W60 U60 >232,900 U90 W60 U60 >232,860 W10 `wm_gates"));
            yield return Section("windmill climb", () => Exec("O1 X1 >208,402 W10 `wm_top"));
            yield return Section("windmill console", () => Exec("O1 X0 U4 A W900 T W60 `wm_on"));
            // The card chest by the gates says "Some strange force stops this" until the console has run
            yield return Section("windmill card", () => Exec("O1 X1 I176,832,d W60 T W30 `wm_card"));
        }
    }
}

namespace AnodyneSharp.Recording
{
    using System;
    using System.Linq;
    using AnodyneSharp.Entities;
    using AnodyneSharp.Entities.Gadget;
    using AnodyneSharp.Entities.Enemy.Crowd;
    using AnodyneSharp.Input;
    using Microsoft.Xna.Framework;

    /// <summary>
    /// The Crowd's moving platforms (Propelled): one starts when we land on it (a jump) while it is active, rides 33px/s to its Stop_Marker
    /// and goes inactive; dust swept onto it makes it active again. Standing on one, holes do not swallow us.
    /// </summary>
    public class CrowdPlatforms : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield break;
        }

        private static Vector2 Me => new(Player.Hitbox.X + Player.Hitbox.Width / 2f, Player.Hitbox.Y + Player.Hitbox.Height / 2f);

        private static Vector2 Mid(Entity e) => new(e.Hitbox.X + e.Hitbox.Width / 2f, e.Hitbox.Y + e.Hitbox.Height / 2f);

        private static readonly System.Reflection.FieldInfo ActiveField =
            typeof(Propelled).GetField("active", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);

        /// <summary>The platform nearest the middle of tile (tx,ty).</summary>
        private static Entity PlatformAt(int tx, int ty) => BotRunner.Entities().Where(e => e.exists && e is Propelled)
            .OrderBy(e => (Mid(e) - new Vector2(tx * 16 + 8, ty * 16 + 8)).LengthSquared()).FirstOrDefault();

        private static bool Active(Entity p) => ActiveField?.GetValue(p) is bool b && b;

        private static void Trace(string what)
        {
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
            {
                System.Console.Error.WriteLine($"  platforms f{BotRunner.Frame}: {what} at {Me}");
            }
        }

        /// <summary>
        /// Jump (towards dir) onto the platform at tile (tx,ty), steering onto its middle in the air: it is the next tile, and a whole
        /// jump would carry us past it.
        /// </summary>
        private static IEnumerable<object> JumpOnto(int tx, int ty, KeyFunctions dir)
        {
            var plat = PlatformAt(tx, ty);
            Trace($"jump onto the platform at {tx},{ty} ({Mid(plat)}, active={Active(plat)})");
            yield return Hold(1, dir);
            yield return Wait(2);
            for (int i = 0; i < 60; i++)
            {
                if (i > 3 && Player.state == PlayerState.GROUND)
                {
                    break;
                }
                var d = Mid(plat) - Me;
                var keys = new List<KeyFunctions>();
                if (i == 0)
                {
                    keys.Add(KeyFunctions.Cancel);
                    keys.Add(dir);
                }
                else
                {
                    if (d.X > 1f) { keys.Add(KeyFunctions.Right); } else if (d.X < -1f) { keys.Add(KeyFunctions.Left); }
                    if (d.Y > 1f) { keys.Add(KeyFunctions.Down); } else if (d.Y < -1f) { keys.Add(KeyFunctions.Up); }
                }
                BotRunner.Status = $"platforms: jump onto {tx},{ty}";
                yield return K(keys.ToArray());
            }
            Trace($"landed, platform moving {plat.velocity}");
        }

        /// <summary>Ride the platform under us until it stops (or until done() says to get off).</summary>
        private static IEnumerable<object> Ride(int tx, int ty, Func<bool> done = null)
        {
            var plat = PlatformAt(tx, ty);
            bool moved = false;
            for (int i = 0; i < 900; i++)
            {
                moved |= plat.velocity != Vector2.Zero;
                if ((moved && plat.velocity == Vector2.Zero) || (done != null && done()))
                {
                    break;
                }
                // Stay on its middle (it carries us, but a pixel off each frame adds up).
                var d = Mid(plat) - Me;
                var keys = new List<KeyFunctions>();
                if (d.X > 2f) { keys.Add(KeyFunctions.Right); } else if (d.X < -2f) { keys.Add(KeyFunctions.Left); }
                if (d.Y > 2f) { keys.Add(KeyFunctions.Down); } else if (d.Y < -2f) { keys.Add(KeyFunctions.Up); }
                BotRunner.Status = $"platforms: ride {tx},{ty}";
                yield return K(keys.ToArray());
            }
            Trace($"ride over, platform at {Mid(plat)}");
        }

        /// <summary>Stand on tile (tx,ty), turn to face and swing (sweeps dust up, or sets the dust we carry down on the tile in front).</summary>
        private static IEnumerable<object> Sweep(int tx, int ty, KeyFunctions face, Facing facing, int nudge = 0)
        {
            yield return Until(() => !(Player.broom != null && Player.broom.exists), 60, "the last swing to end");
            var c = BotNav.CentreOf(new Point(tx, ty));
            yield return GoTo(c.X, c.Y);
            // The dust goes down on the tile in front of the tile of Player.Center, which sits a few pixels off our hitbox's middle: stand
            // where that point is on this tile (as near its middle as that allows).
            var off = Player.Center - Me;
            float Fit(float mid, float o, int t) => Math.Clamp(mid, t * 16 + 2 - o, t * 16 + 13 - o);
            var at = new Vector2(Fit(tx * 16 + 8, off.X, tx), Fit(ty * 16 + 8, off.Y, ty));
            Trace($"sweep stand: off={off} aim={at} pos={Player.Position} hb={Player.Hitbox}");
            yield return WalkTo(at.X - BotNav.HalfW, at.Y - BotNav.HalfH, tol: 0.8f);
            Trace($"sweep stood: centre={Player.Center} facing={Player.facing}");
            for (int i = 0; i < 10 && Player.facing != facing; i++)
            {
                yield return Hold(1, face);
            }
            // (a short broom only just reaches the next tile: lean in, staying on this tile)
            yield return Hold(nudge, face);
            yield return Wait(2);
            Trace($"  before swing: facing={Player.facing} front={(Player.Center / 16 + Entity.FacingDirection(Player.facing)).ToPoint()} coll={AnodyneSharp.Registry.GlobalState.Map.GetCollisionData((Player.Center / 16 + Entity.FacingDirection(Player.facing)).ToPoint().ToVector2() * 16)} carried={Player.broom.dust?.Position}");
            yield return Tap(KeyFunctions.Accept);
            for (int i = 0; i < 24 && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null; i++)
            {
                Trace($"  swing +{i}: carried={(Player.broom.dust == null ? "-" : Player.broom.dust.Position + ":" + Player.broom.dust.exists + ":" + Player.broom.dust.CurAnimName)} centre={Player.Center} broomdust={Player.broom.dust != null} plats={string.Join(" ", BotRunner.Entities().Where(e => e.exists && e is Propelled).Select(e => Mid(e) + ":" + Active(e)))} dusts={string.Join(" ", BotRunner.Entities().Where(e => e is Dust).Select(e => e.Position + ":" + e.exists + ":" + e.CurAnimName))}");
                yield return K();
            }
            yield return Until(() => !(Player.broom != null && Player.broom.exists), 60, "the swing to end");
            yield return Wait(4);
            Trace($"swept from {tx},{ty} facing {facing}: carrying={Player.broom.dust != null}");
        }

        /// <summary>Stand still until ready() (not stuck: we wait on purpose).</summary>
        private static IEnumerable<object> WaitFor(Func<bool> ready, string what, int frames = 900)
        {
            for (int i = 0; i < frames && !ready(); i++)
            {
                BotRunner.Status = $"platforms: wait for {what}";
                BotRunner.NotStuck();
                yield return K();
            }
            Trace($"waited for {what}");
        }

        /// <summary>A whole jump (towards dir, held all the way) off the platform we ride.</summary>
        private static IEnumerable<object> LeapOff(KeyFunctions dir)
        {
            Trace($"leap off {dir}");
            yield return Hold(1, dir, KeyFunctions.Cancel);
            for (int i = 0; i < 60 && Player.state != PlayerState.GROUND; i++)
            {
                yield return K(dir);
            }
            Trace("leapt");
        }

        /// <summary>Sweep from (tx,ty) until we carry dust (carry) or have set it down (!carry): a swing does not always catch it.</summary>
        private static IEnumerable<object> SweepTill(int tx, int ty, KeyFunctions face, Facing facing, bool carry)
        {
            for (int i = 0; i < 4 && (Player.broom.dust != null) != carry; i++)
            {
                yield return Sweep(tx, ty, face, facing, carry ? 2 * i : 0);
            }
            if ((Player.broom.dust != null) != carry)
            {
                throw new InvalidOperationException($"bot: could not {(carry ? "sweep up" : "set down")} the dust from {tx},{ty}");
            }
        }

        /// <summary>Sweep up the loose dust nearest tile (tx,ty) (a fight may have moved it), from a floor tile beside it.</summary>
        private static IEnumerable<object> SweepUpNear(int tx, int ty)
        {
            yield return Until(() => !(Player.broom != null && Player.broom.exists), 60, "the last swing to end"); // (a swing setting dust down still holds it)
            if (Player.broom.dust != null)
            {
                yield break;
            }
            var dust = BotRunner.Entities().Where(e => e.exists && e is Dust && e.CurAnimName != "poof" && e.CurAnimName != "fallpoof")
                .OrderBy(e => (e.Position - new Vector2(tx * 16, ty * 16)).LengthSquared()).FirstOrDefault();
            if (dust == null)
            {
                throw new InvalidOperationException($"bot: no dust near {tx},{ty}");
            }
            var dt = new Point((int)Math.Round(dust.Position.X / 16f), (int)Math.Round(dust.Position.Y / 16f));
            Trace($"dust to sweep up at {dt}");
            foreach (var (o, key, facing) in new[] { (new Point(0, 1), KeyFunctions.Up, Facing.UP), (new Point(0, -1), KeyFunctions.Down, Facing.DOWN), (new Point(1, 0), KeyFunctions.Left, Facing.LEFT), (new Point(-1, 0), KeyFunctions.Right, Facing.RIGHT) })
            {
                var st = new Point(dt.X + o.X, dt.Y + o.Y);
                if (BotNav.Walkable(st) && !BotNav.IsHole(st))
                {
                    yield return SweepTill(st.X, st.Y, key, facing, true);
                    yield break;
                }
            }
            throw new InvalidOperationException($"bot: nowhere to stand beside the dust at {dt}");
        }

        /// <summary>Step off onto the floor tile (tx,ty) next to us.</summary>
        private static IEnumerable<object> StepTo(int tx, int ty)
        {
            var c = BotNav.CentreOf(new Point(tx, ty));
            yield return WalkTo(c.X, c.Y, tol: 1.5f, timeout: 120, diagonal: true);
        }

        /// <summary>
        /// Any screen's platform: from a floor tile next to the one at tile (tx,ty) (behind it if we can, so we jump on along its way),
        /// jump on and ride it until it stops. Getting off is the next step's business.
        /// </summary>
        public static IEnumerable<object> RideFrom(int tx, int ty)
        {
            var plat = PlatformAt(tx, ty);
            if (plat == null)
            {
                throw new Exception($"platform: none near {tx},{ty}");
            }
            var pt = new Point((int)Math.Floor(Mid(plat).X / 16f), (int)Math.Floor(Mid(plat).Y / 16f));
            var ahead = plat.facing switch { Facing.UP => new Point(0, -1), Facing.DOWN => new Point(0, 1), Facing.LEFT => new Point(-1, 0), _ => new Point(1, 0) };
            var here = BotNav.TileOf(BotRunner.Player.Position);
            var sides = new[] { new Point(-ahead.X, -ahead.Y), new Point(ahead.Y, ahead.X), new Point(-ahead.Y, -ahead.X), ahead };
            Point? from = null;
            foreach (var o in sides)
            {
                var q = new Point(pt.X + o.X, pt.Y + o.Y);
                if (BotNav.Walkable(q) && (q == here || BotNav.FindPath(here, q) != null))
                {
                    from = q;
                    break;
                }
            }
            if (from == null)
            {
                throw new Exception($"platform: no floor next to {pt} to jump on from");
            }
            Trace($"ride the platform at {pt} (going {plat.facing}, active={Active(plat)}) from {from}");
            var c = BotNav.CentreOf(from.Value);
            yield return GoTo(c.X, c.Y);
            yield return StepTo(from.Value.X, from.Value.Y);
            var dx = pt.X - from.Value.X;
            var dy = pt.Y - from.Value.Y;
            var dir = dy < 0 ? KeyFunctions.Up : dy > 0 ? KeyFunctions.Down : dx < 0 ? KeyFunctions.Left : KeyFunctions.Right;
            yield return JumpOnto(pt.X, pt.Y, dir);
            yield return Ride(pt.X, pt.Y);
        }

        public static IEnumerable<object> Platforms(int which)
        {
            switch (which)
            {
                case 1:
                    // (2,6) from the north: the active platform down onto the island, its dust onto the platform going up, off that one to the
                    // west floor, the dust there onto the platform going east, and that one to the far side (the (3,6) key chest).
                    yield return GoTo(BotNav.CentreOf(new Point(26, 62)).X, BotNav.CentreOf(new Point(26, 62)).Y);
                    yield return JumpOnto(26, 63, KeyFunctions.Down);
                    yield return Ride(26, 63);
                    yield return StepTo(26, 68);
                    yield return SweepTill(24, 68, KeyFunctions.Left, Facing.LEFT, true); // the island's dust
                    Trace($"carrying dust: {Player.broom.dust != null}; dust left: {string.Join(" ", BotRunner.Entities().Where(e => e.exists && e is Dust).Select(e => e.Position.ToString()))}");
                    yield return SweepTill(24, 68, KeyFunctions.Up, Facing.UP, false); // ...onto the platform above
                    Trace($"island dust on the up platform: active={Active(PlatformAt(24, 67))} carrying={Player.broom.dust != null} facing={Player.facing}");
                    yield return Wait(20);
                    Trace($"  later: active={Active(PlatformAt(24, 67))} carrying={Player.broom.dust != null}");
                    yield return JumpOnto(24, 67, KeyFunctions.Up);
                    yield return Ride(24, 67, () => Me.Y <= 64 * 16 + 9);
                    yield return LeapOff(KeyFunctions.Left);
                    yield return StepTo(22, 64);
                    yield return SweepTill(22, 64, KeyFunctions.Down, Facing.DOWN, true); // the west floor's dust
                    yield return SweepTill(22, 65, KeyFunctions.Right, Facing.RIGHT, false); // ...onto the platform going east
                    Trace($"west dust on the east platform: active={Active(PlatformAt(23, 65))}");
                    yield return JumpOnto(23, 65, KeyFunctions.Right);
                    yield return Ride(23, 65);
                    yield return StepTo(29, 65);
                    break;
                case 2:
                    // (2,6) back from the (3,6) chest side to the west floor: its dust onto the platform going west.
                    yield return SweepTill(30, 64, KeyFunctions.Left, Facing.LEFT, true);
                    yield return SweepTill(29, 64, KeyFunctions.Left, Facing.LEFT, false);
                    Trace($"dust on the west platform: active={Active(PlatformAt(28, 64))}");
                    yield return JumpOnto(28, 64, KeyFunctions.Left);
                    yield return Ride(28, 64);
                    yield return StepTo(22, 64);
                    break;
                case 3:
                    // (2,6) from the west floor (the screen just re-entered, so its platforms and dust are back) to the island: the dust onto
                    // the platform going east, and off it straight up onto the active one going down past it.
                    yield return SweepTill(22, 64, KeyFunctions.Down, Facing.DOWN, true);
                    yield return SweepTill(22, 65, KeyFunctions.Right, Facing.RIGHT, false);
                    yield return JumpOnto(23, 65, KeyFunctions.Right);
                    yield return Ride(23, 65, () => Me.X >= PlatformAt(26, 63).Hitbox.Center.X - 3f);
                    yield return JumpOnto(26, 63, KeyFunctions.Up);
                    yield return Ride(26, 63);
                    yield return StepTo(26, 68);
                    break;
                case 4:
                    // (2,7) from the island down to the west way out (to (1,7)): the island's dust onto the platform below it, that one down to
                    // the far corner, the dust there onto the platform going west.
                    yield return SweepTill(25, 72, KeyFunctions.Up, Facing.UP, true);
                    yield return SweepTill(25, 72, KeyFunctions.Down, Facing.DOWN, false);
                    yield return JumpOnto(25, 73, KeyFunctions.Down);
                    yield return Ride(25, 73);
                    yield return StepTo(25, 78);
                    yield return SweepTill(28, 77, KeyFunctions.Up, Facing.UP, true);
                    // Set down, dust is swept along from where it was picked up (its last position) and stops at the walls on the way: from
                    // the column right of the platform the wall beside it shoves it back a tile. So first move it into the platform's column.
                    yield return SweepTill(27, 77, KeyFunctions.Up, Facing.UP, false);
                    yield return SweepTill(27, 77, KeyFunctions.Up, Facing.UP, true);
                    yield return SweepTill(27, 75, KeyFunctions.Up, Facing.UP, false);
                    Trace($"dust now: {string.Join(" ", BotRunner.Entities().Where(e => e.exists && e is Dust).Select(e => e.Position + ":" + e.CurAnimName))} plats: {string.Join(" ", BotRunner.Entities().Where(e => e.exists && e is Propelled).Select(e => Mid(e) + ":" + Active(e)))}");
                    yield return JumpOnto(27, 74, KeyFunctions.Up);
                    yield return Ride(27, 74);
                    yield return StepTo(21, 74);
                    break;
                case 5:
                    // (1,4) from the south door: the active platform in the bottom right corner up to the top row (the way north).
                    yield return GoTo(BotNav.CentreOf(new Point(18, 48)).X, BotNav.CentreOf(new Point(18, 48)).Y);
                    yield return JumpOnto(18, 47, KeyFunctions.Up);
                    yield return Ride(18, 47);
                    yield return StepTo(17, 41);
                    break;
                case 6:
                    // (1,4) from the top row to the west way out (to (0,4)): one dust onto the platform going down, the other carried along on
                    // it; off it to the right onto the lone floor tile above the platform going west, that dust onto it, and that one west.
                    yield return SweepTill(14, 41, KeyFunctions.Left, Facing.LEFT, true);
                    yield return SweepTill(15, 41, KeyFunctions.Down, Facing.DOWN, false);
                    Trace($"dust on the down platform: active={Active(PlatformAt(15, 42))}");
                    yield return SweepTill(11, 41, KeyFunctions.Down, Facing.DOWN, true);
                    yield return GoTo(BotNav.CentreOf(new Point(15, 41)).X, BotNav.CentreOf(new Point(15, 41)).Y);
                    yield return JumpOnto(15, 42, KeyFunctions.Down);
                    yield return Ride(15, 42, () => Me.Y >= 44 * 16 + 7);
                    yield return LeapOff(KeyFunctions.Right);
                    yield return StepTo(17, 44);
                    yield return SweepTill(17, 44, KeyFunctions.Down, Facing.DOWN, false);
                    Trace($"dust on the west platform: active={Active(PlatformAt(17, 45))}");
                    yield return JumpOnto(17, 45, KeyFunctions.Down);
                    yield return Ride(17, 45);
                    yield return StepTo(10, 45);
                    break;
                case 7:
                    // (0,4) from the bottom row up to the gate: the dust at the top of the left column onto the left of the two platforms.
                    yield return SweepUpNear(1, 42);
                    yield return SweepTill(3, 46, KeyFunctions.Right, Facing.RIGHT, false);
                    Trace($"dust on the up platform: active={Active(PlatformAt(4, 46))}");
                    yield return JumpOnto(4, 46, KeyFunctions.Right);
                    yield return Ride(4, 46);
                    yield return StepTo(4, 41);
                    break;
                case 8:
                {
                    // (1,3) in from the keyblock: the spike roller drops when we touch it (harmless as it falls), then rolls east across
                    // the whole room. Get ahead of it, jump back over it (it can't hurt us in the air) and let it roll on to its stop.
                    yield return GoTo(203, 610);
                    var roller = BotRunner.Entities().OfType<BaseSpikeRoller>().OrderBy(e => (Mid(e) - Me).LengthSquared()).First();
                    yield return Until(() => roller.CollisionOn && roller.Hitbox.Right >= Player.Hitbox.Left - 3, 300, "the spike roller to come");
                    Trace($"hop the roller at {roller.Hitbox}");
                    yield return LeapOff(KeyFunctions.Left);
                    yield return Until(() => roller.velocity == Vector2.Zero, 600, "the spike roller to stop");
                    Trace($"roller stopped at {roller.Hitbox}");
                    break;
                }
                case 9:
                {
                    // (1,2) up the east corridor: another roller drops across it and rolls north to the corridor's top. Touch it while
                    // it falls, step back south and follow it up once it stops.
                    yield return GoTo(296, 466);
                    var roller = BotRunner.Entities().OfType<BaseSpikeRoller>().Where(e => e.Hitbox.Width > e.Hitbox.Height).OrderBy(e => (Mid(e) - Me).LengthSquared()).First();
                    for (int i = 0; i < 40 && !roller.visible; i++)
                    {
                        yield return Hold(1, KeyFunctions.Up);
                    }
                    Trace($"roller dropping at {roller.Hitbox}");
                    // (just south of it, but not so far south that the screen changes and it resets)
                    for (int i = 0; i < 600 && !(roller.CollisionOn && roller.velocity == Vector2.Zero); i++)
                    {
                        BotRunner.Status = "platforms: wait for the spike roller to stop";
                        BotRunner.NotStuck();
                        yield return Player.Position.Y < 465.5f ? K(KeyFunctions.Down) : Player.Position.Y > 467f ? K(KeyFunctions.Up) : K();
                    }
                    Trace($"roller stopped at {roller.Hitbox}");
                    break;
                }
                case 10:
                    // (8,0) from the west: the dust by the entrance onto the platform at the pit's west edge, which rides east across it.
                    yield return SweepUpNear(81, 6);
                    yield return SweepTill(82, 5, KeyFunctions.Right, Facing.RIGHT, false);
                    Trace($"dust on the east platform: active={Active(PlatformAt(83, 5))}");
                    yield return JumpOnto(83, 5, KeyFunctions.Right);
                    yield return Ride(83, 5);
                    yield return StepTo(87, 5);
                    break;
                case 11:
                    // (9,0): the dust in the north-east corner onto the platform at the top of the pit, which rides south across it.
                    yield return SweepUpNear(96, 4);
                    yield return SweepTill(94, 4, KeyFunctions.Down, Facing.DOWN, false);
                    Trace($"dust on the south platform: active={Active(PlatformAt(94, 5))}");
                    yield return JumpOnto(94, 5, KeyFunctions.Down);
                    yield return Ride(94, 5);
                    yield return StepTo(94, 9);
                    break;
                case 12:
                {
                    // (9,1) in from the north: two rollers across the whole room. The top one drops when we touch it and rolls south
                    // to stop on row 18; the bottom one drops when we cross row 18 (the only way east, and it can't be crossed without
                    // touching it) and rolls north to stop on row 11. In the air they can't hurt us, so: get past the top one while it
                    // falls, cross row 18 just ahead of it, jump north through it at the col 95 gap, go round to the east corridor,
                    // jump south through the bottom one there, then jump the gap and the stopped top roller to the exit.
                    var rollers = BotRunner.Entities().OfType<BaseSpikeRoller>().Where(e => e.Hitbox.Width > e.Hitbox.Height)
                        .OrderBy(e => e.Hitbox.Y).ToList();
                    var top = rollers.First();
                    var low = rollers.Last();
                    for (int i = 0; i < 40 && !top.visible; i++)
                    {
                        yield return Hold(1, KeyFunctions.Down);
                    }
                    Trace($"top roller dropping at {top.Hitbox}");
                    yield return GoTo(1496, 244);
                    yield return WaitFor(() => top.Hitbox.Bottom >= 236, "the top roller to come down");
                    yield return GoTo(1496, 280);
                    yield return WalkTo(1491, 283, tol: 1f);
                    yield return WalkTo(1523, 283, tol: 1f);
                    yield return WalkTo(1523, 276, tol: 1f);
                    Trace($"under the gap: top {top.Hitbox}, low {low.Hitbox} visible={low.visible}");
                    yield return WaitFor(() => top.Hitbox.Bottom >= Player.Hitbox.Top - 4
                        || (low.CollisionOn && low.Hitbox.Top <= Player.Hitbox.Bottom + 4), "the rollers to come");
                    Trace($"jump up through the top roller at {top.Hitbox}");
                    yield return LeapOff(KeyFunctions.Up);
                    yield return GoTo(1576, 202);
                    Trace($"in the east corridor: top {top.Hitbox}, low {low.Hitbox}");
                    yield return WaitFor(() => low.CollisionOn && low.Hitbox.Top <= Player.Hitbox.Bottom + 3, "the bottom roller to come up");
                    Trace($"jump down through the bottom roller at {low.Hitbox}");
                    yield return LeapOff(KeyFunctions.Down);
                    yield return GoTo(1576, 246);
                    yield return WaitFor(() => top.CollisionOn && top.velocity == Vector2.Zero, "the top roller to stop");
                    Trace($"top roller stopped at {top.Hitbox}: jump the gap");
                    yield return LeapOff(KeyFunctions.Down);
                    // (a jump carries us 30px: take off right above the roller, clear of the wall on the exit corridor's east side)
                    yield return WalkTo(1555, top.Hitbox.Top - 13, tol: 0.5f);
                    Trace($"take off from {Player.Hitbox}");
                    Trace($"jump the top roller at {top.Hitbox}");
                    yield return LeapOff(KeyFunctions.Down);
                    yield return Hold(10, KeyFunctions.Down);
                    break;
                }
                case 20:
                    // The Apartments' entrance (2,5): the active platform on the west holes, over a hole from the floor, rides up to the
                    // top row and the way north.
                    yield return StepTo(23, 57);
                    yield return JumpOnto(21, 57, KeyFunctions.Left);
                    yield return Ride(21, 57);
                    yield return StepTo(21, 52);
                    break;
                default:
                    throw new InvalidOperationException($"bot: no platform route {which}");
            }
        }
    }
}
