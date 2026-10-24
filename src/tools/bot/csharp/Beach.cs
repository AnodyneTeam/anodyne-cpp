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
            // A death on the way respawns at the Fields checkpoint: walk back through the left door into the Beach first.
            yield return Section("beach fisherman card", () => Exec("O1 X0 >706,520 I703,496,d W150 T W30"),
                () => Map == "FIELDS" ? Exec("O1 X1 >190,892 P175,890 L50 W300") : Exec("W1"));
        }
    }

    /// <summary>
    /// Walkthrough Part 7, the rest: the card gate on (0,4), the Health Upgrade on (1,2), the button/gate rooms in the east, and the
    /// Fisherman on (7,3), whose whirlpool leads to the Red Sea. Written in semantic steps: `+` opens every reachable card chest and
    /// presses every button, `i` collects the map's Health Cicada, `>x,y` walks to a world point across screens.
    /// </summary>
    public class Beach2 : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Beach 2: card gate, Health Upgrade, buttons");
            yield return Section("beach card gate", () => Exec("O1 X1 >32,740 I24,720,d W120"));
            // (1,1) has two bouncing dash traps side by side along the bottom of its corridor, and they are on the left edge just as the
            // screen scroll ends: come in along the top row, above their lanes. From there on the slimes and burst plants are left alone
            // (the dodging walks past them; fighting them in that cramped corridor cost more hearts than it won).
            yield return Section("beach health upgrade", () => Exec("O1 X1 >150,226 H1,1 X0 i"));
            // (no auto-fight: the way to the (4,2) button crosses the burst plant, slimes and Annoyer of (4,1), and walking past them is safer)
            yield return Section("beach buttons", () => Exec("O1 X0 +"));
        }
    }

    /// <summary>Part 7, the end: the Fisherman on (7,3) (two broom hits), then the whirlpool he was fishing in takes us to the Red Sea.</summary>
    public class Beach3 : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Beach 3: the Fisherman and the whirlpool");
            yield return Section("beach to the fisherman", () => Exec("O1 X1 >1168,562 W20"));
            yield return Section("beach fisherman", () => Exec("I1200,576,u W35 A W35 T W200"));
            yield return Section("beach whirlpool", () => Exec("O1 X0 G1203,572 D50 W200"));
        }
    }
}
