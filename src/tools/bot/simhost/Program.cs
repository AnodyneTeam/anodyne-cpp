using AnodyneSharp.Recording;
using System;
using System.Diagnostics;
using System.Linq;
using System.Collections.Generic;
using System.IO;

// usage: SimHost <anodyne_sim binary> [port] [--bot PART[,PART...]]
// Starts the simulator headless. With --bot, plays those route parts in order (BotScripts.PartNames, e.g. nexus,roadway) with the C#
// bot, one bot frame per simulator frame. Without it, a link check: presses A through the title into the game, walks right
// and prints what the IGameView reads.
int port = args.Length > 1 && int.TryParse(args[1], out int pa) ? pa : 47011;
int botAt = Array.IndexOf(args, "--bot");
string part = botAt >= 0 && botAt + 1 < args.Length ? args[botAt + 1] : null;
// (a replay reads no pixels unless SIMHOST_SHOT asks for some: don't draw them)
bool noRender = Array.IndexOf(args, "--replay") >= 0 && Environment.GetEnvironmentVariable("SIMHOST_SHOT") == null;
var simStart = new ProcessStartInfo(args[0], $"--headless --ipc {port}" + (noRender ? " --no-render" : ""))
{
    RedirectStandardError = true,
    RedirectStandardOutput = true,
};
// The C# game sees the bot's held keys as fresh presses every frame (headless, KeyInput.Update clears the key history
// the bot keeps in the same dictionary): the port gets them the same way.
simStart.Environment["ANODYNE_SIM_KEY_REPEAT"] = "1";
// RecordingManager.Now counts the C# game's frames, which start 340 after the port's (the port's boot and title):
// Slime's move timer truncates each frame's ticks, so the port counts the same way.
simStart.Environment["ANODYNE_SIM_NOW_OFFSET"] = "340";
var sim = Process.Start(simStart);
// SIMHOST_STDERR=1: pass the simulator's stderr through (its debug prints)
bool simStderr = Environment.GetEnvironmentVariable("SIMHOST_STDERR") == "1";
sim.ErrorDataReceived += (_, e) => { if (simStderr && e.Data != null) Console.Error.WriteLine(e.Data); };
sim.OutputDataReceived += (_, _) => { };
sim.BeginErrorReadLine();
sim.BeginOutputReadLine();
SimGameView view = null;
for (int i = 0; i < 50 && view == null; i++)
{
    try
    {
        view = new SimGameView(port: port);
    }
    catch (System.Net.Sockets.SocketException)
    {
        System.Threading.Thread.Sleep(100);
    }
}
void Show(string tag)
{
    var p = view.Player;
    Console.WriteLine($"{tag}: f{view.Frame} mode={view.Mode} map={view.Map} grid={view.Grid} hp={view.Health}/{view.MaxHealth} dialog={view.Dialogue != null} broom={view.HasBroom}");
    if (p != null)
    {
        Console.WriteLine($"  player {p.Position} box={p.Hitbox} v={p.velocity} facing={p.facing} state={p.state} anim={p.CurAnimName} swing={p.broom.exists}");
    }
    foreach (var e in view.Entities)
    {
        Console.WriteLine($"  {e.GetType().Name} {e.Hitbox} exists={e.exists} solid={e.Solid} anim={e.CurAnimName}");
    }
}

int replayAt = Array.IndexOf(args, "--replay");
if (replayAt >= 0)
{
    Gs.View = view;
    BotRunner.Entities = () => view.Entities;
    Replay(args[replayAt + 1], args[replayAt + 2], replayAt + 3 < args.Length ? int.Parse(args[replayAt + 3]) : 20);
}
else if (part != null)
{
    Gs.View = view;
    BotRunner.Entities = () => view.Entities;
    // (a bot failure exits the process: show where it left the game)
    AppDomain.CurrentDomain.ProcessExit += (_, _) =>
    {
        if (BotRunner.Active)
        {
            Show("bot failed");
        }
    };
    // (each part continues from where the one before left the game, not from the title)
    Environment.SetEnvironmentVariable("ANODYNE_BOT_ONLY", "1");
    long frames = 0;
    // SIMHOST_SHOT=F1,F2...: a screenshot at those frames (/tmp/simhost_fN.png)
    var shotAt = (Environment.GetEnvironmentVariable("SIMHOST_SHOT") ?? "").Split(',', StringSplitOptions.RemoveEmptyEntries).Select(long.Parse).ToHashSet();
    // SIMHOST_TRACE=FROM-TO: print every frame's keys and position in that frame range
    var tr = (Environment.GetEnvironmentVariable("SIMHOST_TRACE") ?? "").Split('-');
    long traceFrom = tr.Length == 2 && long.TryParse(tr[0], out long t0) ? t0 : -1;
    long traceTo = tr.Length == 2 && long.TryParse(tr[1], out long t1) ? t1 : -1;
    string status = null;
    foreach (var name in part.Split(','))
    {
        Console.WriteLine($"== {name}");
        BotRunner.Start(name);
        while (BotRunner.Active && frames < 200000)
        {
            BotRunner.PlayBusy = view.Busy;
            BotRunner.InDeathMenu = view.DeathMenu;
            BotRunner.Deaths = view.Deaths;
            BotRunner.NextFrame();
            view.Hold(BotRunner.GbaMask, 1);
            frames++;
            if (shotAt.Contains(view.Frame))
            {
                view.Shot($"/tmp/simhost_f{view.Frame}.png");
            }
            if (traceFrom >= 0 && view.Frame >= traceFrom && view.Frame <= traceTo)
            {
                var pc = view.Player?.Center ?? Microsoft.Xna.Framework.Vector2.Zero;
                var near = view.Entities.SelectMany(e => new[] { e }.Concat(view.SubEntities(e)))
                    .Where(e => e.exists && (e.Center - pc).Length() < 48).Select(e => $"{e.GetType().Name}@{e.Hitbox.X},{e.Hitbox.Y} v{e.velocity.X:0},{e.velocity.Y:0}");
                Console.WriteLine($"  t f{view.Frame} keys={BotRunner.GbaMask:x3} {view.Map} {view.Player?.Position} hp={view.Health} puz={view.PuzzlesSolved} grid={view.Grid.X},{view.Grid.Y} busy={view.Busy}/{view.Raw.GetProperty("dialog")}/{view.Raw.GetProperty("cutscene")} face={view.Player?.facing} swing={view.Player?.broom.exists} dust={(view.Player?.broom.dust is { } dd ? $"{dd.Position.X:0},{dd.Position.Y:0}:{dd.exists}:{dd.CurAnimName}" : "-")} [{BotRunner.Status}] {string.Join(" ", near)} bosses={view.Raw.GetProperty("bosses")} killed={view.EnemiesKilled} {string.Join(" ", view.Entities.Where(e => e.GetType().Name is "Seer" or "Gate").Select(e => $"{e.GetType().Name}@{e.Hitbox.X},{e.Hitbox.Y}:{e.CurAnimName}:{(e.Solid ? "S" : "")}"))}");
            }
            if (BotRunner.Status != status)
            {
                status = BotRunner.Status;
                Console.WriteLine($"f{view.Frame} [{status}] {view.Map} {view.Player?.Position} entities={view.Entities.Count}");
            }
        }
        if (BotRunner.Active)
        {
            break;
        }
        Show("part done");
    }
    if (BotRunner.Active)
    {
        Show("gave up");
    }
}
else
{
    const int A = 1, Right = 1 << 4;
    view.Hold(0, 400);
    for (int n = 0; view.Mode != "play" && n < 6000; n += 12)
    {
        view.Hold(A, 1);
        view.Hold(0, 11);
    }
    view.Hold(0, 30);
    Show("in game");
    view.Hold(Right, 30);
    Show("walking right");
    view.Hold(Right | A, 2);
    Show("swinging");
    view.Hold(0, 30);
    Show("standing");
}
view.Dispose();
sim.Kill();

// --replay GBAINPUT TRACE [N]: plays a C# run's keys (run_bot.sh's .gbainput) on the port from the frame the C# game
// first had a map (both boot by tapping A until then), and prints where the port's trace (as BotRunner.Trace writes
// it) first differs from the C# one, then up to N more differing frames.
void Replay(string inputPath, string tracePath, int show)
{
    var masks = new List<int> { 0 }; // (frame 0: none)
    // "# checkpoint FRAME MAP X Y": the bot set its respawn point as the checkpoint (on the death screen) then
    var checkpoints = new Dictionary<long, string[]>();
    foreach (var line in File.ReadLines(inputPath))
    {
        var w = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        if (w.Length == 6 && w[1] == "checkpoint")
        {
            checkpoints[long.Parse(w[2])] = w[3..];
        }
        else if (w.Length == 2 && int.TryParse(w[0], out int n))
        {
            int m = Convert.ToInt32(w[1], 16);
            masks.AddRange(Enumerable.Repeat(m, n));
        }
    }
    var cs = File.ReadLines(tracePath).Skip(1).Select(l => l.Split('\t')).ToList();
    int start = cs.FindIndex(r => r[1] != "");
    long csFrame = long.Parse(cs[start][0]);
    for (int n = 0; view.Mode != "play" && n < 6000; n += 12)
    {
        view.Hold(1, 1);
        view.Hold(0, 11);
    }
    Console.WriteLine($"port in {view.Map} at its f{view.Frame}; the C# game at f{csFrame}");
    // SIMHOST_SAVE=the Save_1.dat a C# part started from (part.sh): the port has no saves to load, so it starts a new game and
    // is given what the save holds, then warped to its checkpoint; its own frame count is put back to what it was before.
    if (Environment.GetEnvironmentVariable("SIMHOST_SAVE") is string savePath)
    {
        long before = view.GameFrame();
        using var save = System.Text.Json.JsonDocument.Parse(File.ReadAllText(savePath));
        var root = save.RootElement;
        var inv = root.GetProperty("inventory");
        var said = new List<string>();
        if (inv.GetProperty("HasTransformer").GetBoolean())
        {
            said.Add("give brooms");
        }
        if (inv.GetProperty("CanJump").GetBoolean())
        {
            said.Add("give jump");
        }
        said.Add($"give cards {inv.GetProperty("CardStatus").EnumerateArray().TakeWhile(c => c.GetBoolean()).Count()}");
        said.Add($"give hearts {root.GetProperty("max_health").GetInt32()}");
        foreach (var ev in root.GetProperty("events").GetProperty("eventKeys").EnumerateObject())
        {
            said.Add($"event {ev.Name} {ev.Value.GetInt32()}");
        }
        var cp = root.GetProperty("checkpoint");
        string cpMap = cp.GetProperty("map").GetString();
        int cpX = cp.GetProperty("Position").GetProperty("X").GetInt32(), cpY = cp.GetProperty("Position").GetProperty("Y").GetInt32();
        said.Add($"checkpoint {cpMap} {cpX} {cpY}");
        said.Add($"warp {cpMap} {cpX} {cpY}");
        foreach (var line in said)
        {
            if (!view.Command(line))
            {
                throw new InvalidOperationException($"port refused '{line}'");
            }
        }
        for (int n = 0; n < 600 && !(view.Map == cpMap && view.Mode == "play" && !view.ScreenTransition && !view.Warping && n > 2); n++)
        {
            view.Hold(0, 1);
        }
        Console.WriteLine($"port given the save's things and warped to {view.Map} {view.Player?.Position}; frame count back to {before}");
        view.GameFrame(before);
    }
    long frameLead = view.GameFrame() - csFrame; // (the port's RecordingManager.Now frames, against the C#'s)
    // (the title screens draw random numbers differently: start play from the C#'s RNG position)
    var csEnts = new Dictionary<long, string[]>();
    string entsPath = Environment.GetEnvironmentVariable("SIMHOST_ENTS");
    if (entsPath != null)
    {
        foreach (var l in File.ReadLines(entsPath))
        {
            var w = l.Split('\t');
            csEnts[long.Parse(w[0])] = w;
        }
        if (csEnts.TryGetValue(csFrame, out var first))
        {
            view.SetRng(long.Parse(first[1]));
        }
    }
    string Row(long f)
    {
        var p = view.Player;
        return $"{f}\t{view.Map}\t{(p == null ? 0 : (int)p.Position.X)}\t{(p == null ? 0 : (int)p.Position.Y)}\t{view.Health}\t{(view.Dialogue == null ? 0 : 1)}\tb{(view.HasBroom ? 1 : 0)}k{view.MapKeys}c{view.CardCount}";
    }
    // SIMHOST_ENTS=the C# run's ANODYNE_ENT_TRACE file: also compare each frame's RNG draws and entities (only the
    // types the port has shown so far: the C# also lists the ones the port has no class for yet)
    // SIMHOST_DUMP=FROM-TO: print both games' rows and entities for those C# frames
    long dumpFrom = -1, dumpTo = -1;
    bool rawDiffShown = false;
    if (Environment.GetEnvironmentVariable("SIMHOST_DUMP") is string dump)
    {
        var d = dump.Split('-');
        dumpFrom = long.Parse(d[0]);
        dumpTo = long.Parse(d[1]);
    }
    var portTypes = new HashSet<string>();
    // (and only the types the C#'s trace lists at all: it has no entry for sub-entities it updates by hand, like
    // HealthCicadaSentinel's HealthCicada)
    var csTypes = csEnts.Values.SelectMany(w => w.Length > 2 ? w[2].Split(' ', StringSplitOptions.RemoveEmptyEntries) : Array.Empty<string>())
        .Select(e => e.Substring(0, e.IndexOf('@'))).ToHashSet();
    string Types(string ents, bool learn) => string.Join(" ", ents.Split(' ', StringSplitOptions.RemoveEmptyEntries).Where(e =>
    {
        string t = e.Substring(0, e.IndexOf('@'));
        if (t == "Entity")
        {
            return false; // plain entities (the C#'s helpers, the port's classes without a C# name): not comparable
        }
        if (learn)
        {
            portTypes.Add(t);
        }
        return portTypes.Contains(t) && csTypes.Contains(t);
    }));
    int diffs = 0;
    bool entDiffShown = false;
    // After the credits both games sit on the title, whose menus draw random numbers and take frames differently:
    // wait for the C# to load the save (its row stops repeating the last one before the title, and has a map again), then
    // boot the port
    // the same way as at the start and take the C#'s RNG position again
    string[] titleRow = null;
    bool titleBlank = false; // a C# row without a map: it is on the title, and its next row with one has loaded the save
    long resynced = -1; // (SetRng lands with the next frame: no RNG comparison on this one)
    for (int i = start; i < cs.Count && diffs <= show; i++)
    {
        long f = long.Parse(cs[i][0]);
        if (titleRow != null)
        {
            if (cs[i][1] == "")
            {
                titleBlank = true;
                continue;
            }
            if (!titleBlank && cs[i][1] == titleRow[1] && cs[i][2] == titleRow[2] && cs[i][3] == titleRow[3])
            {
                continue;
            }
            titleRow = null;
            titleBlank = false;
            for (int n = 0; view.Mode != "play" && n < 6000; n += 12)
            {
                view.Hold(1, 1);
                view.Hold(0, 11);
            }
            Console.WriteLine($"port loaded its save into {view.Map}; the C# game at f{f}");
            resynced = f;
            if (csEnts.TryGetValue(f, out var re))
            {
                view.SetRng(long.Parse(re[1]), keepShake: true);
            }
            view.GameFrame(f + frameLead);
        }
        else if (i > start)
        {
            if (checkpoints.TryGetValue(f, out var cp))
            {
                view.SetCheckpoint(cp[0], int.Parse(cp[1]), int.Parse(cp[2]));
            }
            view.Hold(f < masks.Count ? masks[(int)f] : 0, 1);
            if (view.Mode == "title")
            {
                titleRow = cs[i];
                continue;
            }
        }
        if (f % 50000 == 0)
        {
            Console.Error.WriteLine($"(replay at C# f{f}, port {view.Mode} {view.Map})"); // (progress, and where a hang is)
        }
        string mine = Row(f), theirs = string.Join("\t", cs[i]);
        if (f >= dumpFrom && f <= dumpTo)
        {
            Console.WriteLine($"f{f}\n  C#   {theirs}\t{(csEnts.TryGetValue(f, out var de) ? de[1] + " " + (de.Length > 2 ? de[2] : "") : "")}\n  port {mine}\t{BotRunner.EntLine().Replace('\t', ' ')}\n  port pos {view.Player?.Position.X:R},{view.Player?.Position.Y:R} mode={view.Mode} mask={(f < masks.Count ? masks[(int)f] : -1):x}");
        }
        if (!entDiffShown && i > start && f != resynced && csEnts.TryGetValue(f, out var ce))
        {
            var mineE = BotRunner.EntLine().Split('\t');
            string me = Types(mineE[1], true), te = Types(ce.Length > 2 ? ce[2] : "", false);
            if (mineE[0] != ce[1] || me != te)
            {
                entDiffShown = true;
                Console.WriteLine($"first entity/RNG difference at C# f{f}:\n  C#   rng={ce[1]} {te}\n  port rng={mineE[0]} {me}");
            }
        }
        if (!rawDiffShown && i > start && f != resynced && csEnts.TryGetValue(f, out var cr) && cr.Length > 3 && cr[3] != view.PlayerRaw)
        {
            // (the player's exact Fix16 position: a sub-pixel drift shows up here long before it moves a pixel in the rows)
            rawDiffShown = true;
            Console.WriteLine($"first player sub-pixel difference at C# f{f}: C# {cr[3]} port {view.PlayerRaw}");
        }
        if (mine != theirs)
        {
            if (diffs++ == 0)
            {
                Console.WriteLine($"first difference at C# f{f}:");
            }
            Console.WriteLine($"  C#   {theirs}\n  port {mine}");
        }
    }
    Console.WriteLine(diffs == 0 ? $"identical for {cs.Count - start} frames" : $"({diffs} differing frames shown)");
}
