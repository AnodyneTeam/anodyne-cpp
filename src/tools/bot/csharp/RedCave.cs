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
            yield return Section("redcave buttons room", () => Exec("O1 X0 H3,2 |60"));
            yield return Section("redcave west", () => Exec("O1 X1 H2,2 F H1,2"));
            yield return Section("redcave north to worm", () => Exec("O1 X0 H1,1"));
            yield return Section("redcave stab the worm", () => Exec("g3 O1 X0 I224,224,l I224,224,l I224,224,l W60 g0"));
            // The reapers (Slashers) cannot be fought hit-for-hit in these narrow rooms: a hit or two is taken and the hearts they drop win it back.
            yield return Section("redcave reaper 1", () => Exec("g3 O1 X1 F g0"));
            yield return Section("redcave east 1", () => Exec("O1 X0 H2,1 W90 g3 X1 F g0 `rc_east1"));
            yield return Section("redcave key chest", () => Exec("O1 X0 H3,1 W60 I512,176,d W60 `rc_key1"));
            yield return Section("redcave back to the button room", () => Exec("O1 X1 H3,2 W30 `rc_hub1"));
            yield return Section("redcave water kills the mover", () => Exec("O1 X0 m60 W30 `rc_hub2"));
        }
    }
}
