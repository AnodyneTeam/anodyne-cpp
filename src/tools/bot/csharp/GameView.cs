using AnodyneSharp.Registry;
using Microsoft.Xna.Framework;

namespace AnodyneSharp.Recording
{
    /// <summary>The C# game itself.</summary>
    public sealed class AnodyneGameView : IGameView
    {
        public string Map => GlobalState.CURRENT_MAP_NAME;
        public Point Grid => GlobalState.CurrentMapGrid;
        public bool ScreenTransition => GlobalState.ScreenTransition;
        public int Health => GlobalState.CUR_HEALTH;
        public int MaxHealth => GlobalState.MAX_HEALTH;
        public string Dialogue => GlobalState.Dialogue;
        public bool LastDialogueFinished => GlobalState.LastDialogueFinished;
        public bool CanJump => GlobalState.inventory.CanJump;
        public bool HasBroom => GlobalState.inventory.HasBroom;
        public int CardCount => GlobalState.inventory.CardCount;
        public int MapKeys => GlobalState.inventory.GetCurrentMapKeys();
        public int PuzzlesSolved => GlobalState.PUZZLES_SOLVED;
        public int EnemiesKilled => GlobalState.ENEMIES_KILLED;
    }

    /// <summary>The game the bot is playing: AnodyneGameView by default.</summary>
    public static class Gs
    {
        public static IGameView View = new AnodyneGameView();

        public static string Map => View.Map;
        public static Point Grid => View.Grid;
        public static bool ScreenTransition => View.ScreenTransition;
        public static int Health => View.Health;
        public static int MaxHealth => View.MaxHealth;
        public static string Dialogue => View.Dialogue;
        public static bool LastDialogueFinished => View.LastDialogueFinished;
        public static bool CanJump => View.CanJump;
        public static bool HasBroom => View.HasBroom;
        public static int CardCount => View.CardCount;
        public static int MapKeys => View.MapKeys;
        public static int PuzzlesSolved => View.PuzzlesSolved;
        public static int EnemiesKilled => View.EnemiesKilled;
    }
}
