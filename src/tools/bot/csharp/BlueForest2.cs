using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>Walkthrough Part 5: Blue Forest (2), from the temple exit down to the gate south of the health upgrade.</summary>
    public class BlueForest2 : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Blue Forest 2: Nexus Point east, then save at the checkpoint");
            yield return Section("forest2 nexus point", () => Exec("O1 X1 H5,6 W30 H4,6 V"));

            // West, south, south, west, north, west, west: the big card gate on (0,7) needs four cards.
            yield return Section("forest2 card gate", () => Exec("O1 X1 H4,7 H4,8 H3,8 H3,7 H2,7 H1,7 H0,7 I40,1120,d W90"));

            // North through three fighting rooms, east, then south to the health cicada on (1,5).
            yield return Section("forest2 north", () => Exec("g3 O1 X1 H0,6 V H0,5 H0,4 H0,3 H1,3 H1,4 H1,5 P256,896 W600 T"));

            // South: four slimes guard the gate on (1,6); then west and down the left column to the green-key gate on (0,9),
            // and through the door below it into the Fields.
            yield return Section("forest2 slimes", () => Exec("O1 X1 H1,6"));
            yield return Section("forest2 south", () => Exec("O1 X1 H0,6 H0,7 H0,8 H0,9 I72,1536,u W120 P76,1560 D60 W120"));
        }
    }
}
