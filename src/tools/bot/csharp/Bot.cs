using AnodyneSharp.Entities;
using AnodyneSharp.Input;
using AnodyneSharp.MapData;
using AnodyneSharp.Registry;
using Microsoft.Xna.Framework;
using Microsoft.Xna.Framework.Input;
using System;
using System.Text.RegularExpressions;
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
        /// <summary>ANODYNE_ROUTE=100: take the side rooms (cards, secrets) as well as the walkthrough's main path.</summary>
        public static readonly bool Full100 = Environment.GetEnvironmentVariable("ANODYNE_ROUTE") == "100";
        /// <summary>Token p1: do not fetch dust to plug beams on the way (the way on is through them, at a documented cost in hearts).</summary>
        public static bool NoAutoPlug;
        /// <summary>Hearts to be won back after a hit (-1 = no debt); see RecoverFrames.</summary>
        public static int DebtBaseline = -1;
        /// <summary>Hearts per part that may stay lost after the recovery window (ANODYNE_BOT_CHIP, default 2); reset at the start of each part.</summary>
        public static int ChipLeft = int.TryParse(Environment.GetEnvironmentVariable("ANODYNE_BOT_CHIP"), out int cl) ? cl : 2;
        public static readonly int ChipPerPart = ChipLeft;
        public static readonly int RecoverFrames = int.TryParse(Environment.GetEnvironmentVariable("ANODYNE_BOT_RECOVER"), out int rf) ? rf : 900;
        private static long _debtSince;
        public static bool PlayBusy;
        private static string _lastBusyName = "";
        public static string PlayBusyName = ""; // which child states of the play state are open (for the failure ring)

        /// <summary>The "Continue?" menu after dying is open (Up selects Yes, Accept confirms; No would send us to the death room).</summary>
        public static bool InDeathMenu;

        /// <summary>How many times the player has died (the respawn restarts the innermost Section).</summary>
        public static int Deaths;

        /// <summary>Dev: ANODYNE_BOT_STOP_BEFORE=<section name> abandons the route when that section is about to start and runs ANODYNE_BOT_KEYS instead.</summary>
        public static readonly string StopBefore = Environment.GetEnvironmentVariable("ANODYNE_BOT_STOP_BEFORE");
        public static bool JumpToKeys;
        private static int _lastDeaths;

        /// <summary>The entities of the current screen (set by PlayState).</summary>
        public static Func<List<Entity>> Entities = () => new List<Entity>();

        /// <summary>Shown by the realtime overlay: the bot's current goal.</summary>
        public static string Status = "";
        private static string _traceStatus;
        private static Microsoft.Xna.Framework.Point _traceTile;

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
        /// <summary>Taking damage or dying (game over) is a fatal error for the bot, unless ANODYNE_BOT_ALLOW_DAMAGE is set (scene tests).</summary>
        private static readonly bool _allowDamage = Environment.GetEnvironmentVariable("ANODYNE_BOT_ALLOW_DAMAGE") != null;
        private static int _hpSeen = -1, _hpSeenAllow = -1;

        /// <summary>Hearts the run may lose in total during a boss fight (set by the boss routine): the Seer is not dodge-proof yet, everything else is fatal.</summary>
        public static int BossDamageBudget;

        /// <summary>Dev only: let a failing step restart its section (the old behaviour). By default a step that fails is a fatal error.</summary>
        private static readonly bool _allowRetry = Environment.GetEnvironmentVariable("ANODYNE_BOT_RETRY") != null;

        // Stuck watchdog: nothing observable changed for this many frames.
        private static string _stuckSig = "";
        private static int _busyFrames;
        private static long _stuckSince;

        /// <summary>Frames without any change (position, health, dialogue, keys, screen) before the run is called stuck. Deliberate waits reset the clock.</summary>
        public static int StuckLimit = int.TryParse(Environment.GetEnvironmentVariable("ANODYNE_BOT_STUCK"), out int sl) ? sl : 300;

        /// <summary>Called by steps that stand still on purpose (Wait, standing on a button).</summary>
        public static void NotStuck() => _stuckSince = _botFrame;

        /// <summary>A fade, flash or screen shake is playing (boss death scenes and the like): nothing else changes for a while.</summary>
        private static bool CutsceneRunning()
        {
            try { return GlobalState.flash.Active() || GlobalState.screenShake.Active() || !GlobalState.darkness.ReachedAlpha; }
            catch { return false; }
        }

        private static void CheckStuck()
        {
            var p = Player;
            string sig = $"{GlobalState.CURRENT_MAP_NAME}|{(p == null ? 0 : (int)p.Position.X)}|{(p == null ? 0 : (int)p.Position.Y)}|{GlobalState.CUR_HEALTH}|{GlobalState.Dialogue}|{GlobalState.inventory.CardCount}|{GlobalState.inventory.GetCurrentMapKeys()}|{GlobalState.ScreenTransition}";
            // Dialogue and cutscene states (anything the play state has open) do the waiting themselves: the clock only runs once they are over.
            if (!string.IsNullOrEmpty(GlobalState.Dialogue) || PlayBusy)
            {
                if (++_busyFrames > 9000)
                {
                    System.Console.Error.WriteLine($"BOT FAILED: a dialogue/cutscene ({PlayBusyName}) has been open for {_busyFrames} frames on {GlobalState.CURRENT_MAP_NAME} at {p?.Position} ({Status})");
                    DumpRing();
                    _trace?.Flush();
                    Environment.Exit(2);
                }
                _stuckSince = _botFrame;
            }
            else
            {
                _busyFrames = 0;
            }
            if (sig != _stuckSig)
            {
                _stuckSig = sig;
                _stuckSince = _botFrame;
                return;
            }
            // a long dialogue changes nothing we can see between its pages (same text, same place), so it gets a much longer leash
            if (_botFrame - _stuckSince > (string.IsNullOrEmpty(GlobalState.Dialogue) && !PlayBusy ? (CutsceneRunning() ? 1500 : StuckLimit) : 3000) && !_allowRetry)
            {
                System.Console.Error.WriteLine($"BOT FAILED: stuck for {_botFrame - _stuckSince} frames on {GlobalState.CURRENT_MAP_NAME} at {p?.Position} ({Status})");
                try { AnodyneSharp.Drawing.SpriteDrawer.SaveRender("/tmp/bot/fail.png"); } catch { }
                DumpRing();
                _trace?.Flush();
                Environment.Exit(2);
            }
        }

        // The last frames of the run (player, keys, enemies): written out when the bot fails so the failure can be rebuilt in the offline simulator.
        private static readonly Queue<string> _ring = new();

        // Audit of broom swings that have nothing to hit: every Accept press while no dialogue/cutscene is open and no enemy, dust or
        // interactable is within reach is counted by the status that asked for it, and the totals are printed at exit (ANODYNE_BOT_SWING_AUDIT=1).
        private static readonly Dictionary<string, int> _swingsUseful = new(), _swingsIdle = new();

        private static void SwingAudit(KeyFunctions[] held)
        {
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_SWING_AUDIT") == null || held == null || !held.Contains(KeyFunctions.Accept) || Player == null)
            {
                return;
            }
            if (!string.IsNullOrEmpty(GlobalState.Dialogue) || PlayBusy)
            {
                return; // text being advanced
            }
            var pc = new Vector2(Player.Hitbox.Center.X, Player.Hitbox.Center.Y);
            bool near = Entities().Any(e => e.exists && e != Player && !(e is AnodyneSharp.Entities.Player) &&
                (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - pc).Length() < 40 && (e.GetType().IsDefined(typeof(AnodyneSharp.Entities.Enemy.EnemyAttribute), false) ||
                 Regex.IsMatch(e.GetType().Name, "Dust|Sage|Chest|Button|Statue|Block|Console|Sentinel|Gate|Checkpoint|Treasure|Slime|Shieldy|Annoyer|Seer")));
            var key = Regex.Replace(Status ?? "", @"[\d,.\-]+", "#");
            var d = near ? _swingsUseful : _swingsIdle;
            d[key] = d.TryGetValue(key, out int n) ? n + 1 : 1;
            if (!near && _swingsIdle[key] == 1)
            {
                System.Console.Error.WriteLine($"IDLE SWING f{_botFrame} P={Player.Hitbox} ({Status})");
            }
        }

        public static void PrintSwingAudit()
        {
            if (_swingsIdle.Count + _swingsUseful.Count == 0)
            {
                return;
            }
            System.Console.Error.WriteLine("SWING AUDIT: idle (nothing within 40px) / with a target near, by status:");
            foreach (var k in _swingsIdle.Keys.Union(_swingsUseful.Keys).OrderByDescending(k => _swingsIdle.GetValueOrDefault(k)))
            {
                System.Console.Error.WriteLine($"  {_swingsIdle.GetValueOrDefault(k),5} / {_swingsUseful.GetValueOrDefault(k),5}  {k}");
            }
        }

        private static void RingRecord(KeyFunctions[] held)
        {
            try
            {
                var p = Player;
                if (p == null)
                {
                    return;
                }
                var sb = new System.Text.StringBuilder();
                sb.Append($"f{_botFrame} P={p.Hitbox.X},{p.Hitbox.Y},{p.Hitbox.Width},{p.Hitbox.Height} face={p.facing} hp={GlobalState.CUR_HEALTH} keys={string.Join("+", held)} status=[{Status}] swinging={(p.broom != null && p.broom.exists)} chain={string.Join("<", _stack.Take(3).Select(en => System.Text.RegularExpressions.Regex.Replace(en.GetType().Name, "^<|>d__\\d+$", "")))} busy={PlayBusyName} dlg={(string.IsNullOrEmpty(GlobalState.Dialogue) ? "" : GlobalState.Dialogue.Replace(" ", "_").Substring(0, Math.Min(40, GlobalState.Dialogue.Length)))} fin={GlobalState.LastDialogueFinished} trans={(GlobalState.ScreenTransition ? 1 : 0)} E:");
                foreach (var e in Entities())
                {
                    if (e.exists && (e.GetType().IsDefined(typeof(AnodyneSharp.Entities.Enemy.EnemyAttribute), false) || e.GetType().Name.StartsWith("Seer")))
                    {
                        sb.Append($" {e.GetType().Name}@{e.Hitbox.X},{e.Hitbox.Y},{e.Hitbox.Width},{e.Hitbox.Height}v{e.velocity.X:0.#},{e.velocity.Y:0.#}");
                    }
                }
                _ring.Enqueue(sb.ToString());
                if (_ring.Count > 240)
                {
                    _ring.Dequeue();
                }
            }
            catch
            {
            }
        }

        private static void DumpRing()
        {
            PrintSwingAudit();
            try
            {
                var lines = new List<string>(_ring);
                var pl = Player;
                if (pl != null)
                {
                    // everything within 40px of the player (solid or not): what a failed walk is pressing against
                    var pc = new Vector2(pl.Hitbox.Center.X, pl.Hitbox.Center.Y);
                    lines.Add("NEAR (within 40px): " + string.Join(" ", Entities().Where(e => e.exists && e != pl && (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - pc).Length() < 40)
                        .Select(e => $"{e.GetType().Name}@{e.Hitbox.X},{e.Hitbox.Y},{e.Hitbox.Width}x{e.Hitbox.Height}{(e.Solid ? "S" : "")}")));
                }
                System.IO.File.WriteAllLines("/tmp/bot/fail_ring.txt", lines);
            }
            catch
            {
            }
        }

        private static void CheckNoDamage()
        {
            if (Player == null)
            {
                return;
            }
            int hp = GlobalState.CUR_HEALTH;
            if (Player != null && Environment.GetEnvironmentVariable("ANODYNE_STATUS_TRACE") != null)
            {
                // Debug: every change of the bot's status or of the player's tile, with the frame.
                var tileNow = new Microsoft.Xna.Framework.Point((int)((Player.Position.X + 5) / 16), (int)((Player.Position.Y + 6) / 16));
                if (Status != _traceStatus || tileNow != _traceTile)
                {
                    _traceStatus = Status;
                    _traceTile = tileNow;
                    System.Console.Error.WriteLine($"  st f{_botFrame} {Player.Position.X:0},{Player.Position.Y:0} {Player.state} [{Status}]");
                }
            }
            string why = null;
            if (Deaths != _lastDeaths)
            {
                why = "game over (died)";
            }
            else if (_hpSeen >= 0 && hp < _hpSeen)
            {
                if (BossDamageBudget >= _hpSeen - hp)
                {
                    BossDamageBudget -= _hpSeen - hp; // a boss fight with a heart budget: logged, not fatal
                    if (DebtBaseline >= 0)
                    {
                        DebtBaseline -= _hpSeen - hp; // (and not owed to an earlier hit's debt either: that one only wants its own hearts back)
                    }
                    System.Console.Error.WriteLine($"  boss hit f{_botFrame}: HP {_hpSeen}->{hp}, budget left {BossDamageBudget} by {GlobalState.DamageDealer} P={Player.Hitbox} ({Status}) " + string.Join(" ", Entities().Where(e => e.exists && e.GetType().Name.StartsWith("Seer")).Select(e => $"{e.GetType().Name}@{e.Hitbox.X},{e.Hitbox.Y}")));
                }
                else if (hp > 0 && RecoverFrames > 0)
                {
                    // A hit is tolerated if the hearts it cost are picked up again (drops from kills) within RecoverFrames: that is how a human tanks one.
                    if (DebtBaseline < 0)
                    {
                        DebtBaseline = _hpSeen;
                        _debtSince = _botFrame;
                    }
                    System.Console.Error.WriteLine($"  hit f{_botFrame}: HP {_hpSeen}->{hp}, back to {DebtBaseline} by f{_debtSince + RecoverFrames} or it is fatal ({Status}) by {GlobalState.DamageDealer} at {Player.Hitbox}" +
                        (GlobalState.DamageDealer == "Frog bullet" ? " bullets: " + string.Join(" ", Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Enemy.Crowd.Frog.BurstBullet).Select(e => $"{e.Hitbox} o{e.offset.Y:0} v{e.velocity.Length():0}")) : ""));
                }
                else
                {
                    why = $"took damage (HP {_hpSeen}->{hp})";
                }
            }
            if (DebtBaseline >= 0 && why == null)
            {
                if (!string.IsNullOrEmpty(GlobalState.Dialogue) || PlayBusy)
                {
                    _debtSince++; // the game is paused for text: no time passes in which a heart could be won back
                }
                if (hp >= Math.Min(DebtBaseline, GlobalState.MAX_HEALTH))
                {
                    DebtBaseline = -1; // recovered
                }
                else if (_botFrame - _debtSince > RecoverFrames)
                {
                    int missing = Math.Min(DebtBaseline, GlobalState.MAX_HEALTH) - hp;
                    if (missing <= ChipLeft)
                    {
                        // A heart or two that never dropped: a small, documented allowance per part (resets at the start of every part).
                        ChipLeft -= missing;
                        System.Console.Error.WriteLine($"  chip damage accepted at f{_botFrame}: {missing} heart(s) not won back (allowance left {ChipLeft}) ({Status})");
                        DebtBaseline = -1;
                    }
                    else
                    {
                        why = $"took damage and did not get the hearts back within {RecoverFrames} frames (HP {DebtBaseline}->{hp}, chip allowance {ChipLeft})";
                    }
                }
            }
            _hpSeen = hp;
            if (why != null || (_allowDamage && _hpSeenAllow >= 0 && hp < _hpSeenAllow))
            {
                string near = "";
                try
                {
                    near = string.Join(" ", Entities().Where(e => e.exists && Math.Abs(e.Position.X - Player.Position.X) < 90 && Math.Abs(e.Position.Y - Player.Position.Y) < 90).Select(e => $"{e.GetType().Name}@{(int)e.Position.X},{(int)e.Position.Y}"));
                }
                catch { }
                System.Console.Error.WriteLine($"  HIT near: {near}");
            }
            if (_allowDamage) { _hpSeenAllow = hp; return; }
            if (why != null)
            {
                System.Console.Error.WriteLine($"BOT FAILED: {why} on {GlobalState.CURRENT_MAP_NAME} at {Player.Position} frame {_botFrame} doing [{Status}]");
                try { AnodyneSharp.Drawing.SpriteDrawer.SaveRender("/tmp/bot/fail.png"); } catch { }
                DumpRing();
                _trace?.Flush();
                Environment.Exit(2);
            }
        }

        // Per-frame time budget: the bot must not be what slows the game down. A frame over SlowFrameMs is reported with its cause, and a
        // run whose average over a window is over MaxAvgMs is a fatal error (a planner that blows up shows up at once, not as "it feels slow").
        private static readonly double SlowFrameMs = double.TryParse(Environment.GetEnvironmentVariable("ANODYNE_BOT_SLOW_MS"), out double sm) ? sm : 40;
        private static readonly double MaxAvgMs = double.TryParse(Environment.GetEnvironmentVariable("ANODYNE_BOT_MAX_AVG_MS"), out double am) ? am : 12;
        private static double _winMs; private static int _winN; private static int _slowReports;

        public static Dictionary<Keys, KeyInput.InputState> NextFrame()
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            AnodyneSharp.Recording.SpaceTime.CallsThisFrame = 0;
            AnodyneSharp.Recording.SpaceTime.NodesThisFrame = 0;
            var result = NextFrameCore();
            double ms = sw.Elapsed.TotalMilliseconds;
            _winMs += ms;
            if (ms > SlowFrameMs && _slowReports++ < 40)
            {
                System.Console.Error.WriteLine($"SLOW BOT FRAME {ms:0.0}ms at f{_botFrame} ({Status}): spacetime calls {AnodyneSharp.Recording.SpaceTime.CallsThisFrame}, nodes {AnodyneSharp.Recording.SpaceTime.NodesThisFrame}");
            }
            if (++_winN >= 300)
            {
                double avg = _winMs / _winN;
                if (avg > MaxAvgMs)
                {
                    System.Console.Error.WriteLine($"BOT FAILED: the bot itself is slow: {avg:0.0}ms per frame over the last {_winN} frames at f{_botFrame} ({Status}); spacetime calls/nodes in the last frame {AnodyneSharp.Recording.SpaceTime.CallsThisFrame}/{AnodyneSharp.Recording.SpaceTime.NodesThisFrame}");
                    DumpRing();
                    _trace?.Flush();
                    Environment.Exit(4);
                }
                _winMs = 0; _winN = 0;
            }
            return result;
        }

        private static Dictionary<Keys, KeyInput.InputState> NextFrameCore()
        {
            KeyFunctions[] held = Array.Empty<KeyFunctions>();
            _botFrame++;
            if (JumpToKeys)
            {
                JumpToKeys = false;
                _stack.Clear();
                _stack.Push(new KeysOnlyScript().Run().GetEnumerator());
            }
            if (PlayBusyName != _lastBusyName && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
            {
                System.Console.Error.WriteLine($"  busy '{_lastBusyName}' -> '{PlayBusyName}' at f{_botFrame} P={Player?.Position} dialogue={(string.IsNullOrEmpty(GlobalState.Dialogue) ? "-" : GlobalState.Dialogue.Substring(0, Math.Min(30, GlobalState.Dialogue.Length)))} ({Status})");
            }
            _lastBusyName = PlayBusyName;
            BotNav.RefreshShooterTiles();
            CheckNoDamage();
            CheckStuck();
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_SNAP_AT") is string snap && Environment.GetEnvironmentVariable("ANODYNE_BOT_SAVEDIR") is string sd2)
            {
                // Dev: ANODYNE_BOT_SNAP_AT=<frame>:<dir> writes a save of the game at that frame (for scene tests of a failure in the chain).
                var parts = snap.Split(':', 2);
                if (long.Parse(parts[0]) == _botFrame && Player != null)
                {
                    GlobalState.checkpoint = new GlobalState.CheckPoint(GlobalState.CURRENT_MAP_NAME, Player.Position);
                    GlobalState.SaveGame();
                    SectionEnumerator.CopyDirPublic(sd2, parts[1]);
                }
            }
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
                catch (Exception e) when (!(e is BotFatalException) && _allowRetry && _stack.Any(s => s is SectionEnumerator))
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
                    // which steps were running (innermost first): the enumerators on the bot's stack are the call chain of the step that threw
                    System.Console.Error.WriteLine("  step chain: " + string.Join(" <- ", _stack.Select(en => System.Text.RegularExpressions.Regex.Replace(en.GetType().Name, "^<|>d__\\d+$", ""))));
                    try { AnodyneSharp.Drawing.SpriteDrawer.SaveRender("/tmp/bot/fail.png"); } catch { }
                    DumpRing();
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
                    if (Environment.GetEnvironmentVariable("ANODYNE_KEYTRACE") is string kt && int.TryParse(kt, out int kfrom) && Frame >= kfrom && Frame < kfrom + int.Parse(Environment.GetEnvironmentVariable("ANODYNE_KEYTRACE_LEN") ?? "60"))
                    {
                        System.Console.Error.WriteLine($"KEYS f{Frame} st={BotRunner.Status} me={Player?.Position} hp={AnodyneSharp.Registry.GlobalState.CUR_HEALTH} [{string.Join("+", frame)}] {BotScript.DebugEnemies()}");
                    }
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
            SwingAudit(held);
            RingRecord(held);
            WriteGbaFrame(held, _stack.Count == 0);
            if (_stack.Count == 0)
            {
                PrintSwingAudit();
                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_END_SNAP") is string endSnap && !string.IsNullOrEmpty(endSnap))
                {
                    SectionEnumerator.SnapshotNow(endSnap); // the game as the script left it: the start of the next part (dev)
                }
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
        private static int _secIndex;

        public static void CopyDirPublic(string from, string to) => CopyDir(from, to);

        /// <summary>Dev: snapshot the game now as <ANODYNE_BOT_SECSAVE>/<index>_<name> (token `<name>), so the later sections can be rerun from it.</summary>
        public static void SnapshotNow(string name)
        {
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_SECSAVE") is string secDir && Environment.GetEnvironmentVariable("ANODYNE_BOT_SAVEDIR") is string saveDir && BotRunner.Player != null)
            {
                GlobalState.checkpoint = new GlobalState.CheckPoint(GlobalState.CURRENT_MAP_NAME, BotRunner.Player.Position);
                GlobalState.SaveGame();
                CopyDir(saveDir, System.IO.Path.Combine(secDir, name));
            }
        }

        private static void CopyDir(string from, string to)
        {
            System.IO.Directory.CreateDirectory(to);
            foreach (var f in System.IO.Directory.GetFiles(from))
            {
                System.IO.File.Copy(f, System.IO.Path.Combine(to, System.IO.Path.GetFileName(f)), true);
            }
            foreach (var d in System.IO.Directory.GetDirectories(from))
            {
                CopyDir(d, System.IO.Path.Combine(to, System.IO.Path.GetFileName(d)));
            }
        }

        public bool MoveNext()
        {
            if (_inner == null && Environment.GetEnvironmentVariable("ANODYNE_BOT_SECSAVE") is string secDir && Environment.GetEnvironmentVariable("ANODYNE_BOT_SAVEDIR") is string saveDir)
            {
                // Dev: snapshot a save at the start of every section so a failing section can be tried again from there (scene.sh).
                try
                {
                    GlobalState.checkpoint = new GlobalState.CheckPoint(GlobalState.CURRENT_MAP_NAME, BotRunner.Player.Position);
                    GlobalState.SaveGame();
                    var dest = System.IO.Path.Combine(secDir, $"{_secIndex++:00}_{_name.Replace(' ', '_')}");
                    CopyDir(saveDir, dest);
                }
                catch (Exception e)
                {
                    System.Console.Error.WriteLine("secsave failed: " + e.Message);
                }
            }
            if (_inner == null && BotRunner.StopBefore == _name)
            {
                BotRunner.JumpToKeys = true;
                Current = Array.Empty<KeyFunctions>();
                return true;
            }
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

        /// <summary>
        /// Keys for one frame. A swing pressed in the same frame as a direction goes in the OLD facing (the turn happens after the attack
        /// is read), so "turn and swing" must be two frames: here a direction+Accept pair is sent as the turn alone when we do not face
        /// that way yet, and as the swing alone when we do.
        /// </summary>
        protected static KeyFunctions[] K(params KeyFunctions[] k)
        {
            if (k.Length == 2 && Player != null)
            {
                int ai = Array.IndexOf(k, KeyFunctions.Accept);
                if (ai >= 0)
                {
                    var dir = k[1 - ai];
                    var want = dir == KeyFunctions.Up ? Facing.UP : dir == KeyFunctions.Down ? Facing.DOWN : dir == KeyFunctions.Left ? Facing.LEFT : dir == KeyFunctions.Right ? Facing.RIGHT : (Facing?)null;
                    if (want.HasValue)
                    {
                        return Player.facing == want.Value ? new[] { KeyFunctions.Accept } : new[] { dir };
                    }
                }
            }
            return k;
        }

        protected static IEnumerable<object> Wait(int frames)
        {
            for (int i = 0; i < frames; i++)
            {
                BotRunner.NotStuck();
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
        /// <summary>On a dust raft over water (a fight there would drop the raft); walking the shore between two rafts is not.</summary>
        private static bool OnWater() => InRaft && OnConveyor(BotNav.TileOf(Pos));

        protected static IEnumerable<object> GoToWhere(Func<Point, bool> isGoal, string what, int maxReplans = 6, Point? forced = null)
        {
            int shooterFights = 0, slimeFights = 0, annoyerFights = 0;
            var pluggedGrids = new HashSet<Point>(); // screens whose beams crossing this route were already plugged (or tried)
            HashSet<Point> avoidStationary = null; // tiles round stationary enemies, once one blocked the way
            int blockerHandled = 0;
            // Beams only become known on arriving at their screen: a route that was planned to end inside one is re-planned to end beside it.
            HashSet<Point> goalLaneBan = null;
            var laneReplanned = new HashSet<Point>();
            var userGoal = isGoal;
            isGoal = t => userGoal(t) && (goalLaneBan == null || !goalLaneBan.Contains(t));
            string startMap = Map;
            for (int attempt = 0; attempt <= maxReplans; attempt++)
            {
                replan:
                if (Map != startMap)
                {
                    yield break; // a door took us to another map part-way: the goal was a tile of the old one, we are through
                }
                if (avoidStationary != null)
                {
                    BotNav.Avoid = avoidStationary;
                }
                var path = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced);
                BotNav.Avoid = null;
                if (!InRaft && avoidStationary == null && Enemies().Any(e => e.velocity == Vector2.Zero))
                {
                    // Keep a berth round enemies that stand still (touching one hurts): a route that does is used if there is one.
                    List<Point> wide = null;
                    foreach (int berthMargin in new[] { 12, 6, 1 })
                    {
                        var berth = BotNav.NearEnemies(berthMargin, null, stationaryOnly: true);
                        berth.Remove(BotNav.TileOf(Pos));
                        BotNav.Avoid = berth;
                        wide = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced, strict: true);
                        BotNav.Avoid = null;
                        if (wide != null && (path == null || wide.Count <= path.Count + 12))
                        {
                            break; // a berth that does not mean a huge detour
                        }
                        wide = null;
                    }
                    if (wide != null && (path == null || wide.Count <= path.Count + 12))
                    {
                        path = wide;
                    }
                }
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
                    if (path == null && BotNav.Blocked.Count > 0)
                    {
                        // Tiles learned as "blocked" (we stood still on them: dodging, waiting out bullets) may be what cuts the way: forget them, retry.
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine($"  no path: forgetting the blocked tiles {string.Join(" ", BotNav.Blocked.Select(q => $"{q.X},{q.Y}"))} and trying again");
                        }
                        BotNav.Blocked.Clear();
                        goto replan;
                    }
                    if (path == null)
                    {
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine($"  NOPATH at {Pos} grid {Grid}: " + string.Join(" ", BotRunner.Entities().Where(e => e.exists && (e.GetType().Name is "Dust" or "PewLaser" or "Shieldy" or "Gate" or "Boulder")).Select(e => $"{e.GetType().Name}@{(int)e.Position.X},{(int)e.Position.Y}")));
                            yield return Exec($"&{Grid.X},{Grid.Y},{Grid.X},{Grid.Y}");
                        }
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine("  BLOCKED tiles: " + string.Join(" ", BotNav.Blocked.Select(q => $"{q.X},{q.Y}")));
                        }
                        throw new InvalidOperationException($"bot: no path from {BotNav.TileOf(Pos)} to {what} on {Map}");
                    }
                }
                if (path != null && path.Count > (GlobalState.inventory.CanJump ? 10 : 30) && !InRaft)
                {
                    // A long way round on dry land: a raft across the river may be far shorter.
                    BotNav.AllowWater = true;
                    BotNav.AllowJump = GlobalState.inventory.CanJump;
                    BotNav.Avoid = avoidStationary;
                    var shortcut = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced);
                    BotNav.Avoid = null;
                    BotNav.AllowWater = false;
                    BotNav.AllowJump = false;
                    if (shortcut != null && GlobalState.inventory.CanJump && shortcut.Count + 6 < path.Count && shortcut.Any(t => BotNav.IsSpike(t)) && !shortcut.Any(t => BotNav.IsConveyor(t)))
                    {
                        // With the boots the spikes are jumped: that beats a long way round (often out of the screen and back).
                        path = shortcut;
                        jumping = true;
                    }
                    else if (shortcut != null && shortcut.Count + 20 < path.Count && !shortcut.Any(t => BotNav.IsSpike(t)) && shortcut.Count(t => BotNav.IsConveyor(t)) <= 2)
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
                if (!BotRunner.NoAutoPlug && !pluggedGrids.Contains(Grid) && !Plugging && !InRaft && GlobalState.inventory.HasBroom && path.Count > 0 && HaveLooseDust())
                {
                    // A route through a laser beam with dust lying about: plug the emitter instead of racing (or waiting for) its shots.
                    var crossing = EmittersCrossing(path);
                    if (crossing.Count > 0)
                    {
                        pluggedGrids.Add(Grid);
                        Plugging = true; // BlockLasers walks with GoTo: it must not start plugging again from inside
                        try
                        {
                            yield return BlockLasers(e => crossing.Contains(e));
                        }
                        finally
                        {
                            Plugging = false;
                        }
                        goto replan;
                    }
                }
                if (!PassingTrap && path.Count > 0 && SightTrapOnPath(path) is (Entity trap, Point side, int ti))
                {
                    // An idle dash trap sitting in a one-tile way: bait it from the side, then slip past while it is coming back.
                    PassingTrap = true;
                    try
                    {
                        yield return PassSightTrap(trap, path, ti);
                    }
                    finally
                    {
                        PassingTrap = false;
                    }
                    goto replan;
                }
                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    string via = string.Join(">", path.Take(14).Select(p => $"{p.X},{p.Y}"));
                    System.Console.Error.WriteLine($"  path to {what}: {path.Count} tiles from {BotNav.TileOf(Pos)} via {via}");
                }
                bool stuck = false;
                var gridAtStart = Grid;
                for (int pi = 0; pi < path.Count; pi++)
                {
                    if (Map != startMap)
                    {
                        yield break;
                    }
                    var tile = path[pi];
                    if (Grid != gridAtStart && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null && !laneReplanned.Contains(Grid))
                    {
                        var dl = BotNav.LaneTiles();
                        System.Console.Error.WriteLine($"  arrived on {Grid}: {dl.Count} beam tiles, route ends at {(path.Count > 0 ? path[path.Count - 1].ToString() : "-")} {(path.Count > 0 && dl.Contains(path[path.Count - 1]) ? "(in a beam)" : "")} trans={GlobalState.ScreenTransition}");
                    }
                    if (Grid != gridAtStart && !laneReplanned.Contains(Grid) && !InRaft && !GlobalState.ScreenTransition && path.Count > 0 && BotNav.LaneTiles() is { Count: > 0 } newLanes
                        && (newLanes.Contains(path[path.Count - 1]) || path.Skip(pi).Count(newLanes.Contains) >= 3))
                    {
                        laneReplanned.Add(Grid);
                        goalLaneBan = new HashSet<Point>(newLanes);
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine($"  new screen {Grid}: the route would run along a beam: re-planning with the beams known");
                        }
                        goto replan;
                    }
                    if (Grid != gridAtStart && !BotRunner.NoAutoPlug && !pluggedGrids.Contains(Grid) && !Plugging && !InRaft && !GlobalState.ScreenTransition
                        && GlobalState.inventory.HasBroom && HaveLooseDust())
                    {
                        // Arrived on another screen of a long route: its beams are only known now, so plug the ones the rest of the route crosses.
                        gridAtStart = Grid;
                        var crossing2 = EmittersCrossing(path.Skip(pi).ToList());
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine($"  new screen {Grid}: {crossing2.Count} emitter(s) cross the route");
                        }
                        if (crossing2.Count > 0)
                        {
                            pluggedGrids.Add(Grid);
                            Plugging = true;
                            try
                            {
                                yield return BlockLasers(e => crossing2.Contains(e));
                            }
                            finally
                            {
                                Plugging = false;
                            }
                            goto replan;
                        }
                    }
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
                        // Steam burns in the air too: a jump whose spikes or landing lie in a Red Cave jet waits for the jet to have just gone down.
                        var jumpTiles = path.GetRange(pi, Math.Min(pj + 1, path.Count) - pi);
                        jumpTiles.Add(from);
                        for (int w = 0; w < 400 && SteamUnsafeOver(jumpTiles); w++)
                        {
                            BotRunner.Status = "jump over the spikes: wait for the steam to go down";
                            BotRunner.NotStuck();
                            yield return K();
                        }
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine($"  jump {string.Join(">", jumpTiles.Select(q => $"{q.X},{q.Y}"))} land {land.X},{land.Y} enemies near landing: {BotNav.NearEnemies(10).Contains(land)}");
                        }
                        for (int w = 0; w < 120 && GlobalState.ScreenTransition; w++)
                        {
                            yield return K(); // keys do nothing while the screen scrolls: a jump pressed now never happens
                        }
                        if (BotNav.IsHole(tile))
                        {
                            // A pit: the jump only carries ~27px, so take off from the edge of the tile before it (centre 3px short of the pit),
                            // and not into a fireball or a bullet coming down where we land.
                            var dv = jk == KeyFunctions.Right ? new Vector2(1, 0) : jk == KeyFunctions.Left ? new Vector2(-1, 0) : jk == KeyFunctions.Down ? new Vector2(0, 1) : new Vector2(0, -1);
                            var edge = BotNav.CentreOf(from) + dv * 5f;
                            for (int w = 0; w < 30 && ((edge - Pos) * dv).Length() > 0.8f && Vector2.Dot(edge - Pos, dv) > 0; w++)
                            {
                                BotRunner.Status = "jump the pit: run-up";
                                yield return K(jk);
                            }
                            var landC = BotNav.CentreOf(land);
                            for (int w = 0; w < 300; w++)
                            {
                                var th = PredictThreats(null);
                                var pbx = Player.Hitbox;
                                bool landSafe = true;
                                foreach (var t in th)
                                {
                                    for (int k = 20; k <= 50 && landSafe; k++)
                                    {
                                        if (t.At[k] is Rectangle r)
                                        {
                                            var me2 = new Rectangle((int)(landC.X - pbx.Width / 2f), (int)(landC.Y - pbx.Height / 2f), pbx.Width, pbx.Height);
                                            r.Inflate(2, 2);
                                            landSafe = !me2.Intersects(r);
                                        }
                                    }
                                }
                                if (landSafe)
                                {
                                    break;
                                }
                                // Staying is no good either (a spike roller coming along the edge): go now, the air is safe from most of it.
                                bool hereSafe = true;
                                foreach (var t in th)
                                {
                                    for (int k = 1; k <= 15 && hereSafe; k++)
                                    {
                                        if (t.At[k] is Rectangle r)
                                        {
                                            r.Inflate(2, 2);
                                            hereSafe = !pbx.Intersects(r);
                                        }
                                    }
                                }
                                if (!hereSafe)
                                {
                                    break;
                                }
                                BotRunner.Status = "jump the pit: wait for the landing to clear";
                                BotRunner.NotStuck();
                                yield return K();
                            }
                        }
                        BotRunner.Status = "jump over the spikes";
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine($"  jump take-off at {Pos} state={Player.state} key={jk}");
                        }
                        yield return K(jk, KeyFunctions.Cancel);
                        for (int j = 0; j < 90 && !(BotNav.TileOf(Pos) == land && Player.state != PlayerState.AIR); j++)
                        {
                            if (j > 4 && Player.state != PlayerState.AIR && BotNav.IsJumpable(BotNav.TileOf(Pos)) == false && (land.X - BotNav.TileOf(Pos).X) * (jk == KeyFunctions.Left ? -1 : jk == KeyFunctions.Right ? 1 : 0) + (land.Y - BotNav.TileOf(Pos).Y) * (jk == KeyFunctions.Up ? -1 : jk == KeyFunctions.Down ? 1 : 0) < 0)
                            {
                                break; // landed past the landing tile: stop pushing on (the walker steps back with its dodging), into whatever is beyond
                            }
                            if (j > 24 && j < 60 && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                            {
                                System.Console.Error.WriteLine($"    jump f+{j} at {Pos} hb={Player.Hitbox} state={Player.state} trans={GlobalState.ScreenTransition}");
                            }
                            yield return K(jk);
                        }
                        pi = pj - 1;
                        continue;
                    }
                    var c = BotNav.CentreOf(tile);
                    Vector2 last = Pos;
                    int still = 0;
                    int edgePushes = 0;
                    int steamWaits = 0;
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
                        if (AutoFight && annoyerFights < 6 && !OnWater() && GlobalState.inventory.HasBroom && Enemies().Any(e => e.GetType().Name == "Annoyer" && (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - (Pos + new Vector2(5, 6))).Length() < 70))
                        {
                            // An awake Annoyer comes for us wherever we walk (and walking about only dances us into its swoop): the broom answers it.
                            annoyerFights++;
                            BotRunner.Status = "clear Annoyer(s) on the way";
                            yield return Fight(900, soft: true, only: e => e.GetType().Name == "Annoyer");
                            goto replan;
                        }
                        if (AutoFight && slimeFights < 8 && !OnWater() && GlobalState.inventory.HasBroom && SlimeNear(48) is Entity slimeOnWay)
                        {
                            // A slime drifting across the route hurts on contact: kill it from a safe gap first, then replan.
                            slimeFights++;
                            BotRunner.Status = "clear Slime on the way";
                            yield return Fight(900, soft: true, only: e => e == slimeOnWay);
                            goto replan;
                        }
                        if (DodgeEnabled && AutoFight && shooterFights < 3 && !OnWater() && GlobalState.inventory.HasBroom && GlobalState.CUR_HEALTH >= TankMinHp && ShooterNear(80) is Entity shooter)
                        {
                            // A frog/plant on the way would keep us dancing in and out of its bubbles: kill it (2 hits), pick up the hearts, replan.
                            shooterFights++;
                            BotRunner.Status = $"clear {shooter.GetType().Name} on the way";
                            yield return Fight(900, soft: true, only: e => e == shooter);
                            goto replan;
                        }
                        still = (p == last) ? still + 1 : 0;
                        last = p;
                        if (SteamWait && steamWaits < 600)
                        {
                            // held back for a steam jet's cycle on purpose: neither this tile's time nor the stuck clock runs
                            steamWaits++;
                            still = 0;
                            i--;
                            BotRunner.NotStuck();
                        }
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
                        if (Math.Abs(dy) > 2f && Math.Abs(dx) > 2f && NearSteamJet(24) && (pi == 0 ? BotNav.TileOf(Pos) : path[pi - 1]).Y != tile.Y)
                        {
                            dir = dy < 0 ? KeyFunctions.Up : KeyFunctions.Down; // crossing a steam jet: straight over it, the sidestep afterwards
                        }
                        var safe = SafeMove(dir, null, Math.Abs(dx) > 2f ? Math.Abs(dx) : Math.Abs(dy), stopForBlockers: true, look: path.Skip(pi).Take(4).Select(BotNav.CentreOf).ToList());
                        if (BlockedBy != null && blockerHandled < 6)
                        {
                            // A stationary enemy is in the way. First try a route that keeps clear of it, else kill it, then replan.
                            var blocker = BlockedBy;
                            blockerHandled++;
                            var avoid = BotNav.NearEnemies(12);
                            BotNav.Avoid = avoid;
                            var detour = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced);
                            BotNav.Avoid = null;
                            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                            {
                                System.Console.Error.WriteLine($"  blocked by stationary {blocker.GetType().Name}@{blocker.Hitbox.X},{blocker.Hitbox.Y} at {Pos}: " + (detour != null ? $"detour of {detour.Count} tiles via {string.Join(">", detour.Take(8).Select(q => $"{q.X},{q.Y}"))} from tile {BotNav.TileOf(Pos)} (dir {dir})" : "no detour, fighting it"));
                            }
                            if (detour != null && detour.Count > 0 && (detour.Count <= path.Count + 20 || !GlobalState.inventory.HasBroom))
                            {
                                avoidStationary = avoid;
                            }
                            else if (GlobalState.inventory.HasBroom && blocker.GetType().Name == "Shieldy")
                            {
                                // A shield-bearer in the way: push it along the way we are going (left, right or up), then carry on.
                                yield return PushBlocker(blocker, dir);
                            }
                            else if (GlobalState.inventory.HasBroom)
                            {
                                yield return Fight(1200, soft: true, only: e => e == blocker);
                            }
                            goto replan;
                        }
                        if (BlockedBy != null)
                        {
                            safe = K(dir); // every way of dealing with the blocker has been tried: take the chance
                        }
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
        protected static IEnumerable<object> WalkTo(float x, float y, float tol = 2f, bool yFirst = false, int timeout = 900, bool diagonal = false)
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
                if (diagonal && !xDone && !yDone)
                {
                    // both axes at once: full speed on each (the quickest way through water that drowns you after 3s)
                    yield return K(dx < 0 ? KeyFunctions.Left : KeyFunctions.Right, dy < 0 ? KeyFunctions.Up : KeyFunctions.Down);
                    continue;
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
        public static readonly string[] PartNames = { "nexus", "roadway", "overworld1", "temple", "temple1", "forest2", "fields", "beach", "beach2", "beach3", "redsea", "redcave", "redcave2", "redcave3", "redcave4", "cliff", "crowd", "windmill", "temple2" };

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
            "beach2" => new Beach2(),
            "beach3" => new Beach3(),
            "redsea" => new RedSea(),
            "redcave" => new RedCave(),
            "redcave2" => new RedCaveRightWing(),
            "redcave3" => new RedCaveLeftWing(),
            "redcave4" => new RedCaveTop(),
            "cliff" => new ForestCliff(),
            "crowd" => new Crowd(),
            "windmill" => new Windmill(),
            "temple2" => new Temple2(),
            _ => throw new ArgumentException(name)
        };

        private static IEnumerable<object> OnlyPart(string name)
        {
            yield return StartToNexus.Boot(); // continue the save the game was started with
            BotRunner.ChipLeft = BotRunner.ChipPerPart;
            yield return Part(name).Run();
        }

        /// <summary>Plays the route from the title screen up to and including the named part.</summary>
        public static IEnumerable<object> Through(string last)
        {
            foreach (var n in PartNames)
            {
                BotRunner.ChipLeft = BotRunner.ChipPerPart;
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
                case "keysonly": return new KeysOnlyScript().Run();
                default:
                    if (Array.IndexOf(PartNames, name.ToLowerInvariant()) >= 0)
                    {
                        // ANODYNE_BOT_ONLY=1: just this part, for a game that was loaded from the save the previous part ended on (dev: no replay of the route so far)
                        return Environment.GetEnvironmentVariable("ANODYNE_BOT_ONLY") == "1" ? OnlyPart(name.ToLowerInvariant()) : Through(name);
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
    public class KeysOnlyScript : BotScript
    {
        public override IEnumerable<object> Run()
        {
            yield return Exec(Environment.GetEnvironmentVariable("ANODYNE_BOT_KEYS") ?? "");
            yield return Wait(30);
        }
    }

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
