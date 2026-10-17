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
            yield return Section("temple resave", () => Exec("X0 H2,2 V X1"));

            // (0,1): two buttons open the gate east; a Shieldy has to be shoved onto one while we stand on the other.
            // (1,1): the small-key gate on the north wall leads on towards the upper rooms (the hole by the east exit is not a
            // way across). One self-contained section: after a death the rooms reset, so it redoes the dust and the fights.
            const string ToKeyGate = "X0 O1 K X1 H0,2 F X0 # H1,1 I224,160,d W40 X1";
            yield return Section("temple buttons + key gate", () => Exec(ToKeyGate));

            // TODO: the upper rooms (2,0)/(2,1) (button @336,176), the Red Slime card (chest @448,592), the second key block (@544,320), the
            // checkpoint @496,128 and the Seer. An attempt at (2,0)/(2,1) failed: the chain up to the key gate leaves the bot at 1 HP
            // (lasers on (1,1), Shieldies), so it dies on arrival and loops. Needs better laser dodging / HP handling first.
        }
    }
}
