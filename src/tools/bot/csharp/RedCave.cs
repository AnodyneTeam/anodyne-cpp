using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>
    /// Walkthrough Part 9: the Red Cave. Starts at the entrance (screen 3,4) and works north.
    /// </summary>
    public class RedCave : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Red Cave: north from the entrance");
            yield return Section("redcave north 1", () => Exec("O1 X0 H3,3 m40"));
            // Two-button gate west: stand on the west button (behind the pillar) at its top edge. The Mover homes in on the player's position, so it
            // stops against the pillar level with us, on the east button. (Luring it about first walks under the steam jet and gets shoved in.)
            yield return Section("redcave buttons room", () => Exec("O1 X0 H3,2 B496,416,499,407"));
            yield return Section("redcave west", () => Exec("O1 X1 H2,2 F H1,2"));
            yield return Section("redcave north to worm", () => Exec("O1 X0 H1,1"));
            yield return Section("redcave stab the worm", () => Exec("g3 O1 X0 I224,224,l I224,224,l I224,224,l W60 g0"));
            // The reapers (Slashers) cannot be fought hit-for-hit in these narrow rooms: a hit or two is taken and the hearts they drop win it back.
            yield return Section("redcave reaper 1", () => Exec("g3 O1 X1 F g0"));
            yield return Section("redcave east 1", () => Exec("O1 X0 H2,1 W90 g3 X1 F g0 `rc_east1"));
            yield return Section("redcave key chest", () => Exec("O1 X0 H3,1 W60 I512,176,d W60 `rc_key1"));
            yield return Section("redcave back to the button room", () => Exec("O1 X1 H3,2 W30 `rc_hub1"));
            yield return Section("redcave water kills the mover", () => Exec("O1 X0 m60 W30 `rc_hub2"));
            // East through the steam/spike rooms (4,2), (5,2) to the right tentacle in (5,1). No soft fights: the Slimes here sit by a Four_Shooter
            // and every swing at them is taken in its crossfire. The broom must meet the chain at the pillar's second tile (y 208..224).
            yield return Section("redcave right tentacle", () => Exec("O1 X0 >816,224 I832,208,l W200 `rc_pillar_r"));
            // Out through the middle entrance and round the Red Sea to the right entrance the tentacle has just raised.
            yield return Section("redcave out to the right entrance", () => Exec("O1 X0 oREDSEA@551,789 W30 O1 X1 oREDCAVE@712,740 W60 `rc_right_in"));
        }
    }

    /// <summary>
    /// Walkthrough Part 9b: the Red Cave's right wing. Starts inside the right entrance (screen 5,6), takes the key in (6,3), opens the
    /// double keyblock in (5,5), cuts the east tentacle in (5,4) and ends inside the left entrance (screen 1,6).
    /// </summary>
    public class RedCaveRightWing : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Red Cave: the right wing");
            // Right wing. The two Annoyers by the (5,5) keyblocks respawn every time the screen is entered: kill them from the open floor
            // below them (walking past drags them along and they swoop).
            yield return Section("redcave right wing annoyers", () => Exec("O1 X0 >872,872 W40 X1 F W30"));
            yield return Section("redcave to the steam channel", () => Exec("O1 X1 >1056,792 W10 `rc_64_in"));
            // (6,4): a water channel crossed by two steam jets. Without a raft the water drowns you after 3s, so no waiting in it: the
            // current runs north, and wading up it straight after a burst gets through before the next one (and before drowning).
            yield return Section("redcave wade the steam channel", () => Exec("O0 X0 G1057,772 s G1045,772 G1045,640 W10 `rc_64_out"));
            // (6,3): the Slasher comes down to us; killing it opens the gate to the key. Wading only for the last few tiles to the chest: in
            // the fight it would chase the Slasher out over the pool and drown.
            yield return Section("redcave east key", () => Exec("O1 X0 G1028,600 W10 g3 F W30 a1 I1024,496,d g0 a0 W30 `rc_key63"));
            // Back down the channel is against the current: ride the dust raft. The screen's dust drifts up to the channel's north end,
            // sweep it up from the bank, drop it on the water straight after a steam burst and ride south (diagonally off it at the bottom,
            // or the current carries the raft back into the lower jet).
            yield return Section("redcave raft down the steam channel", () => Exec("O1 X0 G1028,642 W220 u2 W5 u5 W30 G1028,658 u2 s u5 W15 G1028,772 G1058,780,d G1058,790 W10 `rc_64_down"));
            yield return Section("redcave right keyblocks", () => Exec("O1 X1 >872,840 W30 O1 X0 G868,822 U40 W30 U40 W30 `rc_54_in"));
            yield return Section("redcave east tentacle", () => Exec("O1 X0 I864,688,r W200 `rc_pillar_54"));
            // Walking back out through (5,5) means entering it from the north with the respawned Annoyers right at the doorway, frozen in the
            // screen transition while one flies through us: take the pause menu's way back to the entrance instead.
            yield return Section("redcave out to the left entrance", () => Exec("n0 W60 O1 X1 oREDSEA@872,1107 W30 O1 X1 oREDCAVE@392,738 W60 `rc_left_in"));
        }
    }

    /// <summary>
    /// Walkthrough Part 9c: the Red Cave's left wing. Starts inside the left entrance (screen 1,6), takes the keys in (1,3) and (0,3),
    /// opens the keyblocks in (1,5) and (1,4) and cuts the last tentacle, which raises the north entrance.
    /// </summary>
    public class RedCaveLeftWing : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Red Cave: the left wing");
            // (2,5): a ring of water round a room that a steam jet runs through. Raft over to the room, then let the Mover follow us across
            // the jet's column while it is down, and step back across it as it rises: its next lunge after us is into the burn.
            yield return Section("redcave east pool", () => Exec("O1 X1 >344,888 W10 `rc_25_in"));
            yield return Section("redcave east pool mover", () => Exec("O0 X0 ~408,856 W10 s G420,872 W30 s G372,872 W60 s1 G420,872 W150"));
            yield return Section("redcave to the two buttons", () => Exec("O1 X0 s >352,776 W10 `rc_24_in"));
            yield return Section("redcave mover on a button", () => Exec("O1 X0 |40 W30"));
            yield return Section("redcave north-west key", () => Exec("O1 X1 I272,512,d W60 `rc_key13"));
            // Back to the entrance by the menu, then west: the (0,5) Mover waits in its corridor until we stand above the jet across its
            // mouth, and follows us back down through it as it rises.
            yield return Section("redcave to the west steam room", () => Exec("n0 W60 O1 X1 >136,872 W10 `rc_05_in"));
            yield return Section("redcave west steam room mover", () => Exec("O0 X0 s G56,818 W90 s1 G56,858 W150"));
            // (0,4): a four-shooter in the middle of a ring of floor. Fight each Annoyer where none of its bullet lines cross: the lower one
            // from the doorway (8,49), the upper one from (7,41) (a soft fight first: chasing the upper one from below gets nowhere).
            yield return Section("redcave four-shooter ring", () => Exec("O1 X0 >134,782 W5 G134,770 W30 G134,782 X1 F900 X0 >120,664 W10 X1 F O1 X0 >136,616 W10 `rc_03_in"));
            // (0,3): the gate's button is on the key's side: raft round through the (1,3) pool to it.
            yield return Section("redcave west key", () => Exec("O1 X0 ~21,533 W30 I32,496,d W60 `rc_key03"));
            yield return Section("redcave left keyblocks", () => Exec("n0 W60 O1 X0 >248,824 G248,822 U40 W30 G248,776 L40 W30"));
            yield return Section("redcave west tentacle", () => Exec("O1 X0 I192,672,r W200 `rc_pillar_14"));
        }
    }

    /// <summary>
    /// Walkthrough Part 9d: the top of the Red Cave. From the left wing's last tentacle round to the north entrance it raised, east along
    /// the top to the boss, then the red key and the card in the room below it and out through its door to the Nexus.
    /// </summary>
    public class RedCaveTop : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Red Cave: the top and the boss");
            yield return Section("redcave north entrance", () => Exec("n0 W60 O1 X1 oREDSEA@224,1105 W30 O1 X1 oREDCAVE@551,581 W60 `rc_north_in"));
            yield return Section("redcave along the top", () => Exec("O1 X0 H5,0 W60 `rc_50_in"));
            // (5,0): the way to the boss is up a channel whose current runs out of its room. Sweep up the dust by the north wall, drop it in
            // the channel's inlet straight after a steam burst and ride it against the current under both jets, into the boss room.
            yield return Section("redcave raft to the boss", () => Exec("O1 X0 J56,1,l s J52,2,d D12 G852,66,d R220"));
            yield return Section("redcave boss", () => Exec("O0 X0 F W200 i W30"));
            // Below the boss room: the Sage comes over to talk, the red key waits in the corner and the chest holds a card.
            yield return Section("redcave red key", () => Exec("O1 X1 H6,1 W60 X0 I1088,288,u W120 I976,256,d W60 `rc_redkey"));
            // South of it: three tiles' wade down the stream (with the current) to the Nexus pad.
            yield return Section("redcave out to the Nexus", () => Exec("O1 X1 a1 G1028,330 a0 P1036,362 u5 W120 `rc_done"));
        }
    }
}
