using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>Walkthrough Part 6: the Lake (FIELDS).</summary>
    public class Fields : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Fields: Miao, Nexus Point, the Nexus button");
            // Mitra's checkpoint on (5,4) first: deaths on the way then respawn here instead of far back in the Blue Forest.
            yield return Section("fields checkpoint", () => Exec("O1 X1 H5,2 H5,3 H5,4 V"));
            yield return Section("fields miao", () => Exec("O1 X1 H5,2 H5,3 H4,3 I672,528,d W20 T W30"));

            // The Nexus Point (activated by loading its screen); step on the pad and press Accept: Miao objects, then we are teleported.
            yield return Section("fields nexus", () => Exec("O1 X1 H5,3 H6,3 P1034,574 W20 A W60 T W200"));
            // Push the button (opens the gates south); the middle door leads back to the Fields.
            yield return Section("fields nexus back", () => Exec("B688,1072 W60 P712,1060 U30 A W300"));

            // Pick Miao up again, then east to Goldman, who hides in the box; the Icky in it is killed.
            yield return Section("fields miao 2", () => Exec("O1 X1 H5,3 H4,3 I672,528,d W20 T W30"));
            yield return Section("fields goldman", () => Exec("O1 X1 H7,3 H7,2 I1152,432,d W20 T W30 I1152,412,d W60 T W60 F"));

            // The button on (8,2) opens the gates; the cave door sits in the walled yard of (10,1).
            yield return Section("fields cave", () => Exec("O1 X1 H8,2 T B1296,384 W30 H9,2 H9,1 H10,1 P1686,248 U40 W120"));
            // Twelve dust go into the hole; Goldman then walks off the chest. The Goldman card.
            yield return Section("fields goldman card", () => Exec("k W60 T W30 I1360,16,d W120 T W30"));

            // Out of the cave, back to the Nexus Point and through the Nexus door (the world refreshes: Finty shows up).
            yield return Section("fields to nexus", () => Exec("O1 X1 P1344,128 D50 W200 H9,1 H9,2 H8,2 H7,2 H7,3 H6,3 P1034,574 W20 A W60 T W200"));
            yield return Section("fields from nexus", () => Exec("P712,1060 U30 A W300"));

            // Finty (the shopkeeper) is given the box: he hands over the biking shoes, which go to Mitra for the jump boots.
            // Mitra only trades once her first (introduction) chat is over and the screen has been reloaded.
            yield return Section("fields finty", () => Exec("O1 X1 H7,3 H7,4 I1184,704,u W20 T W30 I1184,704,u W20 T W60"));
            yield return Section("fields mitra", () => Exec("O1 X1 H6,4 H5,4 I896,704,d W20 T W60 T W30 H5,5 H5,4 I896,704,d W20 T W60 T W30 V"));

            // The lake: the planner's route is the walkthrough's (spike pocket on (7,6), dust rafts across the island chain down to
            // Mitra's south bank); the button there opens the gate in the channel.
            yield return Section("fields lake", () => Exec("O1 X1 >1056,1203 W90"));

            // The southern chests: the Miao the Cat card and the Mitra card (the walkthrough's long way over dust rafts, spikes and the
            // gate in the channel). `+` plans the whole map (rafts, jumps, buttons, shut gates) and opens every reachable card chest.
            yield return Section("fields southern cards", () => Exec("O1 X1 + W60"));

            // The pause menu's map tab warps back to the Nexus; its middle door returns to the Fields pad (no trip back over the lake).
            yield return Section("fields menu to nexus", () => Exec("n0 W60"));
            yield return Section("fields nexus to pad", () => Exec("P712,1060 U30 A W300"));

            // With the boots, narrow rivers are jumped. West along the south shore to the left door into the Beach.
            yield return Section("fields to beach", () => Exec("O1 X1 >190,892 P175,890 L50 W300"));
        }
    }
}
