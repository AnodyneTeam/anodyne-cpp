using AnodyneSharp.Input;
using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>Title screen -> new game -> intro dialogue -> BLANK -> Nexus (walkthrough Part 1).</summary>
    public class StartToNexus : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Say("boot: title -> new game");
            yield return Boot();

            yield return Say("BLANK: first teleporter");
            // Step on the first teleporter (BLANK 112,112): walking into it warps after a short delay.
            yield return WalkTo(118, 87);
            yield return Hold(400, KeyFunctions.Down).TakeUntil(() => Pos.X > 300);
            yield return Until(() => Map == "BLANK" && Pos.X > 300, 600, "warp to the console room");

            yield return Say("console room");
            // North into the console room; a tutorial text box pops up on entry.
            yield return WalkTo(390, 300);
            yield return ReadDialogue();

            // Both consoles must be activated to open the gates.
            yield return Interact(341, 282);
            yield return Interact(443, 282);
            yield return Wait(120);

            yield return Say("corridor -> teleporter to Nexus");
            // North through the corridor (more tutorial text), then onto the teleporter to the Nexus.
            yield return WalkTo(402, 250);
            yield return WalkTo(402, 100);
            yield return WalkTo(397, 70);
            yield return Hold(600, KeyFunctions.Up).TakeUntil(() => Map == "NEXUS");
            yield return Until(() => Map == "NEXUS", 600, "arrive in the Nexus");
            yield return Wait(60);
        }

        /// <summary>Stand below an object at (x,y), face up and press Accept.</summary>
        private static IEnumerable<object> Interact(float x, float y)
        {
            yield return WalkTo(x, y, yFirst: true);
            yield return Hold(30, KeyFunctions.Up);
            for (int i = 0; i < 3; i++)
            {
                yield return Tap(KeyFunctions.Accept);
                yield return Wait(8);
            }
        }

        /// <summary>Title screen -> menu -> new game -> intro text, until the player can move in BLANK.</summary>
        public static IEnumerable<object> Boot()
        {
            for (int i = 0; i < 3000 && !(Player != null && !string.IsNullOrEmpty(Map)); i++)
            {
                yield return Tap(KeyFunctions.Accept);
                yield return Wait(10);
            }
            yield return Wait(30);
        }
    }
}
