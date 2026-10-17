using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>
    /// Walkthrough Part 8: the Red Sea. The Nexus Point (screen 3,2 is activated by loading it), the Red Walker card chest, the buttons that
    /// open the way, and the dungeon (Red Cave) entrance. Semantic steps: `+` plans the map and opens every reachable card chest / presses buttons.
    /// </summary>
    public class RedSea : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Red Sea: Nexus Point, Red Walker card, into the Red Cave");
            yield return Section("redsea nexus point", () => Exec("O1 X1 H3,2 W30"));
            yield return Section("redsea card", () => Exec("O1 X1 + W60"));
            yield return Section("redsea dungeon", () => Exec("O1 X1 oREDCAVE@551,739"));
        }
    }
}
