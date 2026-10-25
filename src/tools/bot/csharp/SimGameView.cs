using AnodyneSharp.Entities;
using AnodyneSharp.MapData;
using AnodyneSharp.MapData.Tiles;
using AnodyneSharp.Utilities;
using Microsoft.Xna.Framework;
using AnodyneSharp.Entities.Base.Rendering;
using Microsoft.Xna.Framework.Graphics;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.IO;
using System.Net.Sockets;
using System.Text.Json;

namespace AnodyneSharp.Recording
{
    /// <summary>
    /// The GBA port in its SDL3 simulator (src/anodyne/platform/sim, started with --headless --ipc PORT). The link is lock-step:
    /// the simulator sends one JSON line per frame ({"frame":N,"state":{...}}, see game_probe.cpp) and waits for a command;
    /// "keys MASK FRAMES" (GBA KEYINPUT bits, 1 = pressed) runs that many frames.
    /// </summary>
    public sealed class SimGameView : IGameView, IDisposable
    {
        private readonly TcpClient _client;
        private readonly StreamReader _in;
        private readonly StreamWriter _out;
        private JsonElement _state;

        public long Frame { get; private set; }

        public SimGameView(string host = "127.0.0.1", int port = 47011)
        {
            _client = new TcpClient(host, port);
            var stream = _client.GetStream();
            _in = new StreamReader(stream);
            _out = new StreamWriter(stream) { AutoFlush = true, NewLine = "\n" };
            ReadState();
        }

        /// <summary>Holds `mask` (GBA KEYINPUT bit order: A B Select Start Right Left Up Down R L) for `frames` frames.</summary>
        public void Hold(int mask, int frames = 1)
        {
            _out.WriteLine($"keys {mask:x} {Math.Max(1, frames)}");
            ReadState();
        }

        private void ReadState()
        {
            string line = _in.ReadLine() ?? throw new EndOfStreamException("simulator closed the bot link");
            using var doc = JsonDocument.Parse(line);
            Frame = doc.RootElement.GetProperty("frame").GetInt64();
            _state = doc.RootElement.GetProperty("state").Clone();
        }

        private int Int(string name) => _state.TryGetProperty(name, out var v) ? v.GetInt32() : 0;
        private bool Bool(string name) => _state.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.True;

        public JsonElement Raw => _state;
        public string Mode => _state.TryGetProperty("mode", out var v) ? v.GetString() : "";
        public string Map => _state.TryGetProperty("map", out var v) ? v.GetString() : "";
        public Point Grid
        {
            get
            {
                var g = _state.GetProperty("grid");
                return new Point(g[0].GetInt32(), g[1].GetInt32());
            }
        }
        public bool ScreenTransition => Bool("transition");
        public int Health => _state.GetProperty("player").GetProperty("health").GetInt32();
        public int MaxHealth => Int("maxHealth");
        // The port only reports whether the box is open, not its text.
        public string Dialogue => Bool("dialog") ? "..." : null;
        public bool LastDialogueFinished => !Bool("dialog");
        public bool CanJump => Bool("canJump");
        public bool HasBroom => Int("broom") != 0;
        public int CardCount => Int("cards");
        public int MapKeys => Int("keys");
        public int PuzzlesSolved => Int("puzzles");
        public int EnemiesKilled => Int("killed");
        public IPlayerView Player => Mode == "play" && _state.TryGetProperty("player", out var p) ? new SimPlayerView(p) : null;
        /// <summary>A dialogue box or cutscene has the input (PlayState's child states, BotRunner.PlayBusy).</summary>
        public bool Busy => Bool("dialog") || Bool("cutscene");

        private readonly Dictionary<int, Entity> _shadows = new();
        private readonly Dictionary<Entity, List<Entity>> _children = new();
        private List<Entity> _entities = new();
        private long _entitiesFrame = -1;

        /// <summary>
        /// The port's entities this frame (not the player's own), as stand-ins of the C# game's classes (SimEntities) so
        /// the bot's entity code (type tests, Hitbox, velocity...) runs unchanged. For BotRunner.Entities.
        /// </summary>
        public List<Entity> Entities
        {
            get
            {
                if (_entitiesFrame != Frame)
                {
                    _entitiesFrame = Frame;
                    _entities = SimEntities.Update(_state, _shadows, _children);
                }
                return _entities;
            }
        }

        public IEnumerable<Entity> SubEntities(Entity e)
        {
            _ = Entities; // (this frame's)
            return _children.TryGetValue(e, out var list) ? list : System.Linq.Enumerable.Empty<Entity>();
        }

        private SimMapView _map;
        /// <summary>The map's collision, fetched (the "collision" command) when the map changes.</summary>
        public IMapView CurrentMap
        {
            get
            {
                if (Mode != "play" || string.IsNullOrEmpty(Map))
                {
                    return null;
                }
                if (_map == null || _map.Name != Map)
                {
                    _out.WriteLine("collision");
                    _map = new SimMapView(Map, _in.ReadLine() ?? throw new EndOfStreamException("simulator closed the bot link"));
                }
                return _map;
            }
        }

        public void Dispose()
        {
            _client.Dispose();
        }
    }

    /// <summary>
    /// Stand-ins for the port's entities: an object of the C# entity class of the same name (made without running its
    /// constructor) with what the port reports filled in. Its other fields keep their defaults, so bot code that reads an
    /// entity's internals sees nothing there.
    /// </summary>
    public static class SimEntities
    {
        private static Dictionary<string, Type> _types;
        private static readonly System.Reflection.PropertyInfo FlickeringProp = typeof(Entity).GetProperty("_flickering");

        /// <summary>The C# class for a port class name: by name, or outer + inner name for nested ones (AnnoyerFireball).</summary>
        public static Type TypeOf(string name)
        {
            if (_types == null)
            {
                _types = new Dictionary<string, Type>();
                foreach (var t in typeof(Entity).Assembly.GetTypes().Where(t => typeof(Entity).IsAssignableFrom(t) && !t.IsAbstract && !t.IsGenericType))
                {
                    _types.TryAdd(t.Name, t);
                    if (t.DeclaringType != null)
                    {
                        _types.TryAdd(t.DeclaringType.Name + t.Name, t);
                    }
                }
            }
            return _types.TryGetValue(name, out var type) ? type : typeof(Entity);
        }

        private static float Fx(JsonElement a, int i) => a[i].GetInt32() / 65536f;

        /// <summary>
        /// This frame's top-level entities. As in the C# game, an entity's own (an enemy's bullets: the port's entities with
        /// a parent) are not in the list but its sub-entities, in `children`.
        /// </summary>
        public static List<Entity> Update(JsonElement state, Dictionary<int, Entity> shadows, Dictionary<Entity, List<Entity>> children)
        {
            var list = new List<Entity>();
            var seen = new HashSet<int>();
            var parents = new List<(Entity, int)>();
            children.Clear();
            if (!state.TryGetProperty("entities", out var arr))
            {
                shadows.Clear();
                return list;
            }
            foreach (var j in arr.EnumerateArray())
            {
                int id = j.GetProperty("id").GetInt32();
                var type = TypeOf(j.GetProperty("type").GetString());
                // (the same slot as last frame and the same class: the same entity, so state the bot keys by entity carries on)
                if (!shadows.TryGetValue(id, out var e) || e.GetType() != type)
                {
                    e = (Entity)RuntimeHelpers.GetUninitializedObject(type);
                    e.sprite = new SimSprite();
                    shadows[id] = e;
                }
                seen.Add(id);
                var fx = j.GetProperty("fx16");
                e.Position = new Vector2(Fx(fx, 0), Fx(fx, 1));
                e.velocity = new Vector2(Fx(fx, 2), Fx(fx, 3));
                e.width = j.GetProperty("w").GetInt32();
                e.height = j.GetProperty("h").GetInt32();
                e.exists = j.GetProperty("exists").GetBoolean();
                e.Solid = j.GetProperty("solid").GetBoolean();
                e.visible = j.GetProperty("visible").GetBoolean();
                e.opacity = j.GetProperty("opacity").GetInt32() / 65536f;
                e.facing = (Facing)j.GetProperty("face").GetInt32();
                FlickeringProp?.SetValue(e, j.GetProperty("flicker").GetBoolean());
                var sprite = (SimSprite)e.sprite;
                sprite.CurAnimName = j.GetProperty("anim").GetString();
                sprite.Frame = j.GetProperty("frame").GetInt32();
                int parent = j.GetProperty("parent").GetInt32();
                if (parent >= 0)
                {
                    parents.Add((e, parent));
                }
                else
                {
                    list.Add(e);
                }
            }
            foreach (var (e, parent) in parents)
            {
                if (shadows.TryGetValue(parent, out var p) && seen.Contains(parent))
                {
                    if (!children.TryGetValue(p, out var subs))
                    {
                        children[p] = subs = new List<Entity>();
                    }
                    subs.Add(e);
                }
                else
                {
                    list.Add(e); // (its parent is not reported: the player's own)
                }
            }
            foreach (var gone in shadows.Keys.Where(k => !seen.Contains(k)).ToList())
            {
                shadows.Remove(gone);
            }
            return list;
        }

        /// <summary>A sprite that only reports the port's anim and frame.</summary>
        private sealed class SimSprite : ISpriteRenderer
        {
            public bool AnimFinished => false;
            public Color Color { get; set; } = Color.White;
            public string CurAnimName { get; set; } = "";
            public int Frame { get; set; }
            public int FrameIndex => 0;
            public int Height => 16;
            public int Width => 16;
            public ILayerType Layer { get; set; }

            public void Draw(SpriteBatch batch, Vector2 position, float scale, int y_push, float rotation, float opacity, SpriteEffects flip)
            {
            }

            public void SetFrame(int index)
            {
                Frame = index;
            }

            public bool PlayAnim(string name, bool force = false, int? newFramerate = null)
            {
                return false;
            }

            public bool SetTexture(string textureName, int width, int height, bool ignoreChaos, bool allowFailure)
            {
                return false;
            }

            public void Update()
            {
            }
        }
    }

    /// <summary>The port's map collision: "W H" then two hex digits per cell (allowed sides, CollisionEventType).</summary>
    public sealed class SimMapView : IMapView
    {
        private readonly (Touching, CollisionEventType)[] _cells;

        public SimMapView(string name, string line)
        {
            Name = name;
            var parts = line.Split(' ', 3);
            WidthInTiles = int.Parse(parts[0]);
            HeightInTiles = int.Parse(parts[1]);
            string hex = parts.Length > 2 ? parts[2] : "";
            _cells = new (Touching, CollisionEventType)[WidthInTiles * HeightInTiles];
            for (int i = 0; i < _cells.Length && 2 * i + 1 < hex.Length; i++)
            {
                _cells[i] = ((Touching)Convert.ToInt32(hex.Substring(2 * i, 1), 16),
                    (CollisionEventType)Convert.ToInt32(hex.Substring(2 * i + 1, 1), 16));
            }
        }

        public string Name { get; }
        public int WidthInTiles { get; }
        public int HeightInTiles { get; }

        public (Touching, CollisionEventType) GetTileClass(Point p)
        {
            if (p.X < 0 || p.Y < 0 || p.X >= WidthInTiles || p.Y >= HeightInTiles)
            {
                return (Touching.ANY, CollisionEventType.NONE);
            }
            return _cells[p.Y * WidthInTiles + p.X];
        }

        public int GetTile(AnodyneSharp.MapData.Layer layer, Point p) => 0;
        public Touching GetTileDirection(Point p) => Touching.NONE;
    }

    /// <summary>The port's player, from one frame's "player" object (game_probe.cpp).</summary>
    public sealed class SimPlayerView : IPlayerView, IBroomView
    {
        private readonly JsonElement _p;
        private readonly JsonElement _fx;

        public SimPlayerView(JsonElement p)
        {
            _p = p;
            _fx = p.GetProperty("fx16");
        }

        private float Fx(int i) => _fx[i].GetInt32() / 65536f;
        private int Int(string name) => _p.TryGetProperty(name, out var v) ? v.GetInt32() : 0;
        private bool Bool(string name) => _p.TryGetProperty(name, out var v) && v.ValueKind == JsonValueKind.True;

        public Vector2 Position => new Vector2(Fx(0), Fx(1));
        public Rectangle Hitbox => MathUtilities.CreateRectangle(Fx(0), Fx(1), Int("w"), Int("h"));
        public Vector2 Center => Position + new Vector2(Int("w"), Int("h")) / 2;
        // (not reported by the port: the hitbox centre stands in)
        public Vector2 VisualCenter => Center;
        public Vector2 velocity => new Vector2(Fx(2), Fx(3));
        public Vector2 additionalVel => new Vector2(Fx(4), Fx(5));
        public Facing facing => (Facing)Int("face");
        public PlayerState state => (PlayerState)Int("state");
        public PlayerAnimState ANIM_STATE => (PlayerAnimState)Int("animState");
        public Touching touching => (Touching)Int("touching");
        public string CurAnimName => _p.TryGetProperty("anim", out var v) ? v.GetString() : "";
        public bool exists => Bool("exists");
        public bool dontMove => Bool("dontMove");
        public bool reversed => false;
        public bool ON_CONVEYOR => Bool("conveyor");
        public float y_push => 0f;
        public int slowTicks => Bool("slow") ? 1 : 0;
        public float slowMul => Bool("slow") ? 0.5f : 1f;
        public IBroomView broom => this;
        public Entity raft => null;
        public AnodyneSharp.Entities.Player Native => null;

        bool IBroomView.exists => Bool("broomOut");
        Rectangle IBroomView.Hitbox
        {
            get
            {
                var b = _p.GetProperty("broomBox");
                return new Rectangle(b[0].GetInt32(), b[1].GetInt32(), b[2].GetInt32(), b[3].GetInt32());
            }
        }
        Entity IBroomView.dust => null;
        bool IBroomView.just_released_dust => false;
    }
}
