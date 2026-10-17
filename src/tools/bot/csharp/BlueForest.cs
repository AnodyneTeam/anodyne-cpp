using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>Walkthrough Part 3: Blue Forest (OVERWORLD) -> Edward card chest -> Sage -> enter the temple (BEDROOM).</summary>
    public class BlueForest : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Blue Forest: Edward card");
            // Arrive on (2,8). North, west, west, east back through the lower passage, then south to the chest on (1,9).
            yield return Exec("Qn Qw Qw Qe Qs Qs I176,1536,r W30");

            yield return Say("Blue Forest: Sage, then the temple door");
            // The Sage stands in the corridor to the temple (4,6); talk to him, then walk up the stairs into the door.
            yield return Exec("P704,1090 I714,1067,l W30 P714,1030 G716,1030 U60");
            yield return Until(() => Map == "BEDROOM", 600, "enter the temple");
            yield return Wait(30);
        }
    }
}
