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
            // The FAQ takes this side room in the main walkthrough: the Shieldy card is one of the four the card gate in Blue Forest (2) wants.
            // (1,1): the way to the key gate is down the beam column (the only link between its halves); that costs two hearts, documented
            // (g2), with the beam cost scaled down (y10) so the walk takes it instead of a 70-tile detour, and no dust fetching (p1).
            // Then (1,0): two Shieldies, three Slimes and two lasers; the chest is behind the gate that opens when they are dead.
            yield return Section("temple Shieldy card", () => Exec("g2 y10 p1 O1 @0 X1 H3,0 H2,0 H2,1 H1,1 I224,160,d W40 g0 y100 p0 @2 X0 H1,0 W5 % X1 F I176,16,d W40"));

            // Out: down through the entrance door at the bottom of (2,4).
            yield return Section("temple exit", () => Exec("O1 X1 H2,1 H2,2 H2,3 H2,4 Pb405,780 W60"));
        }
    }

    /// <summary>
    /// Walkthrough Part 13: back to the Temple once the windmill has moved its statue. The Nexus pad lands on (4,0); a running jump
    /// over the two holes on (5,0) reaches the key and the Pew Pew card (the intended route is the boulder puzzle), the key opens the
    /// (4,3) key block and the bottom door of (4,4) leads outside onto the Overworld's east woods: a loop through the laser rooms, the
    /// Annoyer/Dog rooms and back west and north to the Annoyer card chest on (5,7).
    /// </summary>
    public class Temple2 : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Temple again: Pew Pew card, then outside to the Annoyer card");
            yield return Section("temple2 nexus to the temple", () => Exec("W30 O1 X1 n0 W60 >908,1232 U20 W10 A W60 T W200 `t2_in"));
            // (a hole swallows the player only after 8 frames on it: take off a little past the first hole's edge to clear both)
            yield return Section("temple2 pew pew card", () => Exec("O1 X1 H5,0 W10 >846,20 W10 er863 W20 I912,16,d W60 T W30 I928,16,d W60 T W30 `t2_chests"));
            yield return Section("temple2 key block", () => Exec("O1 X1 >900,20 W10 el888 W20 H4,0 H4,1 H4,2 H4,3 W20 I720,496,d W60 H4,4 W30 `t2_s44"));
            yield return Section("temple2 out", () => Exec("O1 X1 >724,770 D40 W90 `t2_out"));
            // (the planner counts the checkpoint by the door as solid: step round it by hand)
            yield return Section("temple2 past the checkpoint", () => Exec("O1 X1 D10 W5 R38 W5 D50 W10 `t2_east"));
            // (7,7): down the lane between the two fast lasers' columns, which end on the wall at row 79
            yield return Section("temple2 laser lanes", () => Exec("O1 X1 H7,6 >1234,1110 >1234,1300 >1290,1330 H8,8 `t2_88"));
            // (8,7): the slow laser's column is never clear (a bullet every 20px, 20px of bullet and player): one hit, as in a human run
            yield return Section("temple2 slow laser", () => Exec("O1 X1 >1332,1298 >1332,1240 g1 O0 >1380,1240 >1380,1310 O1 g0 H9,8 H9,9 `t2_99"));
            // (8,9): three Dogs; walking past them costs a heart, fighting them four
            yield return Section("temple2 dogs", () => Exec("O1 X1 F W30 X0 H8,9 >1316,1430 W5 `t2_dogs"));
            yield return Section("temple2 annoyer card", () => Exec("O1 X1 H7,8 H7,9 H6,9 H5,9 X0 H5,8 X1 H6,8 H6,7 I912,1232,d W60 T W30 `t2_card"));
        }
    }

    /// <summary>
    /// Walkthrough Part 14: Black and White Town (SUBURB). Back through the Temple to the east woods and the town's door on (9,7), its
    /// Nexus Point, a detour through the Nexus for the Forest's Mushroom card (the town's card gate wants 16 and the route so far has
    /// 15), the gate and the maze to the health upgrade, ten townsfolk for the man in front of the house, and the Suburbanite's
    /// Corpse card through the house. The town's killers cost 6 hearts a touch and never die: the planner keeps a 30px berth.
    /// </summary>
    public class Suburb : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Black and White Town");
            yield return Section("suburb nexus to the temple", () => Exec("W30 O1 X1 n0 W60 >908,1232 U20 W10 A W60 T W200 `sb_temple"));
            yield return Section("suburb out of the temple", () => Exec("O1 X1 H4,1 H4,2 H4,3 H4,4 >724,770 D40 W90 D10 W5 R38 W5 D50 W10 `sb_out"));
            yield return Section("suburb east woods to the town", () => Exec("O1 X1 H7,6 H8,6 H9,6 H9,7 oSUBURB W90 `sb_in"));
            yield return Section("suburb nexus point", () => Exec("O1 X1 H0,1 H1,1 H2,1 H2,2 H2,3 W30 `sb_pad"));
            // The Forest's (3,2) card: up the drowning channel on a raft of the (3,4) dust, swatting its Slime and Annoyer from the raft
            yield return Section("suburb mushroom card", () => Exec("O1 X1 >396,560 U20 W10 A W60 T W200 >828,1056 U20 W10 A W60 T W200 O1 X0 H0,6 H0,7 H1,7 H2,7 H3,7 H3,6 H3,5 H3,4 J37,48,d W20 b540,400 W10 I528,384,d W60 T W30 `sb_mush"));
            yield return Section("suburb back to town", () => Exec("O1 X1 n0 W60 >828,1168 U20 W10 A W60 T W200 `sb_back"));
            // (the card gate's sentinel is a 10px strip in its middle: stand under that and press)
            yield return Section("suburb card gate", () => Exec("O1 X1 H3,3 H4,3 W20 >760,562 R7 U10 A W30 T W90 `sb_gate"));
            yield return Section("suburb health upgrade", () => Exec("O1 X1 i W60 T W60 `sb_hp"));
            // (2,2) 2, (1,2) 2, (0,2) 1, (0,3) 2, (0,4) 2, (4,4) 1: the (3,3) one walks by a killer
            yield return Section("suburb townsfolk", () => Exec("O1 X0 H2,2 :10 H1,2 :10 H0,2 :10 H0,3 :10 H0,4 :10 H0,3 H0,2 H1,2 H2,2 H2,3 H3,3 :10 H4,3 H4,4 :10 `sb_k10"));
            yield return Section("suburb corpse card", () => Exec("O1 X0 H4,3 H3,3 I560,576,d W30 T W90 W60 >564,578 U30 W90 H1,7 I256,1184,d W60 T W30 `sb_card"));
        }
    }

    /// <summary>
    /// Walkthrough Part 15: the Apartments. First a detour back into Red Cave's last chest for Broom Widen (the Apartments' first pillar
    /// puzzle needs it), then in through the Suburb's pad: the moving-platform entrance, the pillar room, the (2,3) Silverfish room
    /// (a fish herded onto one button while we stand on the other), the Dash Trap room, the rat-and-crystals room for the first key,
    /// and south past the Gas Guy room to the Silverfish card under the spike roller.
    /// </summary>
    public class Apartment : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Apartments (Broom Widen from Red Cave first)");
            yield return Section("apartment to red cave", () => Exec("W30 O1 X1 n0 W60 >588,896 U20 W10 A W60 T W200 >596,852 W30 oREDCAVE@551,883 W90 `ap_rc"));
            // (3,6): a 2-hole pit taken with a running jump; the Widen chest is on (4,6)
            yield return Section("apartment widen", () => Exec("O1 X0 H3,6 >528,1027 W5 er545 W20 H4,6 I768,976,d W60 T W30 `ap_widen"));
            yield return Section("apartment entrance", () => Exec("W30 O1 X1 n0 W60 D30 W90 U4 W60 n0 W120 >828,1168 U20 W10 A W60 T W200 O1 X0 H2,2 H2,1 H2,0 H3,0 W20 oAPARTMENT W90 `ap_in"));
            // Widen equipped; the platform ride to (2,4); its pillar switch hit twice (1s apart) with the wide broom; up into (2,3).
            // (2,3): the fish facing left first runs right along row 34 to the couch (col 26); scared from below it runs up col 26 onto
            // the (26,32) button and stays; we stand on the (21,38) one: two buttons at once open the west gate for good.
            // Everything inside is one section: a death here respawns at the entrance with the rooms' state kept.
            yield return Section("apartment silverfish card", () => Exec(
                ";2 W10 X0 U24 W10 t20 W10 >404,818 U40 W10 J26,47,u W90 >402,723 R2 D1 u5 W30 >402,620 W30 " +
                "G404,595,y G340,595 G340,550,y W40 G340,595,y G418,595 G418,570,y W40 G339,595,y G339,607,y W30 " +
                // (1,3): round the still water by the bottom row and jump the row the top Dash Trap watches
                "O1 X0 H1,3 W10 G291,547 G291,596,y G196,596 G196,550,y G228,550 G228,530,y eu514 U30 W30 " +
                // (1,2): clear it for the north gate; (2,1): the permanent button by raft; (3,1): hit the crystal switch so the rat
                // walks the top row into the far column, again as it heads down so it reaches the permanent button, again to walk the top
                // row ourselves to the other button; the key chest behind both gates
                "O1 X1 F1200 W30 H1,2 G240,380 U60 W30 H2,1 W30 >403,179 W30 X0 H3,1 W30 >499,274 W10 R2 A W70 {Rat,590,170,640,232 W5 A W30 " +
                "{Rat,600,280,640,310 W70 A W30 G499,178,y G563,178 G563,242,y G581,242 W30 G563,242 G563,178,y G499,178 G499,290,y G531,290 G531,230,y I528,208,d W60 T W30 " +
                // (3,2) right side by a running jump, (3,3) down col 37 past the Dog, (3,4) Gas Guy and Silverfish
                "O1 X0 H3,2 >566,450 er583 W20 >613,500 W10 d600 >597,620 H3,4 W10 } W10 " +
                // (3,5): the hidden spike roller drops on whoever crosses row 51 and sweeps the room down to row 58, over the chest's
                // only standing spot. Let it go by from the doorway, flip the (32,58) switch (lowers the (34,51) crystal), leave and come
                // back (the roller resets), and this time dash right past the crystal and down into the chest nook ahead of it, open the
                // chest and jump back up over the roller as it comes.
                ">515,770 W10 G515,812,y G515,803,y W420 G515,866,y ed866 W5 G515,914,y D1 A W70 G515,898,y eu898 W5 G515,818,y U40 W30 " +
                "G515,800,y G515,816,y G564,816 G564,836,y G579,850 G579,882,y ed882 W5 D20 R12 U4 A W30 T W5 L12 U12 {FallingSpikeRoller,496,893,640,950,600 eu W20 `ap_sfcard"));
        }
    }
}
