using AnodyneSharp.Entities;
using AnodyneSharp.MapData;
using AnodyneSharp.MapData.Tiles;
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

        private MapView _map;
        public IMapView CurrentMap
        {
            get
            {
                if (GlobalState.Map is not Map m)
                {
                    return null;
                }
                if (_map == null || _map.Native != m)
                {
                    _map = new MapView(m);
                }
                return _map;
            }
        }

        private PlayerView _player;
        public IPlayerView Player
        {
            get
            {
                var p = BotRunner.GamePlayer;
                if (p == null)
                {
                    return null;
                }
                if (_player == null || _player.Native != p)
                {
                    _player = new PlayerView(p);
                }
                return _player;
            }
        }
    }

    /// <summary>The C# game's map, read straight through.</summary>
    public sealed class MapView : IMapView
    {
        public MapView(Map map)
        {
            Native = map;
        }

        public Map Native { get; }
        public int WidthInTiles => Native.WidthInTiles;
        public int HeightInTiles => Native.HeightInTiles;
        public (Touching, CollisionEventType) GetTileClass(Point p) => Native.GetTileClass(p);
        public int GetTile(Layer layer, Point p) => Native.GetTile(layer, p);
    }

    /// <summary>The C# game's player, read straight through.</summary>
    public sealed class PlayerView : IPlayerView, IBroomView
    {
        public PlayerView(AnodyneSharp.Entities.Player p)
        {
            Native = p;
        }

        public AnodyneSharp.Entities.Player Native { get; }
        public Vector2 Position => Native.Position;
        public Rectangle Hitbox => Native.Hitbox;
        public Vector2 Center => Native.Center;
        public Vector2 VisualCenter => Native.VisualCenter;
        public Vector2 velocity => Native.velocity;
        public Vector2 additionalVel => Native.additionalVel;
        public Facing facing => Native.facing;
        public PlayerState state => Native.state;
        public PlayerAnimState ANIM_STATE => Native.ANIM_STATE;
        public Touching touching => Native.touching;
        public string CurAnimName => Native.CurAnimName;
        public bool exists => Native.exists;
        public bool dontMove => Native.dontMove;
        public bool reversed => Native.reversed;
        public bool ON_CONVEYOR => Native.ON_CONVEYOR;
        public float y_push => Native.y_push;
        public int slowTicks => Native.slowTicks;
        public float slowMul => Native.slowMul;
        public IBroomView broom => Native.broom == null ? null : this;
        public Entity raft => Native.raft;

        bool IBroomView.exists => Native.broom.exists;
        Rectangle IBroomView.Hitbox => Native.broom.Hitbox;
        Entity IBroomView.dust => Native.broom.dust;
        bool IBroomView.just_released_dust => Native.broom.just_released_dust;
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
        public static IPlayerView Player => View.Player;
    }
}
