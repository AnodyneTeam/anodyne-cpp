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
        /// <summary>Set while the bot walks to a checkpoint to heal: the hearts owed are on their way back.</summary>
        public static bool Healing;
        /// <summary>Frames since the oldest hit not yet won back (-1 when none is owed).</summary>
        public static long DebtAge => DebtBaseline >= 0 ? _botFrame - _debtSince : -1;
        /// <summary>Hearts owed (0 when none).</summary>
        public static int DebtMissing => DebtBaseline >= 0 ? Math.Max(0, Math.Min(DebtBaseline, GlobalState.MAX_HEALTH) - GlobalState.CUR_HEALTH) : 0;
        /// <summary>Hearts per part that may stay lost after the recovery window (ANODYNE_BOT_CHIP, default 2, and a quarter of the hearts once
        /// that is more: 3 of 13 late on, where checkpoints are few and far between); reset at the start of each part and section, and once back at full health.</summary>
        private static readonly int ChipBase = int.TryParse(Environment.GetEnvironmentVariable("ANODYNE_BOT_CHIP"), out int cl) ? cl : 2;
        public static int ChipLeft = ChipBase;
        public static int ChipPerPart => Math.Max(ChipBase, GlobalState.MAX_HEALTH / 4);
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

        /// <summary>Frames kept for /tmp/bot/fail_ring.txt (ANODYNE_RING: more to see a hit that came long before the failure).</summary>
        private static readonly int RingSize = int.TryParse(Environment.GetEnvironmentVariable("ANODYNE_RING"), out int rs) ? rs : 240;

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
                if (_ring.Count > RingSize)
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
            try { System.Console.Error.Write(BotNav.RoomText()); } catch { }
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
                if (!string.IsNullOrEmpty(GlobalState.Dialogue) || PlayBusy || Healing)
                {
                    _debtSince++; // the game is paused for text (no time passes in which a heart could be won back), or we are off to a checkpoint for them
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
            if (DebtBaseline < 0 && hp >= GlobalState.MAX_HEALTH && ChipLeft < ChipPerPart)
            {
                ChipLeft = ChipPerPart; // (the hearts let go were won back after all, at a checkpoint later on: nothing is missing any more)
            }
            _hpSeen = hp;
            if ((why != null && why != _whyShown) || (_allowDamage && _hpSeenAllow >= 0 && hp < _hpSeenAllow))
            {
                _whyShown = why; // (with damage allowed the run goes on: say it once, not every frame)
                string near = "";
                try
                {
                    near = string.Join(" ", Entities().Where(e => e.exists && Math.Abs(e.Position.X - Player.Position.X) < 90 && Math.Abs(e.Position.Y - Player.Position.Y) < 90).Select(e => $"{e.GetType().Name}@{(int)e.Position.X},{(int)e.Position.Y}"));
                }
                catch { }
                System.Console.Error.WriteLine($"  HIT near: {near}");
                try { System.Console.Error.Write(BotNav.RoomText()); } catch { }
            }
            if (_allowDamage) { _hpSeenAllow = hp; return; }
            _whyShown = null;
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
        private static string _whyShown;
        private static double _winMs; private static int _winN; private static int _slowReports;

        private static readonly Dictionary<string, (double ms, int n)> _prof = new();

        /// <summary>Adds the time since t0 (a Stopwatch timestamp) to this frame's tally for name (shown with a slow frame).</summary>
        /// <summary>ANODYNE_PROF=1: time the planners per frame and show the tally with each slow frame.</summary>
        public static readonly bool ProfOn = Environment.GetEnvironmentVariable("ANODYNE_PROF") != null;

        private static readonly Dictionary<string, double> _winByStatus = new();

        public static void Prof(string name, long t0)
        {
            double ms = (System.Diagnostics.Stopwatch.GetTimestamp() - t0) * 1000.0 / System.Diagnostics.Stopwatch.Frequency;
            _prof.TryGetValue(name, out var v);
            _prof[name] = (v.ms + ms, v.n + 1);
        }

        public static Dictionary<Keys, KeyInput.InputState> NextFrame()
        {
            _prof.Clear();
            var sw = System.Diagnostics.Stopwatch.StartNew();
            AnodyneSharp.Recording.SpaceTime.CallsThisFrame = 0;
            AnodyneSharp.Recording.SpaceTime.NodesThisFrame = 0;
            var result = NextFrameCore();
            double ms = sw.Elapsed.TotalMilliseconds;
            _winMs += ms;
            var stKey = Status ?? "";
            int cut = stKey.IndexOf('(');
            stKey = cut > 0 ? stKey.Substring(0, cut) : stKey; // (the doing, not where)
            _winByStatus[stKey] = _winByStatus.GetValueOrDefault(stKey) + ms;
            if (ms > SlowFrameMs && _slowReports++ < 40)
            {
                System.Console.Error.WriteLine($"SLOW BOT FRAME {ms:0.0}ms at f{_botFrame} ({Status}): spacetime calls {AnodyneSharp.Recording.SpaceTime.CallsThisFrame}, nodes {AnodyneSharp.Recording.SpaceTime.NodesThisFrame}; "
                    + string.Join(" ", _prof.OrderByDescending(kv => kv.Value.ms).Select(kv => $"{kv.Key} {kv.Value.ms:0.0}ms/{kv.Value.n}")));
            }
            if (++_winN >= 300)
            {
                double avg = _winMs / _winN;
                if (avg > MaxAvgMs)
                {
                    System.Console.Error.WriteLine($"BOT FAILED: the bot itself is slow: {avg:0.0}ms per frame over the last {_winN} frames at f{_botFrame} ({Status}); spacetime calls/nodes in the last frame {AnodyneSharp.Recording.SpaceTime.CallsThisFrame}/{AnodyneSharp.Recording.SpaceTime.NodesThisFrame}");
                    System.Console.Error.WriteLine("  slow window by status: " + string.Join("; ", _winByStatus.OrderByDescending(kv => kv.Value).Take(6).Select(kv => $"{kv.Key.Trim()} {kv.Value:0}ms")));
                    DumpRing();
                    _trace?.Flush();
                    Environment.Exit(4);
                }
                _winMs = 0; _winN = 0;
                _winByStatus.Clear();
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
            held = BotScript.SpikeGuard(held);
            held = BotScript.SwingGuard(held);
            held = BotScript.DoorGuard(held);
            held = BotScript.FightEdgeGuard(held);
            held = BotScript.BulletHop(held);
            held = BotScript.ContactEscape(held);
            held = BotScript.SteamBrace(held);
            if (Player != null && Player.reversed)
            {
                // a Gas Guy cloud swaps the d-pad for a moment: press the mirrored keys so the scripts keep meaning the way they want to go
                held = held.Select(k => k switch
                {
                    KeyFunctions.Up => KeyFunctions.Down,
                    KeyFunctions.Down => KeyFunctions.Up,
                    KeyFunctions.Left => KeyFunctions.Right,
                    KeyFunctions.Right => KeyFunctions.Left,
                    _ => k,
                }).ToArray();
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
        /// Any walk (a fight's chase, a dodge) about to step off the floor onto ground spikes jumps them instead, if the far side is
        /// clear floor ~27px on.
        /// </summary>
        internal static KeyFunctions[] SpikeGuard(KeyFunctions[] held)
        {
            if (Player == null || Player.state != PlayerState.GROUND || !GlobalState.inventory.CanJump || held.Contains(KeyFunctions.Cancel) || GlobalState.ScreenTransition || Environment.GetEnvironmentVariable("ANODYNE_NO_SPIKEGUARD") != null)
            {
                return held;
            }
            float dx = (held.Contains(KeyFunctions.Right) ? 1 : 0) - (held.Contains(KeyFunctions.Left) ? 1 : 0);
            float dy = (held.Contains(KeyFunctions.Down) ? 1 : 0) - (held.Contains(KeyFunctions.Up) ? 1 : 0);
            if (dx == 0 && dy == 0)
            {
                return held;
            }
            var hb = Player.Hitbox;
            // (a spike tile only hurts in its 5x6 core, see Map: brushing its edge, as a route round it does, is fine)
            bool OnSpikes(Rectangle r)
            {
                for (int tx = (int)Math.Floor(r.Left / 16f); tx <= (int)Math.Floor((r.Right - 1) / 16f); tx++)
                {
                    for (int ty = (int)Math.Floor(r.Top / 16f); ty <= (int)Math.Floor((r.Bottom - 1) / 16f); ty++)
                    {
                        if (BotNav.IsSpike(new Point(tx, ty)) && new Rectangle(tx * 16 + 6, ty * 16 + 6, 5, 6).Intersects(r))
                        {
                            return true;
                        }
                    }
                }
                return false;
            }
            bool Floor(Rectangle r) => new[] { new Point(r.Left, r.Top), new Point(r.Right - 1, r.Top), new Point(r.Left, r.Bottom - 1), new Point(r.Right - 1, r.Bottom - 1) }
                .All(c => BotNav.Walkable(new Point((int)Math.Floor(c.X / 16f), (int)Math.Floor(c.Y / 16f))));
            var next = hb;
            next.Offset((int)(dx * 2), (int)(dy * 2));
            if (OnSpikes(hb) || !OnSpikes(next))
            {
                return held;
            }
            var land = hb;
            land.Offset((int)(dx * 27), (int)(dy * 27));
            if (OnSpikes(land) || !Floor(land))
            {
                // No clear floor past them: walking on just costs a heart. Keep only the way(s) that stay off them (a chase or a dodge
                // will find another).
                var keep = held.ToList();
                var nx = hb;
                nx.Offset((int)(dx * 2), 0);
                if (dx != 0 && OnSpikes(nx))
                {
                    keep.Remove(dx > 0 ? KeyFunctions.Right : KeyFunctions.Left);
                }
                var ny = hb;
                ny.Offset(0, (int)(dy * 2));
                if (dy != 0 && OnSpikes(ny))
                {
                    keep.Remove(dy > 0 ? KeyFunctions.Down : KeyFunctions.Up);
                }
                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null && keep.Count != held.Length)
                {
                    System.Console.Error.WriteLine($"  spike guard f{BotRunner.Frame}: no floor past the spikes ahead at {hb}: not stepping on ({BotRunner.Status})");
                }
                return keep.ToArray();
            }
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
            {
                System.Console.Error.WriteLine($"  spike guard f{BotRunner.Frame}: jump the spikes ahead at {hb} ({BotRunner.Status})");
            }
            return held.Append(KeyFunctions.Cancel).ToArray();
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


        /// <summary>
        /// A free jump from tile a to tile b (BotNav.IsLeap: stepping stones in a pit, diagonals included). Picks a take-off spot on a where the
        /// box stands clear of every pit's core and from which a jump with the keys towards b comes down clear too (for a carry a few px short
        /// or long), creeps there, stops, and jumps holding those keys until it lands.
        /// </summary>
        /// <summary>Does this screen's stretch of the route (from step pi) pass over or beside a fire pillar? Then it is walked to their clock.</summary>
        private static bool RouteRunsByPillars(List<Point> path, int pi)
        {
            var pillars = BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Enemy.Circus.FirePillar).Select(e => e.Hitbox).ToList();
            if (pillars.Count == 0)
            {
                return false;
            }
            var scr = Grid;
            for (int k = Math.Max(0, pi); k < path.Count && path[k].X / 10 == scr.X && path[k].Y / 10 == scr.Y; k++)
            {
                var tb = new Rectangle(path[k].X * 16 - 4, path[k].Y * 16 - 4, 24, 24);
                if (pillars.Any(r => r.Intersects(tb)))
                {
                    return true;
                }
            }
            return false;
        }

        /// <summary>Does this screen's stretch of the route (from step pi) cross the line a bouncing dash trap sweeps? Then it is walked to its clock.</summary>
        private static bool RouteCrossesLane(Entity trap, List<Point> path, int pi)
        {
            var r = trap.Hitbox;
            var scr = Grid;
            var lane = trap.velocity.X != 0 ? new Rectangle(scr.X * 160, r.Top - 2, 160, r.Height + 4) : new Rectangle(r.Left - 2, scr.Y * 160, r.Width + 4, 160);
            for (int k = Math.Max(0, pi); k < path.Count && path[k].X / 10 == scr.X && path[k].Y / 10 == scr.Y; k++)
            {
                if (lane.Intersects(new Rectangle(path[k].X * 16 + 3, path[k].Y * 16 + 2, 10, 12)))
                {
                    return true;
                }
            }
            return false;
        }

        protected static IEnumerable<object> Leap(Point a, Point b)
        {
            var spring = BotNav.SpringBetween(a, b);
            var landOn = spring ?? b; // off a spring: the jump only has to come down on the pad, it does the rest
            var finalTile = b;
            b = landOn;
            int sx = Math.Sign(b.X - a.X), sy = Math.Sign(b.Y - a.Y);
            var hb = Player.Hitbox;
            var off = new Vector2(hb.X - Pos.X, hb.Y - Pos.Y);
            Rectangle Box(float x, float y) => new((int)Math.Round(x), (int)Math.Round(y), hb.Width, hb.Height);
            bool Lands(Rectangle r) => spring != null ? r.Intersects(new Rectangle(b.X * 16 + 2, b.Y * 16 + 2, 12, 12)) // (touching the pad as we come down is enough)
                : BotNav.TileOf(new Vector2(r.X, r.Y) - off) == b && StandsSafe(r);
            Vector2? best = null;
            float bestScore = float.MaxValue;
            int sideFrames = BotNav.LeapFrames; // (a lopsided jump lets the side key go after this many frames)
            foreach (int m in BotNav.LeapSideFrames(a, b))
            {
                var c0 = BotNav.LeapCarry(a, b, m, 0);
                for (int y = a.Y * 16 - 6; y <= a.Y * 16 + 16 - hb.Height + 6; y++)
                {
                    for (int x = a.X * 16 - 6; x <= a.X * 16 + 16 - hb.Width + 6; x++)
                    {
                        if (!StandsSafe(Box(x, y)) || BotNav.TileOf(new Vector2(x, y) - off) != a)
                        {
                            continue;
                        }
                        bool ok = true;
                        foreach (float slack in new[] { -2f, 0f, 2f })
                        {
                            var c = BotNav.LeapCarry(a, b, m, slack);
                            ok &= Lands(Box(x + c.X, y + c.Y));
                        }
                        if (!ok)
                        {
                            continue;
                        }
                        // the middle of the safe spots: far from both edges of the margin (a px off either way still works), near where we are
                        float score = Math.Abs(x - hb.X) + Math.Abs(y - hb.Y);
                        foreach (var (ex, ey) in new[] { (-2, 0), (2, 0), (0, -2), (0, 2) })
                        {
                            if (!StandsSafe(Box(x + ex, y + ey)) || !Lands(Box(x + ex + c0.X, y + ey + c0.Y)))
                            {
                                score += 20;
                            }
                        }
                        if (score < bestScore)
                        {
                            bestScore = score;
                            best = new Vector2(x, y) - off;
                            sideFrames = m;
                        }
                    }
                }
                if (best != null)
                {
                    break;
                }
            }
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
            {
                System.Console.Error.WriteLine($"  leap {a.X},{a.Y} -> {b.X},{b.Y} take-off {(best == null ? "none" : best.Value.ToString())} from {Pos}");
            }
            if (best == null)
            {
                yield break;
            }
            var to = best.Value;
            BotRunner.Status = $"leap to {b.X},{b.Y}: creep to the take-off";
            for (int w = 0; w < 40; w++)
            {
                float ddx = to.X - Pos.X, ddy = to.Y - Pos.Y;
                if (Math.Abs(ddx) < 0.8f && Math.Abs(ddy) < 0.8f)
                {
                    break;
                }
                var keys = new List<KeyFunctions>();
                if (Math.Abs(ddx) >= 0.8f)
                {
                    keys.Add(ddx < 0 ? KeyFunctions.Left : KeyFunctions.Right);
                }
                if (Math.Abs(ddy) >= 0.8f)
                {
                    keys.Add(ddy < 0 ? KeyFunctions.Up : KeyFunctions.Down);
                }
                yield return K(keys.ToArray());
            }
            // Coming down beside an enemy (a dog pacing by where a spring drops us): wait at the take-off until the landing is clear.
            // (the jump cannot be steered off it once we are in the air)
            var landBox = new Rectangle(finalTile.X * 16 - 6, finalTile.Y * 16 - 6, 28, 28);
            for (int w = 0; w < 300; w++)
            {
                var lt = PredictThreats(null);
                int lf0 = spring != null ? 30 : 20, lf1 = spring != null ? 75 : 40;
                if (!lt.Any(t => Enumerable.Range(lf0, lf1 - lf0 + 1).Any(f => t.At[Math.Min(f, t.At.Length - 1)] is Rectangle r && r.Intersects(landBox))))
                {
                    break;
                }
                if (w == 0 && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    System.Console.Error.WriteLine($"  leap to {finalTile}: something will be by the landing, waiting at the take-off");
                }
                BotRunner.Status = $"leap to {b.X},{b.Y}: wait for the landing to clear";
                BotRunner.NotStuck();
                yield return StandSafe(null) is var ss && ss.Length > 0 ? ss : K();
                if (Math.Abs(to.X - Pos.X) >= 1f || Math.Abs(to.Y - Pos.Y) >= 1f)
                {
                    yield break; // (dodged off the take-off: let the walker plan again)
                }
            }
            yield return K(); // stand still: an axis without a key keeps whatever speed it had at take-off
            var jumpKeys = new List<KeyFunctions> { KeyFunctions.Cancel };
            if (sx != 0)
            {
                jumpKeys.Add(sx < 0 ? KeyFunctions.Left : KeyFunctions.Right);
            }
            if (sy != 0)
            {
                jumpKeys.Add(sy < 0 ? KeyFunctions.Up : KeyFunctions.Down);
            }
            BotRunner.Status = $"leap to {b.X},{b.Y}";
            yield return K(jumpKeys.ToArray());
            var air = jumpKeys.Skip(1).ToArray();
            // (a lopsided jump: the side key goes after sideFrames, and the main one carries us on alone)
            bool xMain = Math.Abs(b.X - a.X) > Math.Abs(b.Y - a.Y);
            var mainOnly = air.Where(k => xMain ? k is KeyFunctions.Left or KeyFunctions.Right : k is KeyFunctions.Up or KeyFunctions.Down).ToArray();
            for (int j = 0; j < 60 && (j < 3 || Player.state == PlayerState.AIR); j++)
            {
                yield return K(j + 1 >= sideFrames && sideFrames < BotNav.LeapFrames ? mainOnly : air);
            }
            for (int j = 0; j < 90 && spring != null && (j < 4 || Player.state == PlayerState.AUTO_JUMP); j++)
            {
                BotRunner.Status = $"spring down to {finalTile.X},{finalTile.Y}";
                yield return K(); // the pad's bounce carries us on
            }
            yield return K();
        }

        /// <summary>
        /// A long jump off a row of dash pads from a to b (straight): back up to before the pads, run along them holding their way (the
        /// speed lasts while the key is held, in the air too) and jump with our middle at a's far edge.
        /// </summary>
        protected static IEnumerable<object> DashLeap(Point a, Point b)
        {
            // (along the longer way; a landing one tile to the side is reached by holding that way too once in the air)
            var d = Math.Abs(b.X - a.X) > Math.Abs(b.Y - a.Y) ? new Point(Math.Sign(b.X - a.X), 0) : new Point(0, Math.Sign(b.Y - a.Y));
            var lat = new Point(d.X == 0 ? Math.Sign(b.X - a.X) : 0, d.Y == 0 ? Math.Sign(b.Y - a.Y) : 0);
            KeyFunctions? latKey = lat.X > 0 ? KeyFunctions.Right : lat.X < 0 ? KeyFunctions.Left : lat.Y > 0 ? KeyFunctions.Down : lat.Y < 0 ? KeyFunctions.Up : null;
            var key = d.X > 0 ? KeyFunctions.Right : d.X < 0 ? KeyFunctions.Left : d.Y > 0 ? KeyFunctions.Down : KeyFunctions.Up;
            // the run-up: the tile before the first pad of the row (or the first pad, against a wall)
            // (the take-off tile need not be the last pad: a fire pillar's tile may lie between, so back past the whole row)
            // (pads with a floor tile between them count too, as PadsBehind counts them: the second only doubles the dash if we hit the
            // first one on the way)
            int farPad = BotNav.DashPads().ContainsKey(a) ? 0 : -1, reachable = 0;
            for (int k = 1; k <= 4; k++)
            {
                var t = new Point(a.X - d.X * k, a.Y - d.Y * k);
                if (!BotNav.Walkable(t))
                {
                    break;
                }
                reachable = k;
                if (BotNav.DashPads().TryGetValue(t, out var pd) && pd == d)
                {
                    farPad = k;
                }
            }
            int back = farPad < 0 ? reachable : Math.Min(farPad + 1, reachable);
            var start = new Point(a.X - d.X * back, a.Y - d.Y * back);
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
            {
                System.Console.Error.WriteLine($"  dash leap {a.X},{a.Y} -> {b.X},{b.Y}: run up from {start} ({BotNav.PadsBehind(a, d)} pads)");
            }
            var sc = BotNav.CentreOf(start);
            BotRunner.Status = $"dash leap to {b.X},{b.Y}: back to the run-up {start}";
            bool BackClear()
            {
                // the walk back (at walking pace) meets nothing on the ground
                var threats = PredictThreats(null);
                var from0 = Player.Hitbox;
                int n = (int)(Vector2.Distance(sc, Pos) / WalkPxPerFrame) + 4;
                for (int f = 0; f <= n && f < 90; f++)
                {
                    float u = Math.Min(1f, (float)f / n);
                    var box = new Rectangle((int)(from0.X + (sc.X - Pos.X) * u), (int)(from0.Y + (sc.Y - Pos.Y) * u), from0.Width, from0.Height);
                    box.Inflate(2, 2);
                    if (threats.Any(t => t.At[Math.Min(f, t.At.Length - 1)] is Rectangle r && r.Intersects(box)))
                    {
                        return false;
                    }
                }
                return true;
            }
            bool HereThreatened()
            {
                var threats = PredictThreats(null);
                var box = Player.Hitbox;
                return threats.Any(t => Enumerable.Range(0, 24).Any(f => t.At[f] is Rectangle r && r.Intersects(box)));
            }
            for (int w = 0; w < 300 && Vector2.Distance(sc, Pos) > 1f && !BackClear() && !HereThreatened(); w++)
            {
                BotRunner.Status = $"dash leap to {b.X},{b.Y}: wait to back up to {start}";
                yield return K();
            }
            for (int w = 0; w < 120 && Vector2.Distance(sc, Pos) > 1f; w++)
            {
                float ddx = sc.X - Pos.X, ddy = sc.Y - Pos.Y;
                var keys = new List<KeyFunctions>();
                if (Math.Abs(ddx) >= 0.8f)
                {
                    keys.Add(ddx < 0 ? KeyFunctions.Left : KeyFunctions.Right);
                }
                if (Math.Abs(ddy) >= 0.8f)
                {
                    keys.Add(ddy < 0 ? KeyFunctions.Up : KeyFunctions.Down);
                }
                yield return K(keys.ToArray());
            }
            for (int w = 0; w < 65; w++)
            {
                yield return K(); // (a pad we stood on rests for a second before it pushes again)
            }
            // Wait for a run and jump that nothing meets: on the ground anything that hurts, in the air what hurts a jumper (a fire pillar
            // burning where we come down, say). The run is ~1.5x walking speed, the jump ~28 frames.
            var from = Player.Hitbox;
            var landBox = new Rectangle(b.X * 16 + 3, b.Y * 16 + 2, from.Width, from.Height);
            float runPx = Math.Abs((a.X - start.X) * 16 + (a.Y - start.Y) * 16) + 5;
            int runFrames = (int)(runPx / (WalkPxPerFrame * 1.5f)) + 2, total = runFrames + 30;
            bool Clear()
            {
                var threats = PredictThreats(null);
                for (int f = 0; f <= total && f < 90; f++)
                {
                    float u = Math.Min(1f, (float)f / total);
                    var box = new Rectangle((int)(from.X + (landBox.X - from.X) * u), (int)(from.Y + (landBox.Y - from.Y) * u), from.Width, from.Height);
                    box.Inflate(2, 2);
                    bool air = f > runFrames && f < total - 1;
                    // (the run seldom keeps the modelled pace: lining up, the pads' kick; so each spot must stay clear a while either side)
                    for (int k = Math.Max(0, f - 8); k <= Math.Min(Horizon, f + 16); k++)
                    {
                        if (threats.Any(t => (!air || t.HurtsInAir) && t.At[k] is Rectangle r && r.Intersects(box)))
                        {
                            return false;
                        }
                    }
                }
                return true;
            }
            // Taking off from the pad itself (no run-up: spikes or a wall behind it): it pushes the moment it is ready again, and that push
            // is lost unless its key is down that very frame. Stand at its back edge and press on just as it re-arms (each second).
            var padUnder = BotRunner.Entities().FirstOrDefault(e => e.exists && e is AnodyneSharp.Entities.Gadget.DashPad && BotNav.TileOf(e.Position + new Vector2(3, 2)) == BotNav.TileOf(Pos));
            if (padUnder != null && BotNav.DashPads().TryGetValue(BotNav.TileOf(Pos), out var padDir) && padDir == d)
            {
                var pt = BotNav.TileOf(Pos);
                var rear = BotNav.CentreOf(pt) - new Vector2(d.X, d.Y) * 4f;
                for (int w = 0; w < 30 && Vector2.Distance(rear, Pos) > 0.8f; w++)
                {
                    float rx = rear.X - Pos.X, ry = rear.Y - Pos.Y;
                    var rk = new List<KeyFunctions>();
                    if (Math.Abs(rx) > 0.8f) { rk.Add(rx < 0 ? KeyFunctions.Left : KeyFunctions.Right); }
                    if (Math.Abs(ry) > 0.8f) { rk.Add(ry < 0 ? KeyFunctions.Up : KeyFunctions.Down); }
                    yield return K(rk.ToArray());
                }
                var dtField = typeof(AnodyneSharp.Entities.Gadget.DashPad).GetField("disabled_t", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
                for (int w = 0; w < 600; w++)
                {
                    int armIn = padUnder.opacity >= 1f ? 0 : (int)((1f - (dtField?.GetValue(padUnder) is float dt ? dt : 0f)) * 60f);
                    if (armIn <= 2 && Clear())
                    {
                        break;
                    }
                    BotRunner.Status = $"dash leap to {b.X},{b.Y}: on the pad, wait for it to re-arm ({armIn}) and a clear run";
                    yield return K();
                }
            }
            for (int w = 0; w < 400 && !Clear(); w++)
            {
                BotRunner.Status = $"dash leap to {b.X},{b.Y}: wait for a clear run";
                yield return K();
            }
            BotRunner.Status = $"dash leap to {b.X},{b.Y}: run";
            bool vertical = d.Y != 0;
            float edge = vertical ? (d.Y < 0 ? a.Y * 16 + 3 : a.Y * 16 + 13) : (d.X < 0 ? a.X * 16 + 3 : a.X * 16 + 13);
            float lineup = vertical ? a.X * 16 + 8 : a.Y * 16 + 8;
            int latN = Math.Abs(vertical ? b.X - a.X : b.Y - a.Y);
            if (latN >= 2)
            {
                lineup += (vertical ? lat.X : lat.Y) * 6f; // (start from the lane's side edge: the drift alone falls short of two tiles)
            }
            float latGoal = vertical ? b.X * 16 + 8 : b.Y * 16 + 8;
            bool jumped = false;
            for (int w = 0; w < 120; w++)
            {
                var c = Player.Hitbox.Center;
                float along = vertical ? c.Y : c.X, across = vertical ? c.X : c.Y;
                var keys = new List<KeyFunctions> { key };
                // (over two: on the last tile, its pad already fired, edge over too; the dash keeps its pace with a side key down)
                bool onLast = BotNav.TileOf(new Vector2(c.X, c.Y)) == a;
                if ((jumped || latN >= 2 && onLast) && latKey is KeyFunctions lk && (latGoal - across) * (vertical ? lat.X : lat.Y) > 2f)
                {
                    keys.Add(lk); // drift over to the side landing
                }
                else if (!jumped && Math.Abs(across - lineup) > 1.5f)
                {
                    keys.Add(vertical ? (across < lineup ? KeyFunctions.Right : KeyFunctions.Left) : (across < lineup ? KeyFunctions.Down : KeyFunctions.Up));
                }
                if (!jumped && Player.state == PlayerState.GROUND && (vertical ? d.Y : d.X) * (along - edge) >= 0)
                {
                    jumped = true;
                    keys.Add(KeyFunctions.Cancel);
                    yield return K(keys.ToArray());
                    continue;
                }
                if (jumped && Player.state == PlayerState.GROUND)
                {
                    break;
                }
                if (Environment.GetEnvironmentVariable("ANODYNE_LEAP_TRACE") != null)
                {
                    var dsF = typeof(Player).GetField("dash_state", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
                    var pads = string.Join(",", BotRunner.Entities().Where(e => e.exists && e is AnodyneSharp.Entities.Gadget.DashPad && Vector2.Distance(e.Position, Pos) < 40).Select(e => $"{e.Position}:{e.opacity}"));
                    System.Console.Error.WriteLine($"    leap run w{w} c={c} state={Player.state} jumped={jumped} keys={string.Join("+", keys)} edge={edge} dash={dsF?.GetValue(Player)} pc={Player.Center} pads={pads}");
                }
                yield return K(keys.ToArray());
            }
            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
            {
                System.Console.Error.WriteLine($"  dash leap {a.X},{a.Y} -> {b.X},{b.Y}: {(jumped ? "jumped" : "never took off")}, down at {BotNav.TileOf(Pos)} {Pos}");
            }
            yield return K();
        }

        /// <summary>The nearest walkable tile next to ours, or reached over spike tiles (null if none within 6 steps).</summary>
        private static Point? SpikeExit()
        {
            var start = BotNav.TileOf(Pos);
            var seen = new HashSet<Point> { start };
            var q = new Queue<(Point t, int d)>();
            q.Enqueue((start, 0));
            while (q.Count > 0)
            {
                var (t, d) = q.Dequeue();
                if (t != start && BotNav.Walkable(t))
                {
                    // the closest floor tile of this ring to where we actually stand (a step the other way crosses the hole's middle)
                    var ring = q.Where(x => x.d == d && BotNav.Walkable(x.t)).Select(x => x.t).Append(t);
                    var me = Pos + new Vector2(5, 6);
                    return ring.OrderBy(x => (BotNav.CentreOf(x) + new Vector2(5, 6) - me).LengthSquared()).First();
                }
                if (d >= 6 || (t != start && !BotNav.IsSpike(t)) || BotNav.CurrentMap.GetTileClass(t).Item1 == AnodyneSharp.Entities.Touching.ANY)
                {
                    continue;
                }
                foreach (var o in new[] { new Point(1, 0), new Point(-1, 0), new Point(0, 1), new Point(0, -1) })
                {
                    var n = new Point(t.X + o.X, t.Y + o.Y);
                    if (seen.Add(n))
                    {
                        q.Enqueue((n, d + 1));
                    }
                }
            }
            return null;
        }

        protected static IEnumerable<object> GoToWhere(Func<Point, bool> isGoal, string what, int maxReplans = 6, Point? forced = null)
        {
            int shooterFights = 0, slimeFights = 0, annoyerFights = 0;
            var pluggedGrids = new HashSet<Point>(); // screens whose beams crossing this route were already plugged (or tried)
            HashSet<Point> avoidStationary = null; // tiles round stationary enemies, once one blocked the way
            int blockerHandled = 0;
            bool offSpikes = false; // walked off an unwalkable tile we found ourselves standing on (the planner never plans from one)
            int killGateFights = 0; // fights started to open a kill gate the route runs through
            int dodgeWaits = 0; // frames spent holding back for moving dangers (they do not count against a tile)
            int offRoute = 0, offRouteTotal = 0; // replans after being dodged off the route (recent, all told)
            int jumpLaneReplans = 0; // replans because a jump's lane was not clear and the danger was coming for us
            int gateWait = 0; // frames spent waiting for a shut gate on this screen to open (a kill only counts once the enemy is gone)
            // Beams only become known on arriving at their screen: a route that was planned to end inside one is re-planned to end beside it.
            HashSet<Point> goalLaneBan = null;
            var laneReplanned = new HashSet<Point>();
            var chaserGaveUp = new HashSet<Point>(); // screens whose Chasers the timed walk found no way past
            var userGoal = isGoal;
            isGoal = t => userGoal(t) && (goalLaneBan == null || !goalLaneBan.Contains(t));
            string startMap = Map;
            int replans = 0;
            for (int attempt = 0; attempt <= maxReplans; attempt++)
            {
                replan:
                if (++replans > 400)
                {
                    // (the gotos above re-plan without using up an attempt: a loop of them that never gets anywhere ends here)
                    throw new TimeoutException($"bot: GoTo {what} replanned {replans} times without getting there, at {Pos} on {Map}");
                }
                if (Map != startMap)
                {
                    yield break; // a door took us to another map part-way: the goal was a tile of the old one, we are through
                }
                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                {
                    System.Console.Error.WriteLine($"  replan {replans}: grid {Grid} pos {Pos} hitbox {Player.Hitbox} trans {GlobalState.ScreenTransition}");
                }
                if (!GlobalState.ScreenTransition && !GlobalState.ScreenHitbox.Contains(Player.Hitbox.Center))
                {
                    // Outside the screen the game says we are on, not scrolling: we came in over a gate (standing on one holds it open),
                    // stepped back off it and it shut behind us. Go back out across the edge, and count the gate as shut from now on.
                    var scr = GlobalState.ScreenHitbox;
                    var c = Player.Hitbox.Center;
                    var outKey = c.Y < scr.Top ? KeyFunctions.Up : c.Y >= scr.Bottom ? KeyFunctions.Down : c.X < scr.Left ? KeyFunctions.Left : KeyFunctions.Right;
                    var inStep = outKey == KeyFunctions.Up ? new Point(0, 1) : outKey == KeyFunctions.Down ? new Point(0, -1) : outKey == KeyFunctions.Left ? new Point(1, 0) : new Point(-1, 0);
                    var shut = BotNav.TileOf(Pos) + inStep;
                    BotNav.Blocked.Add(shut);
                    if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                    {
                        System.Console.Error.WriteLine($"  outside screen {Grid} at {Pos}: back out {outKey}, gate tile {shut} counted shut");
                    }
                    for (int w = 0; w < 60 && !GlobalState.ScreenTransition && !GlobalState.ScreenHitbox.Contains(Player.Hitbox.Center); w++)
                    {
                        yield return K(outKey);
                    }
                    for (int w = 0; w < 120 && GlobalState.ScreenTransition; w++)
                    {
                        yield return K();
                    }
                }
                if (avoidStationary != null)
                {
                    BotNav.Avoid = avoidStationary;
                }
                var path = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced);
                BotNav.Avoid = null;
                if (!InRaft && avoidStationary == null && (Enemies().Any(e => e.velocity == Vector2.Zero) || BotRunner.Entities().Any(e => e.exists && (BotNav.IsSuburbKiller(e) || DormantDustmaid(e)))))
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
                if (path != null && GlobalState.inventory.CanJump && BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced, strict: true) == null)
                {
                    // The walk is only there through a shut gate (taken as opening on arrival): a way round with jumps that needs no gate
                    // to open (off dash pads, over pits) is the real one.
                    BotNav.AllowJump = true;
                    var round = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced, strict: true);
                    BotNav.AllowJump = false;
                    if (System.Environment.GetEnvironmentVariable("ANODYNE_GATE_TRACE") != null)
                    {
                        System.Console.Error.WriteLine($"  strict-jump probe from {BotNav.TileOf(Pos)}: {(round == null ? "none" : string.Join(">", round.Select(t => $"{t.X},{t.Y}")))}");
                    }
                    if (round != null)
                    {
                        path = round;
                        jumping = true;
                    }
                }
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
                        if (wade != null && !(wade.Count(t => BotNav.IsConveyor(t)) <= 2 || BotNav.WadeOk(wade, BotNav.TileOf(Pos))))
                        {
                            // too long a wade the short way: round by the banks, a short stretch at a time
                            wade = BotNav.WadePath(BotNav.TileOf(Pos), isGoal, forced);
                            if (wade != null && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                            {
                                System.Console.Error.WriteLine($"  wade by the banks: {string.Join(">", wade.Select(q => $"{q.X},{q.Y}{(BotNav.IsConveyor(q) ? "~" : "")}"))}");
                            }
                        }
                        if (wade != null && (wade.Count(t => BotNav.IsConveyor(t)) <= 2 || BotNav.WadeOk(wade, BotNav.TileOf(Pos))))
                        {
                            path = wade;
                        }
                        else if (GlobalState.inventory.CanJump)
                        {
                            // wading and jumping both (a pit to jump on the way to a stream to wade down)
                            BotNav.AllowWater = true;
                            BotNav.AllowJump = true;
                            wade = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced);
                            BotNav.AllowWater = false;
                            BotNav.AllowJump = false;
                            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                            {
                                System.Console.Error.WriteLine($"  wade+jump: {(wade == null ? "none" : string.Join(">", wade.Select(q => $"{q.X},{q.Y}{(BotNav.IsConveyor(q) ? "~" + BotNav.FlowDir(q) : "")}")))} ok={(wade != null && BotNav.WadeOk(wade, BotNav.TileOf(Pos)))}");
                            }
                            if (wade != null && BotNav.WadeOk(wade, BotNav.TileOf(Pos)))
                            {
                                path = wade;
                                jumping = true;
                            }
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
                    if (path == null && !offSpikes && !BotNav.Walkable(BotNav.TileOf(Pos)) && SpikeExit() is Point exit)
                    {
                        // Standing on a tile the planner may not walk (spikes, the lip of a hole: a fight dodged us there): every plan starts
                        // from it, so first walk out the shortest way to floor.
                        offSpikes = true;
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine($"  no path from unwalkable tile {BotNav.TileOf(Pos)} ({BotNav.CurrentMap.GetTileClass(BotNav.TileOf(Pos))}): walking off it to {exit}");
                        }
                        var ec = BotNav.CentreOf(exit);
                        for (int w = 0; w < 45 && !BotNav.Walkable(BotNav.TileOf(Pos)); w++)
                        {
                            // (a plain step at a time: a fall or a knock-back ends it, and the replan starts from wherever we are)
                            float ex = ec.X - Pos.X, ey = ec.Y - Pos.Y;
                            var keys = new List<KeyFunctions>();
                            if (Math.Abs(ex) > 1.5f) { keys.Add(ex < 0 ? KeyFunctions.Left : KeyFunctions.Right); }
                            if (Math.Abs(ey) > 1.5f) { keys.Add(ey < 0 ? KeyFunctions.Up : KeyFunctions.Down); }
                            yield return keys.ToArray();
                        }
                        goto replan;
                    }
                    if (path == null && gateWait < 90 && BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "Gate" && e.Solid
                        && new Point((int)(e.Hitbox.Center.X / 160), (int)(e.Hitbox.Center.Y / 160)) == Grid))
                    {
                        // A gate here may be about to open (an enemy just killed is still dying, a button just pressed): give it a moment.
                        if (gateWait == 0 && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine($"  no path yet at {Pos}: waiting a moment for a shut gate on {Grid}");
                        }
                        for (int w = 0; w < 15; w++, gateWait++)
                        {
                            yield return K();
                        }
                        goto replan;
                    }
                    if (path == null)
                    {
                        if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine($"  NOPATH at {Pos} grid {Grid}: " + string.Join(" ", BotRunner.Entities().Where(e => e.exists && (e.GetType().Name is "Dust" or "PewLaser" or "Shieldy" or "Gate" or "Boulder")).Select(e => $"{e.GetType().Name}@{(int)e.Position.X},{(int)e.Position.Y}{(e.Solid ? " shut" : " open")}")) + $" kills={GlobalState.ENEMIES_KILLED} goal={what} forced={forced}");
                            var here = BotNav.TileOf(Pos);
                            System.Console.Error.WriteLine("  walkable round us: " + string.Join(" ", new[] { new Point(0, -1), new Point(0, 1), new Point(-1, 0), new Point(1, 0), new Point(0, 0) }.Select(o => new Point(here.X + o.X, here.Y + o.Y)).Select(q => $"{q.X},{q.Y}={BotNav.Walkable(q)}/{BotNav.CurrentMap.GetTileClass(q)}"))
                                + $" toGate={BotNav.FindPath(here, new Point(18, 14))?.Count} avoid={avoidStationary?.Count} hard={BotNav.HardAvoid?.Count} blocked={BotNav.Blocked.Count}");
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
                    else if (shortcut != null && shortcut.Count + 20 < path.Count && BotNav.WadeOk(shortcut, BotNav.TileOf(Pos)))
                    {
                        path = shortcut; // water that can be waded (with a current, or a short way)
                        jumping = GlobalState.inventory.CanJump;
                    }
                    else if (shortcut != null && shortcut.Count + 20 < path.Count && !shortcut.Any(t => BotNav.IsSpike(t))
                        && BotNav.RaftPath(BotNav.TileOf(Pos), OnConveyor(BotNav.TileOf(Pos)), shortcut[shortcut.Count - 1], GlobalState.inventory.CanJump) != null)
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
                if (path.Count > 0 && killGateFights < 2 && !InRaft && ShutKillGateOnPath(path) is Entity killGate && Enemies().Any(e => new Point((int)e.Center.X / 160, (int)e.Center.Y / 160) == Grid))
                {
                    // The route runs through this screen's kill gate, still shut: it opens on kills made here, so fight the screen first.
                    killGateFights++;
                    if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                    {
                        System.Console.Error.WriteLine($"  route through the shut kill gate at {killGate.Position} on {Grid}: fighting the screen first");
                    }
                    yield return Fight(3600, soft: true);
                    yield return Wait(30);
                    goto replan;
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
                    if (Environment.GetEnvironmentVariable("ANODYNE_GATE_TRACE") != null && path.Count > 0)
                    {
                        var probe = new[] { new Point(81, 61), new Point(88, 61), new Point(88, 63), new Point(88, 65), new Point(87, 65), new Point(86, 65), new Point(82, 65), new Point(81, 65), new Point(80, 65), new Point(79, 65) };
                        System.Console.Error.WriteLine("  probe: " + string.Join(" ", probe.Select(q => $"{q.X},{q.Y}={BotNav.Walkable(q)}")) + $" jump={BotNav.AllowJump} leap86={BotNav.DashLeapTiles(new Point(86, 65), new Point(-1, 0))} strict={BotNav.FindPath(BotNav.TileOf(Pos), t => t == path[path.Count - 1], null, strict: true)?.Count}");
                    }
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
                    if (Player.raft == null && !GlobalState.ScreenTransition && !chaserGaveUp.Contains(Grid)
                        && (BotRunner.Entities().Any(e => e.exists && (e is AnodyneSharp.Entities.Enemy.Crowd.BouncySpikeRoller || e is AnodyneSharp.Entities.Enemy.Apartment.BounceDashTrap && RouteCrossesLane(e, path, pi))) || RouteRunsByPillars(path, pi)))
                    {
                        // Spike rollers sweeping the room (bars that span it and turn only at walls): time this screen's stretch of the
                        // route against them, jumping each bar as it passes, then plan on from wherever that leaves us.
                        var scr = Grid;
                        int gi = pi;
                        while (gi < path.Count - 1 && path[gi].X / 10 == scr.X && path[gi].Y / 10 == scr.Y)
                        {
                            gi++;
                        }
                        if (BotNav.TileOf(Pos) != path[gi])
                        {
                            yield return SweeperWalk(path[gi]);
                            if (!_sweepWalkOk)
                            {
                                chaserGaveUp.Add(scr);
                            }
                            goto replan;
                        }
                    }
                    if (Player.raft == null && !GlobalState.ScreenTransition && !chaserGaveUp.Contains(Grid)
                        && BotRunner.Entities().Any(e => e.exists && e is AnodyneSharp.Entities.Enemy.Cell.Chaser))
                    {
                        // Chasers about: the plain walk would line up with one and meet it in a corridor. Time this screen's stretch of
                        // the route against them instead, then plan on from wherever that leaves us.
                        var scr = Grid;
                        int gi = pi;
                        while (gi < path.Count - 1 && path[gi].X / 10 == scr.X && path[gi].Y / 10 == scr.Y)
                        {
                            gi++;
                        }
                        if (BotNav.TileOf(Pos) != path[gi])
                        {
                            yield return ChaserWalk(path[gi]);
                            if (!_chaserWalkOk)
                            {
                                chaserGaveUp.Add(scr);
                            }
                            goto replan;
                        }
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
                    var leapFrom = pi == 0 ? BotNav.TileOf(Pos) : path[pi - 1];
                    if (jumping && BotNav.IsLeap(leapFrom, tile))
                    {
                        if (BotNav.TileOf(Pos) != leapFrom)
                        {
                            stuck = true; // never got to the take-off tile (knocked off the route): plan again from where we are
                            break;
                        }
                        yield return BotNav.IsDashLeap(leapFrom, tile) ? DashLeap(leapFrom, tile) : Leap(leapFrom, tile);
                        if (BotNav.TileOf(Pos) != tile)
                        {
                            stuck = true; // came down somewhere else (or not at all): plan again from here
                            break;
                        }
                        continue;
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
                        {
                            // Something that hurts a jumper too (a Chaser pacing this corridor) must keep off the whole way, run-up to
                            // landing, for the time the jump takes: wait for it to pass (it is never fought).
                            var landC0 = BotNav.CentreOf(land);
                            for (int w = 0; w < 900; w++)
                            {
                                var pbx0 = Player.Hitbox;
                                var lane = Rectangle.Union(pbx0, new Rectangle((int)landC0.X, (int)landC0.Y, pbx0.Width, pbx0.Height));
                                lane.Inflate(4, 4);
                                bool clear = true, meSafe = true, clearForJump = true;
                                var me0 = pbx0;
                                me0.Inflate(4, 4);
                                foreach (var t in PredictThreats(null).Where(t => t.HurtsInAir))
                                {
                                    for (int k = 0; k <= 60 && clear; k++)
                                    {
                                        clear = !(t.At[k] is Rectangle r && lane.Intersects(r));
                                        clearForJump &= clear || k > 34;
                                    }
                                    for (int k = 0; k <= 45 && meSafe; k++)
                                    {
                                        meSafe = !(t.At[k] is Rectangle r && me0.Intersects(r));
                                    }
                                }
                                // (standing where it will burn - on a fire pillar's tile, say - the lane need only stay clear for the jump itself)
                                if (clear || !meSafe && clearForJump)
                                {
                                    break;
                                }
                                if (!meSafe && jumpLaneReplans < 10)
                                {
                                    // it is coming for where we stand: no waiting here, let the dodging walk take us out of its way
                                    jumpLaneReplans++;
                                    goto replan;
                                }
                                BotRunner.Status = "jump: wait for the lane to clear";
                                BotRunner.NotStuck();
                                yield return K();
                                yield return K(); // (look again every other frame: the whole room's threats are dear to predict)
                            }
                        }
                        {
                            // Line up with the take-off tile across the jump first: a jump started a few px to the side clips the wall
                            // beside a one-tile gap and comes down short (in the pit, or back where we were).
                            var fc = BotNav.CentreOf(from);
                            bool vertical = jk == KeyFunctions.Up || jk == KeyFunctions.Down;
                            for (int w = 0; w < 40; w++)
                            {
                                float off = vertical ? fc.X - Pos.X : fc.Y - Pos.Y;
                                if (Math.Abs(off) <= 1.5f)
                                {
                                    break;
                                }
                                BotRunner.Status = "jump: line up";
                                yield return K(vertical ? (off < 0 ? KeyFunctions.Left : KeyFunctions.Right) : (off < 0 ? KeyFunctions.Up : KeyFunctions.Down));
                            }
                        }
                        if (BotNav.IsHole(tile))
                        {
                            // A pit: the jump only carries ~27px, so take off from the edge of the tile before it (centre 3px short of the pit).
                            var dv = jk == KeyFunctions.Right ? new Vector2(1, 0) : jk == KeyFunctions.Left ? new Vector2(-1, 0) : jk == KeyFunctions.Down ? new Vector2(0, 1) : new Vector2(0, -1);
                            var edge = BotNav.CentreOf(from) + dv * 5f;
                            for (int w = 0; w < 30 && ((edge - Pos) * dv).Length() > 0.8f && Vector2.Dot(edge - Pos, dv) > 0; w++)
                            {
                                BotRunner.Status = "jump the pit: run-up";
                                yield return K(jk);
                            }
                        }
                        {
                            // Pits and spikes alike: not into a fireball, a bullet or a patrolling enemy (Fields Shieldies between spike rows) where we land.
                            var landC = BotNav.CentreOf(land);
                            for (int w = 0; w < 300; w++)
                            {
                                var th = PredictThreats(null);
                                var pbx = Player.Hitbox;
                                bool landSafe = true;
                                foreach (var t in th)
                                {
                                    for (int k = 15; k <= 50 && landSafe; k++)
                                    {
                                        if (t.At[k] is Rectangle r)
                                        {
                                            // CentreOf is where our box's corner goes to centre it on the tile; we may also come down short of that (the jump carries ~27px).
                                            var me2 = new Rectangle((int)landC.X, (int)landC.Y, pbx.Width, pbx.Height);
                                            var jv = jk == KeyFunctions.Right ? new Vector2(27, 0) : jk == KeyFunctions.Left ? new Vector2(-27, 0) : jk == KeyFunctions.Down ? new Vector2(0, 27) : new Vector2(0, -27);
                                            var me3 = new Rectangle((int)(Pos.X + jv.X), (int)(Pos.Y + jv.Y), pbx.Width, pbx.Height);
                                            r.Inflate(3, 3);
                                            landSafe = !me2.Intersects(r) && !me3.Intersects(r);
                                        }
                                    }
                                }
                                if (w == 0 && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                                {
                                    System.Console.Error.WriteLine($"    landing {landC} safe={landSafe} threats: " + string.Join(" ", th.Select(t => $"{t.Name}{(t.At[20] is Rectangle q ? q.ToString() : "-")}")));
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
                    if (GlobalState.inventory.GetCurrentMapKeys() > 0 && BotRunner.Entities().FirstOrDefault(e => e.exists && e.Solid && e is AnodyneSharp.Entities.Gadget.SmallKeyGate
                            && BotNav.TileOf(e.Position + new Vector2(3, 2)) == tile) is Entity keyGate)
                    {
                        // A keyblock in the way and a key in hand: push into it from the tile before.
                        var kfrom = pi == 0 ? BotNav.TileOf(Pos) : path[pi - 1];
                        var kdir = tile.X > kfrom.X ? KeyFunctions.Right : tile.X < kfrom.X ? KeyFunctions.Left : tile.Y > kfrom.Y ? KeyFunctions.Down : KeyFunctions.Up;
                        var kc = BotNav.CentreOf(kfrom);
                        yield return OpenKeyBlock(kc.X, kc.Y, kdir);
                    }
                    if (BotRunner.Entities().FirstOrDefault(e => e.exists && e.Solid && e is AnodyneSharp.Entities.Gadget.BigCardGate g
                            && GlobalState.inventory.CardCount >= g._preset.Frame && e.Hitbox.Intersects(new Rectangle(tile.X * 16, tile.Y * 16, 16, 16))) is Entity cardGate)
                    {
                        // A card gate in the way and the cards for it: open it from the side we come from.
                        var kfrom = pi == 0 ? BotNav.TileOf(Pos) : path[pi - 1];
                        yield return OpenCardGate(cardGate, kfrom.Y > tile.Y);
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
                            offRoute = Math.Max(0, offRoute - 1); // (on the route again: the allowance builds back up)
                            break;
                        }
                        if (i > 10 && offRoute < 30 && offRouteTotal < 80)
                        {
                            // Dodged (or knocked) off the route, two tiles or more from the step we are on: walking at that tile from
                            // here can only push into a wall, so plan again from where we are.
                            var hereT = BotNav.TileOf(p);
                            var prevT = pi == 0 ? hereT : path[pi - 1];
                            if (Math.Abs(hereT.X - tile.X) + Math.Abs(hereT.Y - tile.Y) > 1 && Math.Abs(hereT.X - prevT.X) + Math.Abs(hereT.Y - prevT.Y) > 1)
                            {
                                offRoute++;
                                offRouteTotal++;
                                if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                                {
                                    System.Console.Error.WriteLine($"  off the route at {hereT} (step {prevT}>{tile}): replan {offRoute}");
                                }
                                goto replan;
                            }
                        }
                        if (i == 150 && Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                        {
                            System.Console.Error.WriteLine($"  slow step to tile {tile} (centre {c}) at {p}: path " + string.Join(">", path.Skip(Math.Max(0, pi - 3)).Take(10).Select(q => $"{q.X},{q.Y}")));
                        }
                        if (annoyerFights < 6 && !OnWater() && GlobalState.inventory.HasBroom && Enemies().Any(e => e.GetType().Name == "Annoyer"
                            && (new Vector2(e.Hitbox.Center.X, e.Hitbox.Center.Y) - (Pos + new Vector2(5, 6))).Length() < (AutoFight ? 70 : 48)))
                        {
                            // An awake Annoyer comes for us wherever we walk (and walking about only dances us into its swoop): the broom answers it.
                            // (even with auto-fight off: one that is already on us is self-defence, and never a tool the route needs alive)
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
                        if (still > 40 && gateWait < 180 && BotRunner.Entities().Any(e => e.exists && e.GetType().Name == "Gate" && e.Solid
                            && e.Hitbox.Intersects(new Rectangle(tile.X * 16, tile.Y * 16, 16, 16))))
                        {
                            // Up against a gate the route counts on opening (the last kill still dying): wait for it, not round it.
                            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null && gateWait == 0)
                            {
                                System.Console.Error.WriteLine($"  at a shut gate on {tile}: waiting for it to open");
                            }
                            for (int w = 0; w < 15; w++, gateWait++)
                            {
                                yield return K();
                            }
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
                        if (BlockedBy is AnodyneSharp.Entities.Enemy.Redcave.Mover && !OnConveyor(BotNav.TileOf(Pos + DirVector(dir) * 8)))
                        {
                            // A Mover's touch only shoves us (it cannot be fought): walk on by it.
                            yield return K(dir);
                            continue;
                        }
                        if (BlockedBy != null && blockerHandled < 6)
                        {
                            // A stationary enemy is in the way. First try a route that keeps clear of it, else kill it, then replan.
                            var blocker = BlockedBy;
                            blockerHandled++;
                            if (blocker is AnodyneSharp.Entities.Enemy.Crowd.BaseSpikeRoller && blocker.velocity == Vector2.Zero && GlobalState.inventory.CanJump
                                && RollerHop(blocker, dir) is float takeoff)
                            {
                                // A roller that has stopped across the way is a spiked wall, but a thin one, and rollers miss us in the air.
                                BotRunner.Status = "jump the stopped roller";
                                yield return JumpToward(dir, takeoff);
                                goto replan;
                            }
                            HashSet<Point> avoid = null;
                            List<Point> detour = null;
                            // (a tighter berth when a wide one leaves no way: a Dustmaid asleep in a corridor two tiles wide)
                            foreach (int berthM in new[] { 12, 6, 1 })
                            {
                                avoid = BotNav.NearEnemies(berthM);
                                avoid.Remove(BotNav.TileOf(Pos));
                                BotNav.Avoid = avoid;
                                detour = BotNav.FindPath(BotNav.TileOf(Pos), isGoal, forced);
                                BotNav.Avoid = null;
                                if (detour != null)
                                {
                                    break;
                                }
                            }
                            if (Environment.GetEnvironmentVariable("ANODYNE_BOT_DEBUG") != null)
                            {
                                System.Console.Error.WriteLine($"  blocked by stationary {blocker.GetType().Name}@{blocker.Hitbox.X},{blocker.Hitbox.Y} at {Pos}: " + (detour != null ? $"detour of {detour.Count} tiles via {string.Join(">", detour.Take(8).Select(q => $"{q.X},{q.Y}"))} from tile {BotNav.TileOf(Pos)} (dir {dir})" : "no detour, fighting it"));
                            }
                            if (detour != null && detour.Count > 0 && (detour.Count <= path.Count + 20 || !GlobalState.inventory.HasBroom))
                            {
                                avoidStationary = avoid;
                            }
                            else if (DormantDustmaid(blocker))
                            {
                                // asleep it cannot be hurt, and touching it hurts: never a fight; edge past it (the dodging walk keeps clear)
                                avoidStationary = avoid;
                            }
                            else if (GlobalState.inventory.HasBroom && blocker.GetType().Name == "Shieldy")
                            {
                                // A shield-bearer in the way: push it along the way we are going (left, right or up), then carry on.
                                yield return PushBlocker(blocker, dir);
                            }
                            else if (GlobalState.inventory.HasBroom)
                            {
                                // An Annoyer close by swoops while we deal with the blocker: it is fought too.
                                yield return Fight(1200, soft: true, only: e => e == blocker || (e.GetType().Name == "Annoyer" && Vector2.Distance(e.Center, Player.Center) < 64));
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
                            if (dodgeWaits < 900)
                            {
                                // holding back for something that moves (a Chaser pacing the corridor ahead): this tile's time and the
                                // stuck clock wait with us, up to a point
                                dodgeWaits++;
                                i--;
                                BotRunner.NotStuck();
                            }
                        }
                        yield return safe;
                    }
                    if (stuck)
                    {
                        break;
                    }
                    if (offRoute < 30 && Vector2.Distance(Pos, BotNav.CentreOf(tile)) > 10f)
                    {
                        // Out of time on this tile and nowhere near it (dodged off the route): plan again from here rather than walk on to
                        // the next tile of a route we are no longer on.
                        offRoute++;
                        goto replan;
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
        public static readonly string[] PartNames = { "nexus", "roadway", "overworld1", "temple", "temple1", "forest2", "fields", "beach", "beach2", "beach3", "redsea", "redcave", "redcave2", "redcave3", "redcave4", "cliff", "crowd", "windmill", "temple2", "suburb", "apartment", "hotel", "hotel2", "hotel3", "hotel4", "hotel5", "hotel6", "cards", "circus", "circusb", "circus2", "circus3" };

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
            "suburb" => new Suburb(),
            "apartment" => new Apartment(),
            "hotel" => new Hotel(),
            "hotel2" => new Hotel2(),
            "hotel3" => new Hotel3(),
            "hotel4" => new Hotel4(),
            "hotel5" => new Hotel5(),
            "hotel6" => new Hotel6(),
            "cards" => new CardsToCircus(),
            "circus" => new Circus(),
            "circusb" => new CircusB(),
            "circus2" => new Circus2(),
            "circus3" => new Circus3(),
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
