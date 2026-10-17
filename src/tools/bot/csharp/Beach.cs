using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>Walkthrough Part 7: the Beach (first steps: Nexus Point and the Fisherman card).</summary>
    public class Beach : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Beach: Nexus Point, then the chest on (4,3)");
            yield return Section("beach nexus point", () => Exec("O1 X0 >1030,420 W30"));
            yield return Section("beach fisherman card", () => Exec("O1 X0 >706,520 I703,496,d W150 T W30"));
        }
    }
}
