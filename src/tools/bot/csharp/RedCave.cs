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
}
