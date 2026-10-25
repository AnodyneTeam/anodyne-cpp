using AnodyneSharp.Recording;
using System;
using System.Diagnostics;

// usage: SimHost <anodyne_sim binary> [port]
// Starts the simulator headless, presses A through the title into the game, then walks right for a second and prints
// what the IGameView reads at each step.
var sim = Process.Start(new ProcessStartInfo(args[0], $"--headless --ipc {(args.Length > 1 ? args[1] : "47011")}")
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
        view = new SimGameView(port: args.Length > 1 ? int.Parse(args[1]) : 47011);
    }
    catch (System.Net.Sockets.SocketException)
    {
        System.Threading.Thread.Sleep(100);
    }
}
const int A = 1, Right = 1 << 4;
view.Hold(0, 400);
for (int n = 0; view.Mode != "play" && n < 6000; n += 12)
{
    view.Hold(A, 1);
    view.Hold(0, 11);
}
view.Hold(0, 30);
void Show(string tag)
{
    var p = view.Player;
    Console.WriteLine($"{tag}: f{view.Frame} map={view.Map} grid={view.Grid} hp={view.Health}/{view.MaxHealth} dialog={view.Dialogue != null} broom={view.HasBroom}");
    Console.WriteLine($"  player {p.Position} box={p.Hitbox} v={p.velocity} facing={p.facing} state={p.state} anim={p.CurAnimName} swing={p.broom.exists}");
}
Show("in game");
view.Hold(Right, 30);
Show("walking right");
view.Hold(Right | A, 2);
Show("swinging");
view.Hold(0, 30);
Show("standing");
view.Dispose();
sim.Kill();
