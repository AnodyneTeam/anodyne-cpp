using AnodyneSharp.Entities;
using AnodyneSharp.Input;
using AnodyneSharp.MapData;
using AnodyneSharp.Registry;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace AnodyneSharp.Recording
{
    /// <summary>
    /// Scripted bot. A script is an iterator that yields either a frame (KeyFunctions[] held that frame) or a nested
    /// IEnumerable&lt;object&gt; (a helper such as WalkTo) which is run to completion first. The runner turns each frame
    /// into the KeyState dictionary the game reads, so the result flows through the normal recorder.
    /// </summary>
    public static class BotRunner
    {
        public static bool Active { get; private set; }
        public static Player Player;

        /// <summary>PlayState has a child state (dialogue, cutscene, menu...) consuming input.</summary>
        public static bool PlayBusy;

        /// <summary>The "Continue?" menu after dying is open (Up selects Yes, Accept confirms; No would send us to the death room).</summary>
        public static bool InDeathMenu;

        /// <summary>How many times the player has died (the respawn restarts the innermost Section).</summary>
        public static int Deaths;
        private static int _lastDeaths;

        /// <summary>The entities of the current screen (set by PlayState).</summary>
        public static Func<List<Entity>> Entities = () => new List<Entity>();

        /// <summary>Shown by the realtime overlay: the bot's current goal.</summary>
        public static string Status = "";

        private static readonly Stack<IEnumerator<object>> _stack = new();
        private static Dictionary<Keys, KeyInput.InputState> _prev;
        private static StreamWriter _trace;
        private static long _frame;
        private static long _botFrame;

        /// <summary>Frames the bot has produced (independent of tracing, so bot decisions never depend on whether a trace is written).</summary>
        public static long Frame => _botFrame;
        private static StreamWriter _gba;
        private static int _gbaMask = -1, _gbaRun;

        // GBA KEYINPUT bit order: A B Select Start Right Left Up Down R L
        private static int GbaBit(KeyFunctions f) => f switch
        {
            KeyFunctions.Accept => 1 << 0,
            KeyFunctions.Cancel => 1 << 1,
            KeyFunctions.Pause => 1 << 3,
            KeyFunctions.Right => 1 << 4,
            KeyFunctions.Left => 1 << 5,
            KeyFunctions.Up => 1 << 6,
            KeyFunctions.Down => 1 << 7,
            KeyFunctions.NextPage => 1 << 8,
            KeyFunctions.PreviousPage => 1 << 9,
            _ => 0
        };

        /// <summary>Appends a frame to the GBA input file as run-length "count maskhex" lines (mask: 1 = pressed).</summary>
        private static void WriteGbaFrame(KeyFunctions[] held, bool last)
        {
            if (_gba == null)
            {
                return;
            }
            int mask = 0;
            foreach (var f in held)
            {
                mask |= GbaBit(f);
            }
            if (mask == _gbaMask)
            {
                _gbaRun++;
            }
            else
            {
                if (_gbaRun > 0)
                {
                    _gba.WriteLine($"{_gbaRun} {_gbaMask:x3}");
                }
                _gbaMask = mask;
                _gbaRun = 1;
            }
            if (last)
            {
                _gba.WriteLine($"{_gbaRun} {_gbaMask:x3}");
                _gba.Flush();
            }
        }

        public static void OpenGbaInput(string path)
        {
            _gba = new StreamWriter(path);
            _gba.WriteLine("# frames mask(hex, 1=pressed): A B Sel Start Right Left Up Down R L");
        }

        public static void Start(string name)
        {
            var script = BotScripts.Get(name);
            _stack.Push(script.GetEnumerator());
            Active = true;
            _prev = new Dictionary<Keys, KeyInput.InputState>(KeyInput.KeyState);
        }

        public static void OpenTrace(string tracePath)
        {
            _trace = new StreamWriter(tracePath);
            _trace.WriteLine("frame\tmap\tx\ty\thp\tdialogue\tinv");
        }

        /// <summary>Produces this frame's key states. Sets Active=false (and closes the game) when the script ends.</summary>
        public static Dictionary<Keys, KeyInput.InputState> NextFrame()
        {
            KeyFunctions[] held = Array.Empty<KeyFunctions>();
            _botFrame++;
            if (Deaths != _lastDeaths)
            {
                // Died and respawned at the checkpoint: drop whatever helpers were running down to the innermost Section,
                // which restarts its steps (it notices the changed death count).
                _lastDeaths = Deaths;
                var keep = new List<IEnumerator<object>>(_stack);
                while (_stack.Count > 0 && !(_stack.Peek() is SectionEnumerator))
                {
                    _stack.Pop();
                }
                if (_stack.Count == 0)
                {
                    foreach (var e in Enumerable.Reverse(keep))
                    {
                        _stack.Push(e);
                    }
                }
            }
            while (_stack.Count > 0)
            {
                var top = _stack.Peek();
                bool more;
                try
                {
                    more = top.MoveNext();
                }
                catch (Exception e) when (!(e is BotFatalException) && _stack.Any(s => s is SectionEnumerator))
                {
                    // A step inside a retry Section failed (timeout, no path...): drop to the section and let it start over.
                    System.Console.Error.WriteLine("  section step failed, retrying: " + e.Message);
                    while (_stack.Count > 0 && !(_stack.Peek() is SectionEnumerator))
                    {
                        _stack.Pop();
                    }
                    ((SectionEnumerator)_stack.Peek()).Fail();
                    continue;
                }
                catch (Exception e)
                {
                    System.Console.Error.WriteLine("BOT FAILED: " + e.Message);
                    try { AnodyneSharp.Drawing.SpriteDrawer.SaveRender("/tmp/bot/fail.png"); } catch { }
                    _trace?.Flush();
                    Environment.Exit(2);
                    return null;
                }
                if (!more)
                {
                    _stack.Pop();
                    continue;
                }
                if (top.Current is KeyFunctions[] frame)
                {
                    held = frame;
                    break;
                }
                if (top.Current is IEnumerable<object> nested)
                {
                    _stack.Push(nested.GetEnumerator());
                }
                else if (top.Current is IEnumerator<object> nestedEnum)
                {
                    _stack.Push(nestedEnum);
                }
            }
            WriteGbaFrame(held, _stack.Count == 0);
            if (_stack.Count == 0)
            {
                Active = false;
                GlobalState.ClosingGame = true;
            }

            var down = new HashSet<Keys>();
            foreach (var f in held)
            {
                if (KeyInput.RebindableKeys.TryGetValue(f, out var rk) && rk.Keys.Count > 0)
                {
                    down.Add(rk.Keys[0]);
                }
            }

            var next = new Dictionary<Keys, KeyInput.InputState>();
            foreach (var k in _prev.Keys)
            {
                bool d = down.Contains(k);
                next[k] = !d ? KeyInput.InputState.NONE
                    : _prev[k] == KeyInput.InputState.NONE ? KeyInput.InputState.PRESSED : KeyInput.InputState.HELD;
            }
            _prev = next;
            return next;
        }

        /// <summary>Call after the game state update, once per frame.</summary>
        public static void Trace()
        {
            _frame++;
            if (_trace == null)
            {
                return;
            }
            var p = Player;
            _trace.WriteLine($"{_frame}\t{GlobalState.CURRENT_MAP_NAME}\t{(p == null ? 0 : (int)p.Position.X)}\t{(p == null ? 0 : (int)p.Position.Y)}\t{GlobalState.CUR_HEALTH}\t{(string.IsNullOrEmpty(GlobalState.Dialogue) ? 0 : 1)}\tb{(GlobalState.inventory.HasBroom ? 1 : 0)}k{GlobalState.inventory.GetCurrentMapKeys()}c{GlobalState.inventory.CardCount}");
            if (!Active || _frame % 300 == 0)
            {
                _trace.Flush();
            }
        }
    }

    /// <summary>
    /// Runs a block of route steps and restarts it from the top whenever the player dies (the game puts them back at
    /// the last checkpoint with items and keys kept, so steps that are already done must be harmless to repeat).
    /// </summary>
    /// <summary>Not retried by an enclosing Section: the whole run stops.</summary>
    public sealed class BotFatalException : Exception
    {
        public BotFatalException(string m) : base(m) { }
    }

    public sealed class SectionEnumerator : IEnumerator<object>
    {
        private readonly Func<IEnumerable<object>> _body;
        private readonly string _name;
        private IEnumerator<object> _inner;
        private int _deathsAtStart;
        private int _tries;

        private readonly Func<IEnumerable<object>> _prereq;

        /// <summary>`prereq` (optional) is replayed first after a death/failure, to get back to where `body` expects to start.</summary>
        public SectionEnumerator(string name, Func<IEnumerable<object>> body, Func<IEnumerable<object>> prereq = null)
        {
            _name = name;
            _body = body;
            _prereq = prereq;
        }

        public object Current { get; private set; }

        /// <summary>A step in the body failed without dying: restart the body from the top.</summary>
        public void Fail()
        {
            _failed = true;
        }

        private bool _failed;

        public bool MoveNext()
        {
            if (_inner == null || _failed || BotRunner.Deaths != _deathsAtStart)
            {
                if (_inner != null && ++_tries > 40)
                {
                    throw new BotFatalException($"bot: section '{_name}' failed {_tries} times");
                }
                _failed = false;
                _deathsAtStart = BotRunner.Deaths;
                _inner = Restart().GetEnumerator();
            }
            if (!_inner.MoveNext())
            {
                return false;
            }
            Current = _inner.Current;
            return true;
        }

        private IEnumerable<object> Restart()
        {
            if (_tries > 0)
            {
                BotRunner.Status = $"{_name}: retry {_tries}";
                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    System.Console.Error.WriteLine($"  section '{_name}': retry {_tries}");
                }
                // Let the respawn finish before replaying the steps.
                for (int i = 0; i < 90 || BotRunner.PlayBusy; i++)
                {
                    // Dialogue/cutscene boxes after the respawn need Accept to move on.
                    yield return BotScript.AdvanceKeys(i);
                }
                BotScript.ResetIntended();
                if (_prereq != null)
                {
                    foreach (var o in _prereq())
                    {
                        yield return o;
                    }
                }
            }
            foreach (var o in _body())
            {
                yield return o;
            }
        }

        public void Reset() { }
        public void Dispose() { }
    }

    public static class BotExt
    {
        /// <summary>Runs the helper's frames until cond() becomes true (checked before every frame).</summary>
        public static IEnumerable<object> AsEnumerable(this IEnumerator<object> e)
        {
            while (e.MoveNext())
            {
                yield return e.Current;
            }
        }

        public static IEnumerable<object> TakeUntil(this IEnumerable<object> frames, Func<bool> cond)
        {
            foreach (var f in frames)
            {
                if (cond())
                {
                    yield break;
                }
                yield return f;
            }
        }
    }

    public abstract partial class BotScript
    {
        public abstract IEnumerable<object> Run();

        protected static Player Player => BotRunner.Player;
        protected static string Map => GlobalState.CURRENT_MAP_NAME;
        protected static Vector2 Pos => Player == null ? Vector2.Zero : Player.Position;
        protected static bool Talking => !string.IsNullOrEmpty(GlobalState.Dialogue) || BotRunner.PlayBusy;

        protected static IEnumerable<object> Say(string status)
        {
            BotRunner.Status = status;
            yield break;
        }

        protected static KeyFunctions[] K(params KeyFunctions[] k) => k;

        protected static IEnumerable<object> Wait(int frames)
        {
            for (int i = 0; i < frames; i++)
            {
                yield return K();
            }
        }

        protected static IEnumerable<object> Tap(KeyFunctions k)
        {
            yield return K(k);
            yield return K();
        }

        protected static IEnumerable<object> Hold(int frames, params KeyFunctions[] k)
        {
            for (int i = 0; i < frames; i++)
            {
                yield return K(k);
            }
        }

        protected static IEnumerable<object> Until(Func<bool> cond, int timeoutFrames, string what)
        {
            for (int i = 0; i < timeoutFrames; i++)
            {
                if (cond())
                {
                    yield break;
                }
                yield return K();
            }
            throw new TimeoutException($"bot: timed out waiting for {what} (map {Map}, pos {Pos})");
        }

        /// <summary>Advances dialogue (mashing Accept) until it has ended and stays ended for a few frames.</summary>
        protected static IEnumerable<object> ReadDialogue(int timeoutFrames = 3000)
        {
            BotRunner.Status = "reading dialogue";
            int idle = 0;
            for (int i = 0; i < timeoutFrames && idle < 20; i++)
            {
                if (Talking)
                {
                    idle = 0;
                    yield return BotScript.AdvanceKeys(i);
                }
                else
                {
                    idle++;
                    yield return K();
                }
            }
        }

        /// <summary>
        /// Pathfinds over the map's tiles to the tile containing world point (x,y) and walks there. If the bot stops
        /// making progress (a gate, block or NPC the collision map doesn't know about) it marks the next tile blocked
        /// and replans.
        /// </summary>
        protected static IEnumerable<object> GoTo(float x, float y, int maxReplans = 6)
        {
            var goal = BotNav.TileOf(new Vector2(x, y)); // (x,y) = where the hitbox top-left should end up
            BotRunner.Status = $"go to tile {goal.X},{goal.Y}";
            return GoToWhere(t => t == goal, $"({x},{y})", maxReplans, goal);
        }

        /// <summary>Like GoTo but to the nearest tile satisfying isGoal.</summary>
        protected static IEnumerable<object> GoToWhere(Func<Point, bool> isGoal, string what, int maxReplans = 6, Point? forced = null)
        {
            int shooterFights = 0;
            bool pluggedTried = false;
            for (int attempt = 0; attempt <= maxReplans; attempt++)
            {
                replan:
                var path = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced);
                bool jumping = false;
                if (path == null)
                {
                    // Spikes and narrow rivers: with the boots they are jumped over.
                    if (GlobalState.inventory.CanJump)
                    {
                        BotNav.AllowJump = true;
                        path = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced);
                        BotNav.AllowJump = false;
                        jumping = path != null;
                    }
                    // Without boots a strip of river only a tile or two wide is simply waded across.
                    if (path == null)
                    {
                        BotNav.AllowWater = true;
                        var wade = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced);
                        BotNav.AllowWater = false;
                        if (wade != null && wade.Count(t => BotNav.IsConveyor(t)) <= 2)
                        {
                            path = wade;
                        }
                    }
                    // Or a river is in the way: cross it on a dust raft.
                    if (path == null && !InRaft)
                    {
                        BotNav.AllowWater = true;
                        var wet = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced);
                        BotNav.AllowWater = false;
                        if (wet != null && wet.Count > 0)
                        {
                            var last = BotNav.CentreOf(wet[wet.Count - 1]);
                            yield return RaftTo(last.X, last.Y);
                            yield break;
                        }
                    }
                    if (path == null)
                    {
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine($"  NOPATH at {Pos} grid {Grid}: " + string.Join(" ", BotRunner.Entities().Where(e => e.exists && (e.GetType().Name is "Dust" or "PewLaser" or "Shieldy" or "Gate" or "Boulder")).Select(e => $"{e.GetType().Name}@{(int)e.Position.X},{(int)e.Position.Y}")));
                            yield return Exec($"&{Grid.X},{Grid.Y},{Grid.X},{Grid.Y}");
                        }
                        throw new InvalidOperationException($"bot: no path from {BotNav.TileOf(Pos)} to {what} on {Map}");
                    }
                }
                if (path != null && path.Count > 30 && !InRaft)
                {
                    // A long way round on dry land: a raft across the river may be far shorter.
                    BotNav.AllowWater = true;
                    BotNav.AllowJump = GlobalState.inventory.CanJump;
                    var shortcut = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced);
                    BotNav.AllowWater = false;
                    BotNav.AllowJump = false;
                    if (shortcut != null && shortcut.Count + 20 < path.Count && !shortcut.Any(t => BotNav.IsSpike(t)) && shortcut.Count(t => BotNav.IsConveyor(t)) <= 2)
                    {
                        path = shortcut;
                    }
                    else if (shortcut != null && shortcut.Count + 20 < path.Count && !shortcut.Any(t => BotNav.IsSpike(t)))
                    {
                        var lastT = BotNav.CentreOf(shortcut[shortcut.Count - 1]);
                        yield return RaftTo(lastT.X, lastT.Y);
                        yield break;
                    }
                }
                if (DodgeEnabled && !pluggedTried && !InRaft && GlobalState.inventory.HasBroom && path.Count > 0 && HaveLooseDust())
                {
                    // A route through a laser beam with dust lying about: plug the emitter instead of racing (or waiting for) its shots.
                    var crossing = EmittersCrossing(path);
                    if (crossing.Count > 0)
                    {
                        pluggedTried = true;
                        yield return BlockLasers(e => crossing.Contains(e));
                        goto replan;
                    }
                }
                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    string via = string.Join(">", path.Take(14).Select(p => $"{p.X},{p.Y}"));
                    System.Console.Error.WriteLine($"  path to {what}: {path.Count} tiles from {BotNav.TileOf(Pos)} via {via}");
                }
                bool stuck = false;
                for (int pi = 0; pi < path.Count; pi++)
                {
                    var tile = path[pi];
                    if (jumping && BotNav.IsJumpable(tile))
                    {
                        int pj = pi;
                        while (pj < path.Count && BotNav.IsJumpable(path[pj]))
                        {
                            pj++;
                        }
                        var land = pj < path.Count ? path[pj] : path[pj - 1];
                        var from = pi == 0 ? BotNav.TileOf(Pos) : path[pi - 1];
                        var jk = tile.X > from.X ? KeyFunctions.Right : tile.X < from.X ? KeyFunctions.Left : tile.Y > from.Y ? KeyFunctions.Down : KeyFunctions.Up;
                        BotRunner.Status = "jump over the spikes";
                        yield return K(jk, KeyFunctions.Cancel);
                        for (int j = 0; j < 90 && !(BotNav.TileOf(Pos) == land && Player.state != PlayerState.AIR); j++)
                        {
                            yield return K(jk);
                        }
                        pi = pj - 1;
                        continue;
                    }
                    var c = BotNav.CentreOf(tile);
                    Vector2 last = Pos;
                    int still = 0;
                    int edgePushes = 0;
                    for (int i = 0; i < 200; i++)
                    {
                        if (Talking)
                        {
                            still = 0;
                            yield return BotScript.AdvanceKeys(i);
                            continue;
                        }
                        if (GlobalState.ScreenTransition)
                        {
                            still = 0;
                            yield return K();
                            continue;
                        }
                        var p = Pos;
                        if (Math.Abs(c.X - p.X) <= 2f && Math.Abs(c.Y - p.Y) <= 2f)
                        {
                            break;
                        }
                        if (DodgeEnabled && AutoFight && shooterFights < 3 && !InRaft && GlobalState.inventory.HasBroom && GlobalState.CUR_HEALTH >= TankMinHp && ShooterNear(80) is Entity shooter)
                        {
                            // A frog/plant on the way would keep us dancing in and out of its bubbles: kill it (2 hits), pick up the hearts, replan.
                            shooterFights++;
                            BotRunner.Status = $"clear {shooter.GetType().Name} on the way";
                            yield return Fight(900, soft: true, only: e => e == shooter);
                            goto replan;
                        }
                        still = (p == last) ? still + 1 : 0;
                        last = p;
                        if (still > 40 && edgePushes < 3 && new Point(tile.X / 10, tile.Y / 10) != Grid)
                        {
                            // Right at a screen edge without crossing it: shove on in that direction before calling the tile blocked.
                            edgePushes++;
                            var here0 = BotNav.TileOf(Pos);
                            var pushDir = tile.X > here0.X ? KeyFunctions.Right : tile.X < here0.X ? KeyFunctions.Left : tile.Y > here0.Y ? KeyFunctions.Down : KeyFunctions.Up;
                            yield return Hold(25, pushDir);
                            still = 0;
                            continue;
                        }
                        if (still > 40)
                        {
                            BotNav.Blocked.Add(tile);
                            stuck = true;
                            break;
                        }
                        // Correct the perpendicular axis first so the 10px hitbox lines up with 1-tile corridors.
                        float dx = c.X - p.X, dy = c.Y - p.Y;
                        KeyFunctions dir = Math.Abs(dx) > 2f
                            ? (dx < 0 ? KeyFunctions.Left : KeyFunctions.Right)
                            : (dy < 0 ? KeyFunctions.Up : KeyFunctions.Down);
                        var safe = SafeMove(dir, null, Math.Abs(dx) > 2f ? Math.Abs(dx) : Math.Abs(dy));
                        if (Dodged)
                        {
                            still = 0;
                        }
                        yield return safe;
                    }
                    if (stuck)
                    {
                        break;
                    }
                }
                if (!stuck && isGoal(BotNav.TileOf(Pos)))
                {
                    yield break;
                }
                if (!stuck && path.Count > 0)
                {
                    // Ran out of time on a tile without being detected as stuck (e.g. pushed around): block it and retry.
                    BotNav.Blocked.Add(path[0]);
                }
            }
            throw new TimeoutException($"bot: GoTo {what} gave up at {Pos} on {Map}");
        }

        /// <summary>Walks in a straight line (one axis at a time, x first unless yFirst) until within tol of the target.</summary>
        protected static IEnumerable<object> WalkTo(float x, float y, float tol = 2f, bool yFirst = false, int timeout = 900)
        {
            BotRunner.Status = $"walk to {x:0},{y:0}  (at {Pos.X:0},{Pos.Y:0})";
            Vector2 last = Pos;
            int still = 0;
            for (int i = 0; i < timeout; i++)
            {
                if (Talking)
                {
                    still = 0;
                    yield return BotScript.AdvanceKeys(i);
                    continue;
                }
                if (GlobalState.ScreenTransition)
                {
                    still = 0;
                    yield return K();
                    continue;
                }
                var p = Pos;
                if (i % 10 == 0)
                {
                    BotRunner.Status = $"walk to {x:0},{y:0}  (at {p.X:0},{p.Y:0})";
                }
                still = (p == last) ? still + 1 : 0;
                last = p;
                if (still > 90)
                {
                    throw new TimeoutException($"bot: WalkTo({x},{y}) stuck at {p} on {Map}");
                }
                float dx = x - p.X, dy = y - p.Y;
                bool xDone = Math.Abs(dx) <= tol, yDone = Math.Abs(dy) <= tol;
                if (xDone && yDone)
                {
                    yield break;
                }
                KeyFunctions dir;
                if (yFirst ? (!yDone) : xDone)
                {
                    dir = dy < 0 ? KeyFunctions.Up : KeyFunctions.Down;
                }
                else
                {
                    dir = dx < 0 ? KeyFunctions.Left : KeyFunctions.Right;
                }
                yield return K(dir);
            }
            throw new TimeoutException($"bot: WalkTo({x},{y}) timed out at {Pos} on {Map}");
        }
    }

    public static class BotScripts
    {
        /// <summary>The route in order; every part starts where the previous one ended.</summary>
        public static readonly string[] PartNames = { "nexus", "roadway", "overworld1", "temple", "temple1", "forest2", "fields", "beach" };

        private static BotScript Part(string name) => name switch
        {
            "nexus" => new StartToNexus(),
            "roadway" => new NexusToRoadway(),
            "overworld1" => new Roadway(),
            "temple" => new BlueForest(),
            "temple1" => new Temple(),
            "forest2" => new BlueForest2(),
            "fields" => new Fields(),
            "beach" => new Beach(),
            _ => throw new ArgumentException(name)
        };

        /// <summary>Plays the route from the title screen up to and including the named part.</summary>
        public static IEnumerable<object> Through(string last)
        {
            foreach (var n in PartNames)
            {
                yield return Part(n).Run();
                if (n == last.ToLowerInvariant())
                {
                    yield break;
                }
            }
        }

        public static IEnumerable<object> Get(string name)
        {
            switch (name.ToLowerInvariant())
            {
                case "nexus": return new StartToNexus().Run();
                case "keys": return new KeysScript().Run();
                default:
                    if (Array.IndexOf(PartNames, name.ToLowerInvariant()) >= 0)
                    {
                        return Through(name);
                    }
                    throw new ArgumentException($"unknown bot script '{name}'");
            }
        }
    }
}

namespace AnodyneSharp.Recording
{
    /// <summary>
    /// Development aid: Boot, then play the moves in $ANODYNE_BOT_KEYS, e.g. "U40 R20 A W30 Au10".
    /// U/D/L/R hold a direction N frames, A taps Accept (N times spaced 10 frames apart), C Cancel, W waits, T reads dialogue.
    /// </summary>
    public class KeysScript : BotScript
    {
        public override IEnumerable<object> Run()
        {
            string pre = Environment.GetEnvironmentVariable("ANODYNE_BOT_PRE");
            if (!string.IsNullOrEmpty(pre))
            {
                yield return BotScripts.Through(pre);
            }
            else
            {
                yield return StartToNexus.Boot();
            }
            yield return Exec(Environment.GetEnvironmentVariable("ANODYNE_BOT_KEYS") ?? "");
            yield return Wait(30);
        }
    }
}
