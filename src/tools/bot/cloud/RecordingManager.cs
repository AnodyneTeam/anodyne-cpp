using System;

namespace AnodyneSharp.Recording
{
    /// <summary>
    /// Command line for bot runs (reconstructed glue: the original lives in an unpublished AnodyneSharp checkout).
    /// -bot=&lt;part|keys&gt; -trace=&lt;file&gt; -gbainput=&lt;file&gt; -fast. Any of these: deterministic RNG and a fixed 1/60 s step.
    /// -record/-playback are accepted but not implemented here.
    /// </summary>
    public static class RecordingManager
    {
        public const int DeterministicSeed = 1234;
        public const float FixedStep = 1f / 60f;

        public static bool Fast { get; private set; }
        public static string BotPath { get; private set; }
        public static string TracePath { get; private set; }
        public static string GbaInputPath { get; private set; }
        public static long ResumeAt { get; private set; } = -1;

        /// <summary>ANODYNE_BOT_DRAW_EVERY=n: with -fast, draw only every n-th frame (screenshots show the last drawn frame).</summary>
        public static readonly int DrawEvery = int.TryParse(Environment.GetEnvironmentVariable("ANODYNE_BOT_DRAW_EVERY"), out int de) ? de : 1;

        private static long _frameNumber;
        public static long CurrentFrame => _frameNumber;

        public static bool Deterministic => Fast || BotPath != null || TracePath != null;

        public static void ParseCommandLine(string[] args)
        {
            foreach (var arg in args)
            {
                if (arg.Equals("-fast", StringComparison.OrdinalIgnoreCase))
                {
                    Fast = true;
                }
                else if (arg.StartsWith("-bot=", StringComparison.OrdinalIgnoreCase))
                {
                    BotPath = arg.Substring("-bot=".Length);
                }
                else if (arg.StartsWith("-trace=", StringComparison.OrdinalIgnoreCase))
                {
                    TracePath = arg.Substring("-trace=".Length);
                }
                else if (arg.StartsWith("-gbainput=", StringComparison.OrdinalIgnoreCase))
                {
                    GbaInputPath = arg.Substring("-gbainput=".Length);
                }
                else if (arg.StartsWith("-resumeat=", StringComparison.OrdinalIgnoreCase))
                {
                    ResumeAt = long.Parse(arg.Substring("-resumeat=".Length));
                }
                else if (arg.StartsWith("-record=", StringComparison.OrdinalIgnoreCase) || arg.StartsWith("-playback=", StringComparison.OrdinalIgnoreCase))
                {
                    Console.Error.WriteLine($"RecordingManager: {arg} is not supported by this build (ignored)");
                }
            }
        }

        /// <summary>Called once after content is loaded: opens the outputs and starts the bot.</summary>
        public static void Start()
        {
            if (Deterministic)
            {
                Registry.GlobalState.RNG = new Random(DeterministicSeed);
            }
            if (TracePath != null)
            {
                BotRunner.OpenTrace(TracePath);
            }
            if (GbaInputPath != null)
            {
                BotRunner.OpenGbaInput(GbaInputPath);
            }
            if (BotPath != null)
            {
                BotRunner.Start(BotPath);
            }
        }

        public static void FrameUpdate()
        {
            _frameNumber++;
        }
    }
}
