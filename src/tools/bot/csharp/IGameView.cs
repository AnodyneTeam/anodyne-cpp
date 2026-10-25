using Microsoft.Xna.Framework;

namespace AnodyneSharp.Recording
{
    /// <summary>
    /// The game state the bot reads, so the same bot can play either the C# game (AnodyneGameView) or the GBA port in its
    /// simulator over the bot link (SimGameView). Bot code reads it through Gs rather than GlobalState.
    /// Moving the bot onto this is gradual: what is here so far is the scalar state; entities, presets and the map's
    /// collision still come from the C# game directly.
    /// </summary>
    public interface IGameView
    {
        /// <summary>GlobalState.CURRENT_MAP_NAME.</summary>
        string Map { get; }
        /// <summary>The current screen (GlobalState.CurrentMapGrid).</summary>
        Point Grid { get; }
        /// <summary>The camera is scrolling to the next screen.</summary>
        bool ScreenTransition { get; }
        int Health { get; }
        int MaxHealth { get; }
        /// <summary>The text of the open dialogue box: null or "" when none is open.</summary>
        string Dialogue { get; }
        bool LastDialogueFinished { get; }
        bool CanJump { get; }
        bool HasBroom { get; }
        int CardCount { get; }
        /// <summary>Small keys for the current map.</summary>
        int MapKeys { get; }
        int PuzzlesSolved { get; }
        int EnemiesKilled { get; }
    }
}
