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

        public void Dispose()
        {
            _client.Dispose();
        }
    }
}
