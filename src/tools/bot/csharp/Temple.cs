using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>Walkthrough Part 4: the temple (BEDROOM). Every block replays from the top after a death (checkpoint at the entrance).</summary>
    public class Temple : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Temple: save at the entrance");
            yield return Exec("O0 V");

            // Shieldy room (2,2): its three Shieldies only take damage from a downward swing. The room has a checkpoint.
            yield return Section("temple Shieldy room", () => Exec("X0 H2,2 F X1 V"));

            // West wing: the key chest on (0,4) behind the pit column.
            yield return Section("temple key 1", () => Exec("H0,4 I16,656,d W20"));

            // Screen (1,2): the left PewLaser column is too dense to cross. Pick up the dust beside the boulders (from its east
            // side) and place it on the beam's first tile (standing on the dust's old tile, facing the laser column).
            // North-west wing: (0,2) and (0,3) are full of Shieldies/Annoyers; the gate on (0,3) opens when they are dead,
            // and the chest on (1,3) holds the second key.
            yield return Section("temple key 2", () => Exec("X0 O1 K X1 H0,2 H0,3 H1,3 I176,496,d W20"));

            // Back to the (2,2) checkpoint to save with both keys in the inventory (deaths then respawn there, not at the entrance).
            yield return Section("temple resave", () => Exec("O1 X0 H0,2 # H2,2 F X1 V"));

            // (0,1): two buttons open the gate east; a Shieldy has to be shoved onto one while we stand on the other.
            // NOTE: the small-key gate on (1,1)'s north wall (chest room (1,0): 2 lasers, 2 Shieldies, 2 Slimes) is a side room; it would
            // spend a key the walkthrough needs later, so it is left for the 100% route.
            // (1,1) -> (2,1): plug the laser over the gap column (dodge off for the dash up it), cross east to the room whose button
            // opens its gates, step on it, slip through the bullet row and the corridor down to the (2,2) checkpoint and save.
            const string ToEast = "X0 O1 K X1 H0,2 F X0 # H1,1 O1 l16,17,12 H2,1 B336,176 H2,2 F V";
            yield return Section("temple east", () => Exec(ToEast + " X1"));

            // East wing: (3,2) Shieldies/Annoyer, then south (3,3) and west (2,3) to the chest with the Red Slime card (@448,592).
            yield return Section("temple red slime card", () => Exec("O1 X1 H3,2 H3,3 H2,3 I448,592,d W60"));

            // Back up to (3,2): the key block (@544,320) on its north wall opens with the second key.
            yield return Section("temple key block", () => Exec("O1 X1 H3,3 H3,2 I544,320,d W60"));

            // (3,1): Annoyers and side lasers. Its west exit leads into a nook of (2,1) where a Slime sits; killing it opens (2,1)'s north gate.
            yield return Section("temple slime nook", () => Exec("g1 O1 @0 X1 H3,1 H2,1 @2 X0"));

            // Walkthrough: east, south, west, north (through the (2,1) corridor, killing its Shieldy), north into the Seer's room (2,0):
            // the boss fight, then the health upgrade it leaves behind.
            yield return Section("temple seer", () => Exec("g0 O1 X1 H3,1 H3,2 H2,2 H2,1 H2,0 W200 P370,66 W80"));

            // East of the boss room: save at the checkpoint, talk to the sage, take the green key and open the Seer card chest.
            yield return Section("temple sage room", () => Exec("O1 X0 H3,0 V I547,83,l W30 I596,96,l W60 I544,32,d W60 H4,0 W30"));

            // (4,0) holds a Nexus Point (activated just by loading the screen). Back west to the (1,1) key gate, which the green key opens:
            // north of it, (1,0) has two Shieldies and two Slimes (plus two lasers) guarding the Shieldy card chest. Fighting with the
            // dodge off and the lasers left alone works best here: they do the work on the Shieldies.
            if (BotRunner.Full100)
            {
                yield return Section("temple Shieldy card", () => Exec("O1 @0 X1 H3,0 H2,0 H2,1 H1,1 I224,160,d W40 X0 @2 H1,0 O0 F O1 I176,16,d W40 X1"));
            }

            // Out: down through the entrance door at the bottom of (2,4).
            yield return Section("temple exit", () => Exec("O1 X1 H2,1 H2,2 H2,3 H2,4 Pb405,780 W60"));
        }
    }
}
