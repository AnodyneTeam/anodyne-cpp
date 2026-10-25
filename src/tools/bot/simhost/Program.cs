using AnodyneSharp.Recording;
using System;
using System.Diagnostics;

// usage: SimHost <anodyne_sim binary> [port] [--bot PART]
// Starts the simulator headless. With --bot, plays that route part (BotScripts.PartNames, e.g. nexus) with the C# bot,
// one bot frame per simulator frame. Without it, a link check: presses A through the title into the game, walks right
// and prints what the IGameView reads.
int port = args.Length > 1 && int.TryParse(args[1], out int pa) ? pa : 47011;
int botAt = Array.IndexOf(args, "--bot");
string part = botAt >= 0 && botAt + 1 < args.Length ? args[botAt + 1] : null;
var sim = Process.Start(new ProcessStartInfo(args[0], $"--headless --ipc {port}")
{
    RedirectStandardError = true,
    RedirectStandardOutput = true,
});
sim.ErrorDataReceived += (_, _) => { };
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
}

if (part != null)
{
    Gs.View = view;
    BotRunner.Start(part);
    long frames = 0;
    string status = null;
    while (BotRunner.Active && frames < 200000)
    {
        BotRunner.PlayBusy = view.Busy;
        BotRunner.NextFrame();
        view.Hold(BotRunner.GbaMask, 1);
        frames++;
        if (BotRunner.Status != status)
        {
            status = BotRunner.Status;
            Console.WriteLine($"f{view.Frame} [{status}] {view.Map} {view.Player?.Position}");
        }
    }
    Show(BotRunner.Active ? "gave up" : "part done");
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
