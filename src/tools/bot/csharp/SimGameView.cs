using AnodyneSharp.Entities;
using AnodyneSharp.MapData;
using AnodyneSharp.MapData.Tiles;
using AnodyneSharp.Utilities;
using Microsoft.Xna.Framework;
using System;
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

        public int GetTile(Layer layer, Point p) => 0;
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
