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
            // (3,6): a 2-hole pit taken with a running jump; the Widen chest is on (4,6), between two Slashers: fight them (walking past
            // with auto-fight off only collected their cuts), with the usual Red Cave Slasher budget
            yield return Section("apartment widen", () => Exec("O1 X0 H3,6 >528,1027 W5 er545 W20 g3 X1 H4,6 F X0 g0 I768,976,d W60 T W30 `ap_widen"));
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
            // Second key: the (0,2) pillars before its gate are up whenever the (0,1) ones on the way in are down, so flip twice:
            // (3,1)'s crystal to get into (0,1), then (2,0)'s via the top corridor (Gas Guy fought in it). (2,0)'s rat runs up col 21,
            // along row 1 and down our col 23 (the walker predicts it and the dash traps' charges). (0,2): `[` finds the dust move that sends
            // the rat round over one button while we hold the other.
            yield return Section("apartment key 2", () => Exec(
                "W30 n0 W120 X0 t20 W10 >404,818 U40 W10 J26,47,u W90 >402,723 R2 D1 u5 W30 >402,620 W30 O1 X0 >244,380 U60 W30 H2,1 W30 >403,179 W30 H3,1 W10 >499,274 W10 R2 A W60 X0 H3,2 H3,3 W10 G500,546,y L40 W20 " +
                "G451,546 G451,514,y G355,514 G355,546,y G300,546 W10 H1,3 W10 G291,547 G291,596,y G196,596 G196,550,y G228,550 G228,530,y eu514 U30 W30 >244,380 U60 W30 " +
                "H0,1 W20 >170,178 W10 F1800 W30 >387,34 R1 A W30 >19,330 W30 [ W10 I16,496,d W60 T W30 `ap_key2"));
            // Gas Guy card: back up through (0,2) and the top corridor to (2,0)'s key block; (1,0)'s Shieldies only come to the col-18 gap when
            // their pillars let them, so flip its crystal twice (the second time with the top one right of col 13, so the rising pillar
            // shuts it in on the gap's side); stairs to (6,0); TeleGuys, rats; in (5,0) step back into the doorway first so the Gas Guys
            // come in a line; stairs to (0,0) and its chest.
            // TODO: (5,0)'s Dog and Gas Guys can still cost 2 hearts (budgeted).
            yield return Section("apartment gas guy card", () => Exec(
                "g2 X1 W10 >371,140 >340,82 L40 W30 >300,82 W10 >291,34 U1 A W30 F1800 W30 >291,34 U1 {Shieldy,240,90,252,105 A W30 F W60 " +
                ">195,40 U30 W120 H6,1 W30 H5,1 W30 >868,150 W200 F W30 >819,40 U30 W120 I128,64,d W60 T W30 `ap_gascard"));
            // East wing: from (0,0) back to the entrance and up the (3,1) crystal to the (3,0) stairs, which come out in (6,0); (7,0)'s rat
            // walks onto its button by itself; hit (8,0)'s crystal switch on the way (its pillar would block the way back); in (7,1) `[`
            // sends the rat over one button while we hold the other.
            yield return Section("apartment east wing", () => Exec(
                "W5 >131,18 W10 n0 W120 X0 t20 W10 >404,818 U40 W10 J26,47,u W90 >402,723 R2 D1 u5 W30 >402,620 W30 O1 X0 >244,380 U60 W30 " +
                "H2,1 W30 >403,179 W30 H3,1 W10 >499,274 W10 R2 A W60 X1 H3,0 W30 >611,130 U30 W120 H7,0 W30 >1139,82 L1 A W20 " +
                "{Rat,1183,40,1192,60,1800 A W200 >1379,82 U1 A W30 >1219,170 W5 [ W30 `ap_east"));
            // (8,1): the propelled platform over the pit to its button; (8,2) fight; (8,3) `]` scares the silverfish onto the button
            // that opens the key chest.
            yield return Section("apartment key 3", () => Exec(
                ">1283,210 W10 (82,14 W10 G1379,226 G1379,242 W10 X1 H8,2 W30 F1800 W30 X1 H8,3 W30 ] W30 I1312,496,d W60 T W30 `ap_key3"));
            // (7,3): the slashers, then a running jump into the water (too far to wade) to the dust raft and across to the button;
            // (6,3), (6,4)'s key block. TODO: the slashers still cost up to 3 hearts (budgeted).
            yield return Section("apartment water", () => Exec(
                "g3 X1 H7,3 W30 F1800 W30 H7,3 W30 >1139,578 W10 er G1187,578 G1187,546 G1142,546 W10 G1123,546 W30 X0 W2 H6,3 W30 " +
                "X1 H6,4 W30 F1800 W30 `ap_water"));
            // (7,2): its key block, then the down door to (2,2): its two buttons by the two propelled platforms, and back up.
            yield return Section("apartment twin platforms", () => Exec(
                "X0 H7,2 W30 >1139,370 L30 W30 >1203,410 U20 W120 J27,26,l W20 J21,26,u W20 (21,25 W10 >339,338 W10 >451,338 W20 " +
                "J28,21,d W20 >339,338 J21,21,d W20 (21,22 W10 >403,418 U20 W120 `ap_twin"));
            // (6,2) fight; (5,2) `[` rat button; (5,3) the crystal maze (`_` plans the switch flips); (4,3) and the (4,2) door down to (4,7).
            // TODO: (6,2)'s dash traps and (4,3)'s silverfish (fought by walking at them) can cost 2 hearts (budgeted).
            yield return Section("apartment maze", () => Exec(
                "g2 X1 H6,2 W30 F1800 W30 X0 H5,2 [ W30 X0 H5,3 W20 _803,610 W20 X1 H4,3 W30 F1800 W20 H4,2 W20 >771,338 U40 W120 `ap_maze"));
            // (5,7) silverfish button (a Gas Guy in the room is fought first), (5,6), (6,6)'s key chest.
            // TODO: (5,6)'s dash traps and (6,6)'s slasher can still cost 2 hearts (budgeted).
            yield return Section("apartment south key", () => Exec(
                "g2 X0 H5,7 ] W30 X1 F1800 W20 X1 H5,6 W30 F W30 X1 H6,6 W30 F W30 I1088,976,d W60 T W30 `ap_south"));
            // (6,7): the silverfish onto its button by pillar flips (`]`), the key block, the (7,7) checkpoint and the Watcher (7,6),
            // then its health cicada.
            yield return Section("apartment watcher", () => Exec(
                "X0 H5,6 W10 H5,7 W10 H6,7 W20 ] W30 >1091,1138 R40 W30 V W20 X1 H7,6 W120 i W60 `ap_watcher"));
            // (8,7)'s card, and out to the Nexus.
            yield return Section("apartment last card", () => Exec("H8,7 W20 I1376,1168,d W60 T W30 n0 W120 `ap_done"));
        }
    }

    /// <summary>
    /// Walkthrough Part 16: the Hotel. Its Nexus door stays dark until the Hotel's own pad has been touched, so the way in is on foot:
    /// the Crowd's pad, the Crowd's top door into the Cliffs' east side, up to the Cliff top's door into Space, Space's pad on the
    /// way, and Space's top door down onto the Hotel's roof.
    /// </summary>
    public class Hotel : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Hotel (on foot through the Cliffs and Space)");
            // The Apartments end at their entrance: out to the Suburb, to the Nexus, and its Crowd door.
            yield return Section("hotel nexus to the crowd pad", () => Exec("W30 O1 X1 >396,930 D30 W90 n0 W120 >906,900 U20 W10 A W60 T W200 `ho_crowd"));
            yield return Section("hotel cliff top", () => Exec("O1 X1 >1528,662 U30 W90 D20 W20 H8,1 >1405,214 R40 W90 `ho_space"));
            yield return Section("hotel space", () => Exec("O1 X1 H5,2 P870,392 W60 H5,0 >876,24 U40 W120 `ho_in"));
            // Roof west to the (0,0) door, which drops into (2,2).
            yield return Section("hotel roof to (2,2)", () => Exec("O1 X1 H1,0 H0,0 W10 >56,120 U30 W90 `ho_22"));
            // (2,3): jump the holes to the dust, carry it down onto the (28,36) LEFT platform and ride it west. Going into (1,3) over its
            // gate holds the gate open.
            yield return Section("hotel (2,3) dust platform to (1,3)", () => Exec("O1 X0 H2,3 W10 G386,514 W5 er414 W20 J28,32,u+ W10 " +
                "G450,530 W5 ed530 W20 J28,35,d- W20 (28,36 W10 G340,562 W5 L40 W30 `ho_13"));
            // (1,3)'s slimes open its south gates; (1,4)'s west column leads down to the (1,5) button, which opens (1,5)'s east gate.
            yield return Section("hotel (1,3) slimes and the (1,5) button", () => Exec("O1 X1 F W90 X0 >180,850 W30 `ho_15"));
            // Back up and through (1,3)'s other gate onto (1,4)'s button (opens the gate to (2,4)), then drop over the holes to (1,5).
            yield return Section("hotel (1,4) button and drop", () => Exec("O1 X1 >276,658 W20 >307,750 W5 G292,750 W5 ed W40 `ho_14"));
            // (1,5): hop down past the frogs and over the hole to the open east gate, into (2,5)'s west column.
            yield return Section("hotel (1,5) frogs to (2,5)", () => Exec("O1 X0 >245,866 W5 ed W40 G245,915 W5 er W40 >310,915 W5 R40 W30 `ho_25"));
            // (2,5): the checkpoint, the button that opens the gates from (2,4), and the Hotel's Nexus pad.
            yield return Section("hotel nexus pad", () => Exec("O1 X0 >356,886 W20 >338,818 W40 >441,872 W60 `ho_pad"));
        }
    }

    /// <summary>
    /// The Hotel's keys (the dungeon is one big puzzle; see the hotel-route notes): from the pad, the menu warp back to the (2,2) entrance.
    /// </summary>
    public class Hotel2 : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Hotel's keys");
            yield return Section("hotel2 back to the entrance", () => Exec("W30 O1 X0 n0 W120 `h2_22"));
            // (2,3): jump the holes east (the dust stays for later), down past (28,34) and out east to (3,3).
            yield return Section("hotel2 (2,3) east to (3,3)", () => Exec("O1 X0 H2,3 W10 G386,514 W5 er414 W20 G450,530 W5 ed530 W20 R40 W30 `h2_33"));
            // (3,4): its dust onto the dead DOWN platform beside it, ride it down, out south to (3,5) and east to (4,5).
            yield return Section("hotel2 (3,4) dust platform", () => Exec("O1 X1 H3,4 W10 X0 J37,41,r+ W10 J37,41,l- W20 (36,41 W10 G600,770 W5 D30 W30 " +
                ">642,818 W10 `h2_45"));
            // (4,5), first visit: its dust onto the DOWN platform to the pocket, hop the hole to the bullet Slimes and kill them (that opens the
            // north gate for good), their dust back over the hole onto the UP platform, and up. A fight may end with us back in the pocket.
            yield return Section("hotel2 (4,5) slimes", () => Exec("O1 X0 J41,51,r+ W10 J42,51,d- W20 (42,52 W10 )712,920,40 G690,915 W5 er697 W20 G752,915 X1 F W60 X0 " +
                "'43,57,5 )712,920,40 G690,915 W5 er697 W30 J47,58,r+ W10 G722,915 W5 el W30 J43,57,u- W20 (43,56 W10 `h2_45b"));
            // (4,5) again: out west and back puts its dust back; onto the RIGHT platform this time, across to the open gate, and (4,4)'s Dog.
            yield return Section("hotel2 (4,5) to (4,4) and its dog", () => Exec("O1 X0 G691,822 W5 G660,822 W5 G610,820 W20 G660,820 W20 J41,51,r+ W10 " +
                "J42,51,r- W20 (43,51 W10 G760,820 W5 G771,818 W5 U60 W30 X1 F W60 `h2_44"));
            // (4,3): the key chest past the Annoyers and the laser lanes (a chest opens from below).
            yield return Section("hotel2 (4,3) key", () => Exec("O1 X1 H4,3 W20 F W30 I768,496,d W60 T W30 `h2_key1"));
        }
    }

    /// <summary>The Hotel's floor 3 (by the floor-4 elevator, which key 1 opens the way to) and on through door 27 into the east wing.</summary>
    public class Hotel3 : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Hotel: floor 3 and into the east wing");
            // Back to the entrance, (2,3)'s active platform down, (2,4)'s spring to the bottom and into (2,5) (its gates are open now).
            yield return Section("hotel3 entrance to (2,5)", () => Exec("W30 O1 X0 n0 W120 H2,3 W10 (25,33 W10 D20 W30 >387,656 W10 ed W120 D30 W30 `h3_25"));
            // Key 1 into (2,5)'s keyblock, the only way to the elevator from this side; ride it to floor 3 (the third of its floors).
            yield return Section("hotel3 keyblock and elevator to floor 3", () => Exec("O1 X0 >355,914 W5 G355,914,kr W30 >392,918 W10 U40 W30 " +
                "D1 W10 D1 W10 A W150 `h3_f3"));
            // (3,11): its Gas Guy opens the gate to key 2.
            yield return Section("hotel3 (3,11) gas guy and key 2", () => Exec("O1 X1 H3,11 W10 F W30 X0 I608,1776,d W60 T W30 `h3_key2"));
            // Up (2,11)'s west pillar column (do not touch the pillar switch: the pillars are down as things stand), jump (2,10)'s hole,
            // key 2 into its keyblock, (1,10)'s water down to door 27, which comes out in the east wing's (7,3).
            yield return Section("hotel3 keyblock and door 27", () => Exec("O1 X0 D20 W10 >356,1778 W5 U60 W20 G356,1735 W5 eu W30 U10 G355,1682 W10 " +
                "G339,1666 W5 G339,1666,kl W30 L40 W20 >235,1700 W5 D150 W30 >180,1890 W10 U30 W120 `h3_east"));
        }
    }

    /// <summary>
    /// The Hotel's east wing (goal-level only: screens, chests, doors, lifts; the planner does the walking, leaping, wading and rafting):
    /// key 3, door 30 to (9,5), round by (9,6)-(9,8) to (8,8), whose slime opens the boss gates' kill half, and the spring into the
    /// floor-1 elevator pocket.
    /// </summary>
    public class Hotel4 : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Hotel: the east wing round to the boss gates");
            // (7,3) to (7,2) and (8,0); (8,1)'s cracked stepping stones; (8,2)'s key chest.
            yield return Section("hotel4 (8,2) key", () => Exec("O1 X1 H8,1 W30 H8,2 W30 +k W30 `h4_82k"));
            // Door 30 to (9,5), its keyblock into (9,6) (steam pipes blow at the pits), the button behind the steam.
            yield return Section("hotel4 door 30 to (9,6)", () => Exec("O1 X1 H9,0 W20 door30 W10 H9,6 W30 `h4_96"));
            yield return Section("hotel4 (9,6) steam button", () => Exec("O1 X1 >1523,1090 W60 `h4_96b"));
            // (8,6) raft, (9,7) two burst plants (its gate wants both), (9,8) the dog, (8,8) the slime: the boss gates' kill half opens for good.
            yield return Section("hotel4 (8,6) to (8,8)", () => Exec("O1 X1 H8,6 W10 F W30 H9,7 W30 F6000 W30 H9,8 W30 F6000 W30 H8,8 W30 F W30 `h4_88s"));
            // The spring pad drops us into the floor-1 elevator's pocket, above the boss.
            yield return Section("hotel4 spring into the pocket", () => Exec("O1 X1 >1347,1394 W30 `h4_pocket"));
        }
    }

    /// <summary>
    /// The way round to the (8,8) button, the boss gates' other half: lift to floor 3, door 27, back over (8,1)-(8,2) and door 30 to (9,5);
    /// raft (8,5)'s dust west over the lake to (7,7) (leaving its Dustmaids standing), (7,6)'s three kills and key, (7,8)'s keyblock, and
    /// past the frog to the button.
    /// </summary>
    public class Hotel5 : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Hotel: round to the (8,8) button");
            yield return Section("hotel5 lift and doors back to (9,5)", () => Exec("O1 X1 lift3 W30 door27 W30 H8,1 W30 H8,2 W30 H9,0 W20 door30 W30 `h5_95"));
            yield return Section("hotel5 raft to (7,7)", () => Exec("O1 X1 H7,7 W30 `h5_77"));
            yield return Section("hotel5 (7,6) key", () => Exec("O1 X1 H7,6 F6000 +k W30 `h5_76k"));
            yield return Section("hotel5 keyblock to (7,8)", () => Exec("O1 X1 H7,8 W30 `h5_78"));
            yield return Section("hotel5 the (8,8) button", () => Exec("O1 X1 P1331,1298 W60 `h5_button"));
        }
    }

    /// <summary>The Hotel's boss: drop into the pocket and through both gate pairs; the eye in its pool (from a dust raft), then on land below.</summary>
    public class Hotel6 : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Hotel: the Eye boss");
            // TODO: g6 per phase: its bullets still land a few times a fight
            yield return Section("hotel6 eye boss, water phase", () => Exec("O1 X1 g6 H8,9 W30 F20000 W60"));
            yield return Section("hotel6 eye boss, land phase", () => Exec("O1 X1 g6 H8,10 W30 F20000 W60 `h6_done"));
        }
    }

    /// <summary>After the Hotel: out to the Nexus, then the cards on the way (Cliff, Space, the Cell) and on to the Circus door.</summary>
    public class CardsToCircus : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Cards: Hotel card, Cliff, Space, the Cell");
            yield return Section("cards hotel card and out to the Nexus", () => Exec("O1 X1 W30 H8,11 W30 + W30 X0 n0 W120 H2,3 W10 (25,33 W10 D20 W30 >387,656 W10 ed W120 D30 W30 >441,872 W30 A W200 `cp_nexus"));
            yield return Section("cards cliff", () => Exec("O1 X1 viaCROWD W30 oCLIFF@1520,640 W30 + W30 `cp_cliff"));
            yield return Section("cards space", () => Exec("O1 X1 oSPACE W60 + W30 `cp_space"));
            yield return Section("cards cell", () => Exec("O1 X1 viaREDCAVE W30 oCELL W60 + W30 `cp_cell"));
            yield return Section("cards into the circus", () => Exec("O1 X1 viaREDCAVE W30 oCELL W60 oCIRCUS@1193,6 W30 `cp_circusin"));
        }
    }

    /// <summary>The Circus, first half: keys 1 and 2 and the Flame Pillar and west cards.</summary>
    public class Circus : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Circus: keys and cards");
            yield return Section("circus key 1 past the acrobats", () => Exec("O1 X1 Qw Qn >979,1202 >1347,930 dust:ArthurDanger W240 +K W30 `cc_k1"));
            yield return Section("circus flame pillar card", () => Exec("O1 X1 n0 W60 >707,1090 >723,930 heal >659,658 >627,706 >531,786 >531,1090 +s W30 `cc_fp"));
        }
    }

    /// <summary>The Circus, second part: the west (Contort) card and keys 2 and 3, ending at the entrance.</summary>
    public class CircusB : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Circus: the west card and keys 2 and 3");
            yield return Section("circus west card", () => Exec("O1 X1 n0 >67,962 >83,658 + W30 `cc_west"));
            yield return Section("circus key 2", () => Exec("O1 X1 >1059,658 >1235,530 heal >1203,354 +k W30 `cc_k2"));
            // (key 3, past the (45,40) keyblock: the boss's way needs it for the (45,30) one; then back to the entrance, so the next part
            // does not start out of a reload beside enemies that came back)
            yield return Section("circus key 3", () => Exec("O1 X1 +K W30 >707,1364 W30 `cc_k3"));
        }
    }

    /// <summary>The Circus boss (Arthur and Javiera's act), the health upgrade and their card.</summary>
    public class Circus2 : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Circus: the boss");
            // (key 3 is in the north-west chest past the (45,40) keyblock; the boss's way needs it for the (45,30) one)
            yield return Section("circus boss button", () => Exec("O1 X1 >659,338 W60 >707,338 W30 `cc_pre"));
            yield return Section("circus boss", () => Exec("O1 X1 F W60 i W30 `cc_boss"));
            yield return Section("circus A&J card", () => Exec("O1 X1 door32 W30 + W30 `cc_aj"));
        }
    }

    /// <summary>The Circus, last half: the rat puzzle, key 4, the top key gate and the Lion card.</summary>
    /// <summary>
    /// The cards the ending's 36 still lacks after the Circus: Red Cave (0,0), Crowd (8,2), the Terminal's Sage card, then the Hotel's
    /// Dustmaid (3,6) (a dust-powered lift), Dasher (1,8) (sight traps) and Burst Plant (9,3) (a hop into the chasm's current, a
    /// Dustmaid kited, the dust rafted north over two screens and down the current to the chest).
    /// </summary>
    public class Cards2 : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("Cards to 36: Red Cave, Crowd, Terminal and the Hotel's three");
            yield return Section("red cave (0,0) card", () => Exec("O1 X1 viaREDCAVE W30 g6 + W30 g4 O1 X1 +K W30 + W30"));
            yield return Section("crowd (8,2) card", () => Exec("g4 O1 X1 viaCROWD W30 + W30 g4 O1 X1 H3,3 oCROWD@560,512 W30 >838,352 U30 W60 's7,2,3 >838,352 U30 W60 O1 X1 F3000 W60 >1190,330 >1189,262 >1240,246 G1250,246,kr W20 H8,1 F3000 W30 >1350,300 H8,2 t13 I1408,432,d W60 T W30"));
            yield return Section("terminal sage card", () => Exec("g4 O1 X1 viaFIELDS W30 H5,4 H5,5 H4,5 H4,4 H3,4 H3,3 H2,3 H2,2 H1,2 H1,1 oTERMINAL W60 + W30"));
            yield return Section("hotel dustmaid card", () => Exec("g4 O1 X1 viaHOTEL W30 >520,980 W20 G500,984 ed988 W30 G500,1045 er511 W30 J35,65,d+ el536 W30 G500,1064 ed1068 W200 u1 u5 W20 UC14 W190 U24 u5 W30 T W30"));
            yield return Section("hotel floor 3 key", () => Exec("g4 O1 X1 >440,872 W30 lift3 W30 H3,10 W20 >555,1675 G553,1676 W20 I608,1616,d W60 T W30"));
            yield return Section("hotel dasher card", () => Exec("g4 O1 X1 H2,9 W20 >339,1458 G339,1458,kl W30 g0 O1 X0 G300,1458 sight243,1458 W10 U30 W30 g2 O1 X0 W5 sight291,1314 J18,82,u W60 T W30"));
            yield return Section("hotel back from the dasher", () => Exec("g2 O1 X0 sight243,1412 D20 W5 sight307,1458 W10"));
            yield return Section("hotel to the burst plant chasm", () => Exec("g3 O1 X1 door27 W30 H8,1 W30 H8,2 W30"));
            string wade = string.Concat(System.Linq.Enumerable.Repeat("D2 RD4 ", 18));
            yield return Section("hotel burst plant chasm", () => Exec("D16 er1327 R60 +k W10 G1408,452 L7 " + wade + "R20 >1456,560"));
            yield return Section("hotel burst plant card", () => Exec("J92,35,d+ J92,35,d- F W20 dust:92,30 U70 L14 F W20 J92,21,l- L10 U80 dust:93,13 R10 F W20 ~1544,552 F W20 +s W60"));
        }
    }

    public class Endgame : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The endgame: the Sage, GO, BLUE, HAPPY and the Briar");
            yield return Section("hotel exit", () => Exec("O1 X1 W30 X0 n0 W120 H2,3 W10 (25,33 W10 D20 W30 >387,656 W10 ed W120 D30 W30 >441,872 W30 A W200"));
            yield return Section("terminal sage", () => Exec("g4 O1 X1 viaTERMINAL W30 >392,600 W30 F20000 W60"));
            yield return Section("terminal to go", () => Exec("O1 X1 >396,40 W10 U90 W120"));
            yield return Section("go swap puzzle", () => Exec("I393,752,d W60 T W30 >392,720 W30 H2,3 W20 goswap W60 H2,2 W30"));
            yield return Section("go to blue", () => Exec("O1 X1 H3,2 W20 H4,2 W30 door44 W60"));
            yield return Section("blue in", () => Exec("g8 O1 X1 b344,746 W60 g4 O1 X1 >563,404 W30"));
            yield return Section("blue (3,2) islands", () => Exec("O1 X0 G545,396 W10 @x539,u !1 U28 L13 W2 ULC1 UL7 U18 W20 DC1 D14 R8 W20 !0 @x539,d !1 D20 !0 @y425,r !1 L48 W2 D12 W2 LC1 L17 D8 W20 UC1 U8 R12 W20 U8 R30 U20 !0 W10"));
            yield return Section("blue (4,1) dash chain", () => Exec("O1 X1 >755,330 W20 >755,312 W20 O1 X0 W5 !1 UL28 ULC1 UL27 U6 UC1 U30 !0 W30"));
            yield return Section("blue (4,0) climb", BlueSteamClimb);
            yield return Section("blue (2,0) lasers", () => Exec("g6 O1 X1 >456,150 W10 dust:28,7 W10 G452,100 W10 dust:28,5 W10 G452,68 W10 dust:28,3 W10 G452,24 W10"));
            yield return Section("blue mitra buttons", () => Exec("g6 O1 X1 b150,40 W30 G72,36 W1500"));
            yield return Section("blue back to go", () => Exec("g6 O1 X1 >36,84 W5 L40 W200 g6 O1 X1 >40,408 W30 L60 W100"));
            yield return Section("happy", () => Exec("g6 O1 X1 b230,390 W30 b60,60 W30 b340,84 W30 b550,180 W30 b516,292 W30 b708,180 W30"));
            yield return Section("happy back to go", () => Exec("g6 O1 X1 W30 >764,244 W10 R40 W150 >392,300 W30"));
            yield return Section("briar", () => Exec("g6 O1 X1 F40000 W600 ending"));
        }

        /// <summary>Up the pixel seam between (4,0)'s pits, with steam pushing off it (a fall puts us back at the bottom: again), then west along the top.</summary>
        private static IEnumerable<object> BlueSteamClimb()
        {
            for (int t = 0; t < 4 && Player.Position.Y > 40; t++)
            {
                yield return Exec("O1 X0 !1 R32 !0 @x731,u ^x731,u,140 W5");
                yield return Wait(60); // (a fall: back at the bottom by now)
            }
            yield return Exec("O1 X0 !1 LC1 L27 W8 L4 LC1 L27 W8 L4 LC1 L27 W8 L40 !0 W20");
        }
    }

    public class Circus3 : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("The Circus: rat puzzle and the Lion card");
            yield return Section("circus rat puzzle", () => Exec("O1 X1 >232,1032 >328,1032 [3600 W30 `cc_rat"));
            yield return Section("circus key 4", () => Exec("O1 X1 +K W30 `cc_k4"));
            yield return Section("circus top key gate", () => Exec("O1 X1 >664,56 >616,56 W30 `cc_kg"));
            yield return Section("circus lion card", () => Exec("O1 X1 >504,24 W30 >552,200 >580,270 F >552,340 F >606,440 + W30 `cc_lion"));
        }
    }
}
