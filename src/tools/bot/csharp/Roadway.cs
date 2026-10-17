using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>Walkthrough Part 2: Roadway (STREET) -> broom -> key -> lock -> teleporter to the Blue Forest.</summary>
    public class Roadway : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Roadway: north, east, east");
            // Entry screen (1,5). The button on (3,4) opens the gate east; the chest in the alcove on (4,4) holds the broom.
            yield return Exec("Qn Qe Qe P578,704 W40 Qe W20 I768,672,d W30");

            yield return Say("Roadway: kill the slimes, key chest");
            // West along the road; the key chest on (0,4) sits behind a gate that opens when the room's enemies are dead.
            yield return Exec("F Qw F Qw F Qw F Qw F I16,656,d W20");

            yield return Say("Roadway: open the lock, north, east to the teleporter");
            yield return Exec("Qe F I240,672,d W30 Qn F Qn F Qn F Qe F Qe F P530,226 W200");
            yield return Until(() => Map == "OVERWORLD", 600, "arrive in the Blue Forest");
        }
    }
}
