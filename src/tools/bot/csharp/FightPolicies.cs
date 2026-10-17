using Microsoft.Xna.Framework;
using System;

namespace AnodyneSharp.Recording
{
    public enum PFacing { Up, Down, Left, Right }
    public enum PKey { Up, Down, Left, Right, Accept }

    /// <summary>What a fight policy needs to know (plain data, so the policies can be unit-tested against a simulated room).</summary>
    public sealed class PCtx
    {
        public Rectangle P;          // player hitbox
        public PFacing Facing;
        public Rectangle T;          // target hitbox
        public Vector2 TV;           // target velocity (px/s)
        public Func<Rectangle, bool> Walk = _ => true; // is this player-sized rectangle on floor?
    }

    /// <summary>Per-fight memory of a policy (swing cooldown etc.).</summary>
    public sealed class PolicyMem
    {
        public int Swing, LastSwing = -1000, VRetreat, DodgeLeft;
        public PKey DodgeKey;
    }

    /// <summary>
    /// The fight tactics for single enemies, as pure functions: given the situation they return the keys for this frame (an empty
    /// array = stand still) or null when the tactic does not apply (the caller then falls back to its other logic).
    /// </summary>
    public static class FightPolicies
    {
        public const int SwingCooldown = 18;
        private static readonly PKey[] None = new PKey[0];
        private static PKey[] K(params PKey[] k) => k;

        private static bool Ahead(PCtx c, PKey dir, int frames)
        {
            float v = 70f / 60f * frames;
            var r = c.P;
            r.Offset(dir == PKey.Left ? (int)-v : dir == PKey.Right ? (int)v : 0, dir == PKey.Up ? (int)-v : dir == PKey.Down ? (int)v : 0);
            return c.Walk(r);
        }

        /// <summary>A Shieldy that stands still dies to two downward swings: when we are north of it, line up, come down, turn down, swing.</summary>
        public static PKey[] BoomStationary(PCtx c, PolicyMem m)
        {
            var pb = c.P;
            var tb = c.T;
            int gap = tb.Top - pb.Bottom;
            float adx = tb.Center.X - pb.Center.X;
            if (!(pb.Center.Y < tb.Center.Y && gap <= 80))
            {
                return null;
            }
            if (Math.Abs(adx) <= 6)
            {
                if (gap < 2)
                {
                    return K(PKey.Up);
                }
                if (c.Facing != PFacing.Down)
                {
                    return gap >= 4 ? K(PKey.Down) : K(PKey.Up);
                }
                if (gap > 9 && Ahead(c, PKey.Down, 2))
                {
                    return K(PKey.Down);
                }
                if (gap <= 12)
                {
                    m.Swing++;
                    if (m.Swing - m.LastSwing >= SwingCooldown + 8)
                    {
                        m.LastSwing = m.Swing;
                        return K(PKey.Accept);
                    }
                    return None;
                }
                return null;
            }
            var slide = adx < 0 ? PKey.Left : PKey.Right;
            if (gap < 2)
            {
                return K(PKey.Up);
            }
            return Ahead(c, slide, 3) ? K(slide) : null;
        }

        private static PKey AwaySide(PCtx c, Rectangle pb, float adx)
        {
            // the side we are already on (adx < 0: it is to our left, so go right); the other one if that is a wall
            var first = adx <= 0 ? PKey.Right : PKey.Left;
            var r = pb;
            r.Offset(first == PKey.Right ? 14 : -14, 0);
            if (c.Walk(r))
            {
                return first;
            }
            return first == PKey.Right ? PKey.Left : PKey.Right;
        }

        /// <summary>A Shieldy walking up and down a column: wait above it facing down, swing when it is in reach, back off between swings.</summary>
        public static PKey[] VerticalShieldy(PCtx c, PolicyMem m)
        {
            var pb = c.P;
            var tb = c.T;
            int gap = tb.Top - pb.Bottom;
            float adx = tb.Center.X - pb.Center.X;
            if (m.DodgeLeft > 0)
            {
                m.DodgeLeft--;
                if (Math.Abs(adx) > 15)
                {
                    m.DodgeLeft = 0; // clear of the column
                    return None;
                }
                return K(m.DodgeKey); // stepping out of its column while it goes by
            }
            if (gap < -2)
            {
                // level with or above us (it is turning round at the wall behind us): keep out of its column until it has gone by
                if (Math.Abs(adx) > 14)
                {
                    return None;
                }
                m.DodgeKey = AwaySide(c, pb, adx);
                m.DodgeLeft = 60;
                return K(m.DodgeKey);
            }
            if (Math.Abs(adx) > 4)
            {
                if (c.TV.Y < 0 && gap < 45)
                {
                    // it is coming up the column: do not step back into its path, and do not stand half in it either
                    if (Math.Abs(adx) < 13)
                    {
                        var awayK = adx < 0 ? PKey.Right : PKey.Left;
                        var ar = pb;
                        ar.Offset(adx < 0 ? 2 : -2, 0);
                        return c.Walk(ar) ? K(awayK) : None;
                    }
                    return None;
                }
                var side = adx < 0 ? PKey.Left : PKey.Right;
                var moved = pb;
                moved.Offset(adx < 0 ? -2 : 2, 0);
                return gap < 10 ? K(PKey.Up) : (c.Walk(moved) ? K(side) : None);
            }
            bool approaching = c.TV.Y < 0;
            if (!approaching && c.TV.Y > 0)
            {
                // Walking away, but about to hit the wall at the end of its beat and come back at us during the swing: treat it as coming.
                for (int d = 2; d <= 24; d += 2)
                {
                    var probe = tb;
                    probe.Offset(0, d);
                    if (!c.Walk(probe))
                    {
                        approaching = true;
                        break;
                    }
                }
            }
            if (m.VRetreat > 0)
            {
                m.VRetreat--;
                return K(PKey.Up);
            }
            // The player stands still for the whole ~22-frame swing and the broom only reaches ~14px by its 3rd frame: against one that is
            // coming up the column the swing must start 12-16px away (it closes 11px meanwhile), against one walking away 4-10px is fine.
            var backRect = pb;
            backRect.Offset(0, -14);
            bool canBack = c.Walk(backRect);
            PKey[] Dodge()
            {
                // cornered against the wall behind us: step out of its column instead
                m.DodgeKey = AwaySide(c, pb, adx);
                m.DodgeLeft = 60;
                return K(m.DodgeKey);
            }
            if (c.Facing != PFacing.Down)
            {
                if (gap >= (approaching ? 14 : 5))
                {
                    return K(PKey.Down);
                }
                return (canBack || !approaching) ? K(PKey.Up) : Dodge();
            }
            if (approaching)
            {
                if (gap >= 12 && gap <= 17 && m.Swing - m.LastSwing >= SwingCooldown + 8)
                {
                    m.Swing++;
                    m.LastSwing = m.Swing;
                    m.VRetreat = canBack ? 16 : 0;
                    return K(PKey.Accept);
                }
                m.Swing++;
                if (gap >= 12)
                {
                    return None;
                }
                return canBack ? K(PKey.Up) : Dodge();
            }
            if (gap >= 3 && gap <= 8 && m.Swing - m.LastSwing >= SwingCooldown + 8)
            {
                m.Swing++;
                m.LastSwing = m.Swing;
                return K(PKey.Accept);
            }
            m.Swing++;
            return gap < 3 ? K(PKey.Up) : (gap > 8 ? K(PKey.Down) : None);
        }

        /// <summary>
        /// A slime drifts at under 10px/s and hurts on contact (its goo is harmless): line up on the broom's axis, stop with a gap and swing
        /// from there; back off if it drifts into reach.
        /// </summary>
        public static PKey[] Slime(PCtx c, PolicyMem m)
        {
            var sb = c.T;
            var pb = c.P;
            float dx = sb.Center.X - pb.Center.X, dy = sb.Center.Y - pb.Center.Y;
            int gx = Math.Max(sb.Left - pb.Right, pb.Left - sb.Right);
            int gy = Math.Max(sb.Top - pb.Bottom, pb.Top - sb.Bottom);
            if (Math.Max(gx, gy) < 4)
            {
                return K(gx >= gy ? (dx < 0 ? PKey.Right : PKey.Left) : (dy < 0 ? PKey.Down : PKey.Up));
            }
            bool horiz = Math.Abs(dx) >= Math.Abs(dy);
            float along = horiz ? dx : dy, across = horiz ? dy : dx;
            float gap = Math.Abs(along) - (horiz ? 13 : 14);
            var face = horiz ? (dx < 0 ? PKey.Left : PKey.Right) : (dy < 0 ? PKey.Up : PKey.Down);
            var away = horiz ? (dx < 0 ? PKey.Right : PKey.Left) : (dy < 0 ? PKey.Down : PKey.Up);
            var side = horiz ? (dy < 0 ? PKey.Up : PKey.Down) : (dx < 0 ? PKey.Left : PKey.Right);
            m.Swing++;
            if (Math.Abs(across) <= 7)
            {
                if (gap < 4)
                {
                    return K(away);
                }
                if (gap <= 9)
                {
                    if (m.Swing - m.LastSwing >= SwingCooldown)
                    {
                        m.LastSwing = m.Swing;
                        return K(face, PKey.Accept);
                    }
                    return None;
                }
                return K(face);
            }
            return gap < 6 ? K(away) : K(side);
        }
    }
}
