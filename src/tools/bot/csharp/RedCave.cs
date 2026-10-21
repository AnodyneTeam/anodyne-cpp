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
}
