using AnodyneSharp.Recording;
using Microsoft.Xna.Framework;
using System;
using System.Collections.Generic;
using System.Linq;

/// <summary>A re-implementation of the Seer boss (Seer.cs) frame by frame: its two stages, orbs, dive/dust cycle and the wave.</summary>
sealed class SeerBoss
{
    public Vector2 Preset;
    public Vector2 Pos;            // sprite/hitbox top-left (16x24)
    public int Health = 7;
    public int FlickerT;
    public readonly float[] OrbAngle = { 0, 0 };
    public readonly float[] OrbRadius = { 16, 32 };
    public readonly float[] OrbSpeed = { 3f, 2.4f };
    public readonly Vector2[] OrbPos = new Vector2[2];
    public bool OrbsOverride;
    public Vector2? Wave;          // top-left, 128x8
    public bool WaveEnd;
    public Vector2 WaveVel;
    public readonly List<Vector2> Dusts = new();
    float yTimer;
    int hdir;                      // 0: towards +60, 1: towards -50
    int stage = 1;                 // 1, then 2
    int phase;                     // stage 2: 0 get into position, 1 bounce, 2 charge, 3 wave falling
    int vstep;                     // dive iteration 0..7 (up,down pairs)
    int chargeStep;
    public Rectangle Body => new((int)Pos.X, (int)Pos.Y, 16, 24);
    public bool Flickering => FlickerT > 0;
    public bool Vulnerable => !Flickering && Health > 0;
    public Vector2 Center => new(Pos.X + 8, Pos.Y + 12);
    const float Dt = 1f / 60f;

    public SeerBoss(Vector2 preset)
    {
        Preset = preset;
        Pos = preset;
        RotateOrbs();
    }

    static bool MoveTo(ref float v, float target, float speed)
    {
        float step = speed * Dt;
        if (Math.Abs(target - v) <= step) { v = target; return true; }
        v += Math.Sign(target - v) * step;
        return false;
    }

    void RotateOrbs()
    {
        for (int i = 0; i < 2; i++)
        {
            OrbAngle[i] += OrbSpeed[i] * Dt;
            OrbPos[i] = Center + OrbRadius[i] * new Vector2(MathF.Cos(OrbAngle[i]), MathF.Sin(OrbAngle[i]));
        }
    }

    void MoveHorizontal()
    {
        if (hdir == 0) { if (MoveTo(ref Pos.X, Preset.X + 60, 50f)) { hdir = 1; } }
        else { if (MoveTo(ref Pos.X, Preset.X - 50, 50f)) { hdir = 0; } }
    }

    public void Hit()
    {
        if (!Flickering && Health > 0) { Health--; FlickerT = 90; }
    }

    public IEnumerable<Rectangle> OrbBoxes => OrbPos.Select(o => new Rectangle((int)o.X, (int)o.Y, 16, 16));
    public Rectangle? WaveBox => Wave.HasValue && !WaveEnd ? new Rectangle((int)Wave.Value.X, (int)Wave.Value.Y, 128, 8) : null;

    public void Step()
    {
        if (FlickerT > 0) { FlickerT--; }
        if (Health > 6 && stage == 1)
        {
            yTimer += Dt;
            Pos.Y = Preset.Y + 10 * MathF.Sin(yTimer);
            MoveHorizontal();
            RotateOrbs();
            return;
        }
        stage = 2;
        switch (phase)
        {
            case 0:
                RotateOrbs();
                if (MoveTo(ref Pos.X, Preset.X, 90f) && MoveTo(ref Pos.Y, Preset.Y, 12f)) { phase = 1; vstep = 0; hdir = 0; }
                break;
            case 1:
                for (int i = 0; i < 2; i++)
                {
                    MoveTo(ref OrbRadius[i], 8, 30f);
                    MoveTo(ref OrbSpeed[i], 240f, 0.6f);
                }
                RotateOrbs();
                MoveHorizontal();
                // dive loop: 4 x (up to Y-32 at 120, down to Y+80 at 90)
                if (vstep % 2 == 0)
                {
                    if (MoveTo(ref Pos.Y, Preset.Y - 32, 120)) { vstep++; }
                }
                else
                {
                    if (MoveTo(ref Pos.Y, Preset.Y + 80f, 90))
                    {
                        if (vstep / 2 < 3) { Dusts.Add(Pos); }
                        vstep++;
                        if (vstep >= 8) { phase = 2; chargeStep = 0; }
                    }
                }
                break;
            case 2:
                // charge: to (X+30, Y-20) at 60px/s (both axes, non-short-circuit &), orbs gather, then the wave appears
                {
                    bool bx = MoveTo(ref Pos.X, Preset.X + 30, 60f);
                    bool by = MoveTo(ref Pos.Y, Preset.Y - 20, 60f);
                    if (bx & by)
                    {
                        Wave = Preset - new Vector2(50, 10);
                        WaveEnd = false;
                        WaveVel = new Vector2(0, 30f);
                        phase = 3;
                    }
                }
                break;
            case 3:
                MoveHorizontal();
                if (Wave.HasValue)
                {
                    Wave = Wave.Value + WaveVel * Dt;
                    // the wave ends on the first dust it touches (or the floor)
                    var wb = new Rectangle((int)Wave.Value.X, (int)Wave.Value.Y, 128, 8);
                    if (Wave.Value.Y > 160 || Dusts.Any(d => new Rectangle((int)d.X, (int)d.Y, 16, 16).Intersects(wb))) { WaveEnd = true; }
                }
                if (WaveEnd)
                {
                    Dusts.Clear();
                    Wave = null;
                    phase = 0;
                    for (int i = 0; i < 2; i++) { OrbRadius[i] = i == 0 ? 16 : 32; OrbSpeed[i] = i == 0 ? 3f : 2.4f; }
                }
                break;
        }
    }
}
