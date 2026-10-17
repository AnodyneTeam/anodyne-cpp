using AnodyneSharp.Input;
using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>Nexus: walk north, let the Sage talk, then west to the Roadway (STREET) pad (walkthrough Part 1, end).</summary>
    public class NexusToRoadway : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Nexus: north to the Sage");
            yield return GoTo(704, 1200);
            yield return ReadDialogue();

            yield return Say("Nexus: west to the Roadway pad");
            yield return GoTo(580, 1231);
            yield return WalkTo(563, 1231);
            yield return Hold(30, KeyFunctions.Up);
            for (int i = 0; i < 2; i++)
            {
                yield return Tap(KeyFunctions.Accept);
                yield return Wait(8);
            }
            yield return Until(() => Map == "STREET", 600, "arrive in the Roadway");
            yield return Wait(60);
        }
    }
}
