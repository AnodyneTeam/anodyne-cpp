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

        /// <summary>Puts the port's RNG where the C#'s is after that many draws (parity replays: the title screens draw
        /// differently).</summary>
        public bool SetRng(long draws, bool keepShake = false)
        {
            _out.WriteLine(keepShake ? $"rng {draws} keep" : $"rng {draws}");
            return _in.ReadLine() == "ok";
        }

        /// <summary>Sets GlobalState.checkpoint (what the bot does on the death screen).</summary>
        public bool SetCheckpoint(string map, int x, int y)
        {
            _out.WriteLine($"checkpoint {map} {x} {y}");
            return _in.ReadLine() == "ok";
        }

        /// <summary>Saves the current frame as a PNG.</summary>
        public bool Shot(string path)
        {
            _out.WriteLine($"shot {path}");
            return _in.ReadLine() == "ok";
        }

        private void ReadState()
        {
            string line = _in.ReadLine() ?? throw new EndOfStreamException("simulator closed the bot link");
            using var doc = JsonDocument.Parse(line);
            Frame = doc.RootElement.GetProperty("frame").GetInt64();
            _state = doc.RootElement.GetProperty("state").Clone();
            SyncPresets();
        }

        private static bool _presetsLoaded;
        private string _presetsSeen, _presetsMap;

        /// <summary>
        /// The bot reads the map's entity presets (EntityManager.GetMapEntities: gates, doors, pillars of the screens not loaded) and
        /// whether they are still alive: load the C# game's own Entities.xml once and set EntityManager.State from the port's.
        /// </summary>
        private void SyncPresets()
        {
            if (!_state.TryGetProperty("presets", out var pv))
            {
                return;
            }
            string seen = pv.GetRawText(), map = Map;
            if (seen == _presetsSeen && map == _presetsMap)
            {
                return;
            }
            _presetsSeen = seen;
            _presetsMap = map;
            if (!_presetsLoaded)
            {
                _presetsLoaded = true;
                // (EntityManager.Initialize reads the entry assembly's resources: here that is SimHost, so read the game's own)
                var asm = typeof(EntityManager).Assembly;
                using var stream = asm.GetManifestResourceStream($"{asm.GetName().Name}.Content.Entities.xml");
                using var reader = new StreamReader(stream);
                typeof(EntityManager).GetMethod("ReadEntities", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Static)
                    .Invoke(null, new object[] { reader.ReadToEnd() });
            }
            var list = EntityManager.GetMapEntities(map);
            foreach (var pr in list)
            {
                pr.Alive = true;
                pr.Activated = false;
            }
            foreach (var d in pv.EnumerateArray())
            {
                int x = d[0].GetInt32(), y = d[1].GetInt32(), frame = d[2].GetInt32(), flags = d[3].GetInt32();
                foreach (var pr in list)
                {
                    if ((int)pr.Position.X == x && (int)pr.Position.Y == y && pr.Frame == frame)
                    {
                        pr.Alive = (flags & 1) == 0;
                        pr.Activated = (flags & 2) != 0;
                    }
                }
            }
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
        public long RngCalls => _state.TryGetProperty("rng", out var r) ? r.GetInt64() : -1;
        public int MaxHealth => Int("maxHealth");
        // The port only reports whether the box is open, not its text.
        // GlobalState.Dialogue: empty while a cutscene's DialogueEvent box is up, as in the C#
        public string Dialogue => Bool("globalDialog") ? "..." : null;
        public bool LastDialogueFinished => !Bool("dialog");
        public bool CanJump => Bool("canJump");
        public bool HasBroom => Int("broom") != 0;
        public int CardCount => Int("cards");
        public int MapKeys => Int("keys");
        public int PuzzlesSolved => Int("puzzles");
        public int EnemiesKilled => Int("killed");
        // (the C#'s credits still have the play state's player)
        public IPlayerView Player => (Mode == "play" || Mode == "credits") && _state.TryGetProperty("player", out var p) ? new SimPlayerView(p, this) : null;
        /// <summary>A dialogue box or cutscene has the input (PlayState's child states, BotRunner.PlayBusy).</summary>
        public bool Busy => Bool("dialog") || Bool("cutscene") || DeathMenu;
        /// <summary>DeathState is up (BotRunner.InDeathMenu).</summary>
        public bool DeathMenu => Bool("deathMenu");
        /// <summary>Deaths so far, each one back at the checkpoint (BotRunner.Deaths).</summary>
        public int Deaths => Int("deaths");

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

        /// <summary>The stand-in of the port entity in pool slot id this frame, or null.</summary>
        public Entity EntityById(int id)
        {
            _ = Entities;
            return id >= 0 && _shadows.TryGetValue(id, out var e) ? e : null;
        }

        public string FsmPath(Entity e) => SimEntities.Fsm.TryGetValue(e, out var s) ? s : "";

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
        /// <summary>Each stand-in's state machine as the port reports it (IGameView.FsmPath).</summary>
        public static readonly Dictionary<Entity, string> Fsm = new();

        private static Dictionary<string, Type> _types;
        private static readonly System.Reflection.PropertyInfo FlickeringProp = typeof(Entity).GetProperty("_flickering");

        // Port class names that are not the C# class's own (or outer + inner) name: C# full names (nested: Outer+Inner).
        private static readonly Dictionary<string, string> Aliases = new()
        {
            ["PewLaserBullet"] = "AnodyneSharp.Entities.Enemy.Bedroom.PewLaser+Laser",
            ["BigKey"] = "Big_Key",
            ["Treasure"] = "TreasureChest",
            ["KeyGate"] = "SmallKeyGate",
            ["VBlocker"] = "VerticalBlocker",
            ["EyeLight"] = "Eye_Light",
            ["GlowLight"] = "FiveFrameGlowLight",
        };

        /// <summary>The C# class for a port class name: by name, or outer + inner name for nested ones (AnnoyerFireball).</summary>
        public static Type TypeOf(string name)
        {
            if (_types == null)
            {
                _types = new Dictionary<string, Type>();
                foreach (var (port, cs) in Aliases)
                {
                    var t = typeof(Entity).Assembly.GetType(cs)
                        ?? typeof(Entity).Assembly.GetTypes().FirstOrDefault(t => t.Name == cs && typeof(Entity).IsAssignableFrom(t));
                    if (t != null)
                    {
                        _types[port] = t;
                    }
                }
                foreach (var t in typeof(Entity).Assembly.GetTypes().Where(t => typeof(Entity).IsAssignableFrom(t) && !t.IsAbstract && !t.IsGenericType))
                {
                    _types.TryAdd(t.Name, t);
                    if (t.DeclaringType != null)
                    {
                        _types.TryAdd(t.DeclaringType.Name + t.Name, t);
                    }
                }
            }
            if (!_types.TryGetValue(name, out var type))
            {
                System.Console.Error.WriteLine($"SimEntities: no C# class for the port's {name}: a plain Entity stands in");
                _types[name] = type = typeof(Entity);
            }
            return type;
        }

        private static float Fx(JsonElement a, int i) => a[i].GetInt32() / 65536f;

        private const System.Reflection.BindingFlags AnyInstance =
            System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic;

        private static System.Reflection.FieldInfo FindField(Type t, string name)
        {
            for (; t != null; t = t.BaseType)
            {
                if (t.GetField(name, AnyInstance | System.Reflection.BindingFlags.DeclaredOnly) is { } f)
                {
                    return f;
                }
            }
            return null;
        }

        /// <summary>Sets a C# field the port reports in "f": floats come as 16.16, bools as 0/1.</summary>
        private static void SetField(object o, string name, int raw)
        {
            var f = FindField(o.GetType(), name);
            if (f == null)
            {
                return;
            }
            object v = f.FieldType == typeof(float) ? raw / 65536f : f.FieldType == typeof(bool) ? raw != 0 : f.FieldType == typeof(int) ? raw : null;
            if (v != null)
            {
                f.SetValue(o, v);
            }
        }

        private static void SetField(object o, string name, object value) => FindField(o.GetType(), name)?.SetValue(o, value);

        private static T Blank<T>() where T : Entity
        {
            var e = (T)RuntimeHelpers.GetUninitializedObject(typeof(T));
            e.sprite = new SimSprite();
            return e;
        }

        // What a class's stand-in needs beyond fields the port reports: the C# containers its children live in...
        // (fixups get the entity, its sub-entities and the port's "f" fields, which may name fields the C# class lacks)
        private static readonly Dictionary<Type, Action<Entity, List<Entity>, JsonElement>> Fixups = new()
        {
            [typeof(AnodyneSharp.Entities.Enemy.Bedroom.PewLaser)] = (e, subs, f) =>
            {
                // lasers: an EntityPool of 5, the dead ones too (the bot reads a laser's speed and spawn offset off them)
                bool fast = f.ValueKind == JsonValueKind.Object && f.TryGetProperty("_isFast", out var fv) && fv.GetInt32() != 0;
                var lasers = subs.OfType<AnodyneSharp.Entities.Enemy.Bedroom.PewLaser.Laser>().ToList();
                while (lasers.Count < 5)
                {
                    var l = Blank<AnodyneSharp.Entities.Enemy.Bedroom.PewLaser.Laser>();
                    l.exists = false;
                    l.facing = e.facing;
                    (l.width, l.height) = e.facing is Facing.LEFT or Facing.RIGHT ? (8, 16) : (16, 8);
                    SetField(l, "_isFast", (object)fast);
                    lasers.Add(l);
                }
                foreach (var l in lasers)
                {
                    SetField(l, "spawn_offset", e.facing switch
                    {
                        Facing.DOWN => new Vector2(0, 11),
                        Facing.UP => new Vector2(0, 8),
                        Facing.LEFT => new Vector2(8, 0),
                        _ => Vector2.Zero,
                    });
                }
                var pool = RuntimeHelpers.GetUninitializedObject(typeof(EntityPool<AnodyneSharp.Entities.Enemy.Bedroom.PewLaser.Laser>));
                SetField(pool, "_pool", lasers);
                SetField(e, "lasers", pool);
            },
        };



        /// <summary>
        /// This frame's entities. As in the C# game (PlayState._gridEntities) the list has sub-entities too; an entity's own
        /// (an enemy's bullets: the port's entities with a parent) are also its sub-entities, in `children`.
        /// </summary>
        public static List<Entity> Update(JsonElement state, Dictionary<int, Entity> shadows, Dictionary<Entity, List<Entity>> children)
        {
            var list = new List<Entity>();
            var seen = new HashSet<int>();
            var parents = new List<(Entity, int)>();
            var fieldsOf = new Dictionary<Entity, JsonElement>();
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
                // (a stand-in whose class keeps its preset, e.g. BigCardGate's card count: the map's preset at its spot)
                var presetField = FindField(type, "_preset");
                if (presetField != null && presetField.GetValue(e) == null && state.TryGetProperty("map", out var mapName))
                {
                    presetField.SetValue(e, EntityManager.GetMapEntities(mapName.GetString()).FirstOrDefault(pr =>
                        (int)pr.Position.X == (int)e.Position.X && (int)pr.Position.Y == (int)e.Position.Y));
                }
                FlickeringProp?.SetValue(e, j.GetProperty("flicker").GetBoolean());
                if (j.TryGetProperty("f", out var fields))
                {
                    fieldsOf[e] = fields;
                    foreach (var f in fields.EnumerateObject())
                    {
                        SetField(e, f.Name, f.Value.GetInt32());
                    }
                }
                Fsm[e] = j.TryGetProperty("fsm", out var fsm) ? fsm.GetString() : "";
                var sprite = (SimSprite)e.sprite;
                sprite.CurAnimName = j.GetProperty("anim").GetString();
                sprite.Frame = j.GetProperty("frame").GetInt32();
                int parent = j.GetProperty("parent").GetInt32();
                if (parent >= 0)
                {
                    parents.Add((e, parent));
                }
                list.Add(e);
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
            }
            foreach (var e in list.Concat(children.Values.SelectMany(c => c)).ToList())
            {
                if (Fixups.TryGetValue(e.GetType(), out var fix))
                {
                    fix(e, children.TryGetValue(e, out var subs) ? subs : new List<Entity>(), fieldsOf.TryGetValue(e, out var f) ? f : default);
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
        private readonly SimGameView _view;

        public SimPlayerView(JsonElement p, SimGameView view)
        {
            _p = p;
            _view = view;
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
        Entity IBroomView.dust => _view.EntityById(Int("dustId"));
        bool IBroomView.just_released_dust => Bool("justReleased");
    }
}
