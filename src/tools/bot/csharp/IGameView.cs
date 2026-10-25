using AnodyneSharp.Entities;
using AnodyneSharp.MapData;
using AnodyneSharp.MapData.Tiles;
using Microsoft.Xna.Framework;
using System.Collections.Generic;

namespace AnodyneSharp.Recording
{
    /// <summary>
    /// The game state the bot reads, so the same bot can play either the C# game (AnodyneGameView) or the GBA port in its
    /// simulator over the bot link (SimGameView). Bot code reads it through Gs rather than GlobalState.
    /// Moving the bot onto this is gradual: what is here so far is the scalar state and the player, the map's collision and
    /// entities (BotRunner.Entities, set by the host); presets still come from the C# game directly.
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
        /// <summary>The player, or null when there is none (no PlayState).</summary>
        IPlayerView Player { get; }
        /// <summary>The current map's tiles, or null when none is loaded.</summary>
        IMapView CurrentMap { get; }
        /// <summary>Entity.SubEntities (an enemy's bullets...). Bot code calls it as e.Subs().</summary>
        IEnumerable<Entity> SubEntities(Entity e);
    }

    /// <summary>The current map, by 16x16 tile.</summary>
    public interface IMapView
    {
        int WidthInTiles { get; }
        int HeightInTiles { get; }
        /// <summary>The tile's collision sides and event (BG, with BG2 on top); off the map: (ANY, NONE).</summary>
        (Touching, CollisionEventType) GetTileClass(Point p);
        /// <summary>The tile id on a layer (0 = empty). Not reported by the simulator: 0 there.</summary>
        int GetTile(Layer layer, Point p);
        /// <summary>The BG tile's direction (a conveyor's flow; still water says ANY). Not reported by the simulator yet: NONE.</summary>
        Touching GetTileDirection(Point p);
    }

    /// <summary>
    /// The player as the bot reads it. Members are named as on AnodyneSharp.Entities.Player so bot code reads the same
    /// either way; what only the C# game has (its internals, writes) goes through Native.
    /// </summary>
    public interface IPlayerView
    {
        /// <summary>The hitbox's top-left, map pixels.</summary>
        Vector2 Position { get; }
        Rectangle Hitbox { get; }
        Vector2 Center { get; }
        Vector2 VisualCenter { get; }
        /// <summary>Px per second.</summary>
        Vector2 velocity { get; }
        Vector2 additionalVel { get; }
        Facing facing { get; }
        PlayerState state { get; }
        PlayerAnimState ANIM_STATE { get; }
        Touching touching { get; }
        string CurAnimName { get; }
        bool exists { get; }
        bool dontMove { get; }
        bool reversed { get; }
        bool ON_CONVEYOR { get; }
        float y_push { get; }
        int slowTicks { get; }
        float slowMul { get; }
        IBroomView broom { get; }
        /// <summary>The dust the player rides, or null.</summary>
        Entity raft { get; }
        /// <summary>The C# game's player; null on the simulator.</summary>
        AnodyneSharp.Entities.Player Native { get; }
    }

    public interface IBroomView
    {
        /// <summary>Out: a swing is on.</summary>
        bool exists { get; }
        Rectangle Hitbox { get; }
        /// <summary>The dust carried or just put down, or null.</summary>
        Entity dust { get; }
        bool just_released_dust { get; }
    }
}
