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
        public bool Swinging;        // the broom is out (a swing in progress: pressing Accept now does nothing)
        public Rectangle T;          // target hitbox
        public Vector2 TV;           // target velocity (px/s)
        public bool Fast;            // a bullet Slime (twice a Slime's speed)
        public Func<Rectangle, bool> Walk = _ => true; // is this player-sized rectangle on floor?
        public Func<Rectangle, bool> Hazard = _ => false; // is this player-sized rectangle in a beam lane (or otherwise a place not to loiter)?
        public System.Collections.Generic.IList<Rectangle> Others = new Rectangle[0]; // the other living enemies on the screen
    }

    /// <summary>Per-fight memory of a policy (swing cooldown etc.).</summary>
    public sealed class PolicyMem
    {
        public int Swing, LastSwing = -1000, VRetreat, DodgeLeft;
        public PKey DodgeKey;
        public Vector2? PrevT;
        public PFacing HoldFace = PFacing.Right;
        public int HoldUntil;
        public System.Collections.Generic.List<Vector2> PrevCentres = new();
        public System.Collections.Generic.Dictionary<int, Vector2> PrevAll = new();
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

        /// <summary>
        /// An Annoyer flies in on a straight line (through us if it comes from the far side) to a spot 20px to our right, then orbits there for
        /// 3s before it swoops. The swing only reaches straight ahead for ~0.36s: line up level with it while it flies in, face it, swing as
        /// it arrives; once it orbits, hold one direction and swing (it passes the broom on most laps).
        /// </summary>
        public static PKey[] AnnoyerHold(PCtx c, PolicyMem m)
        {
            var tc = new Vector2(c.T.Center.X, c.T.Center.Y);
            var pc = new Vector2(c.P.Center.X, c.P.Center.Y);
            float dx = tc.X - pc.X, dy = tc.Y - pc.Y;
            float dist = (float)Math.Sqrt(dx * dx + dy * dy);
            float speed = m.PrevT.HasValue ? (tc - m.PrevT.Value).Length() : 0f;
            m.PrevT = tc;
            bool orbiting = speed > 1.4f && dist < 26;
            bool horiz = Math.Abs(dx) >= Math.Abs(dy);
            var face = (orbiting || dist < 12) ? PKey.Right : horiz ? (dx < 0 ? PKey.Left : PKey.Right) : (dy < 0 ? PKey.Up : PKey.Down);
            var faceF = face == PKey.Left ? PFacing.Left : face == PKey.Right ? PFacing.Right : face == PKey.Up ? PFacing.Up : PFacing.Down;
            float across = horiz ? dy : dx;
            if (!orbiting && dist >= 12 && Math.Abs(across) > 8 && dist > 22)
            {
                // flying in on a diagonal: line up level with it first, so that the swing (which only reaches straight ahead) meets it
                return horiz ? K(dy < 0 ? PKey.Up : PKey.Down) : K(dx < 0 ? PKey.Left : PKey.Right);
            }
            if (c.Facing != faceF)
            {
                return K(face);
            }
            m.Swing++;
            float reach = orbiting ? 34 : 27;
            if (dist < reach && !c.Swinging && m.Swing - m.LastSwing >= 4)
            {
                m.LastSwing = m.Swing;
                return K(PKey.Accept);
            }
            return None;
        }

        /// <summary>
        /// All awake Annoyers at once (ctx.T = the primary, ctx.Others = the rest, in a stable order): one that is still flying in is handled
        /// first (face it, line up, swing as it arrives); when they are all orbiting, hold one direction and swing every lap.
        /// </summary>
        public static PKey[] AnnoyersHold(PCtx c, PolicyMem m)
        {
            // Just broom them: when one is close, face the nearest one, hold that facing for a while (they orbit at 168px/s, so turning to
            // follow one is pointless) and swing on every cooldown; a swing held out to one side meets one on almost every lap.
            var pc = new Vector2(c.P.Center.X, c.P.Center.Y);
            var all = new System.Collections.Generic.List<Rectangle> { c.T };
            all.AddRange(c.Others);
            Rectangle near = c.T;
            float bestD = 1e9f;
            foreach (var a in all)
            {
                float d = (new Vector2(a.Center.X, a.Center.Y) - pc).Length();
                if (d < bestD)
                {
                    bestD = d;
                    near = a;
                }
            }
            m.Swing++;
            // speed of the nearest one: match it to the closest centre of the previous frame (they come in a list, not with identities)
            var nc = new Vector2(near.Center.X, near.Center.Y);
            float speed = 0f;
            float bestPrev = 1e9f;
            foreach (var pv in m.PrevCentres)
            {
                float d = (pv - nc).Length();
                if (d < bestPrev)
                {
                    bestPrev = d;
                }
            }
            if (m.PrevCentres.Count > 0)
            {
                speed = bestPrev;
            }
            m.PrevCentres = all.ConvertAll(a => new Vector2(a.Center.X, a.Center.Y));
            if (bestD > 36)
            {
                if (speed < 0.3f && bestD > 50)
                {
                    // asleep (it wakes when we come within ~64px): walk up to it
                    float wx = near.Center.X - pc.X, wy = near.Center.Y - pc.Y;
                    return Math.Abs(wx) >= Math.Abs(wy) ? K(wx < 0 ? PKey.Left : PKey.Right) : K(wy < 0 ? PKey.Up : PKey.Down);
                }
                return None; // flying in: stay put, it comes to us
            }
            bool orbiting = speed > 1.4f;
            float ddx = near.Center.X - pc.X, ddy = near.Center.Y - pc.Y;
            PFacing want;
            if (orbiting)
            {
                if (m.Swing >= m.HoldUntil)
                {
                    m.HoldFace = Math.Abs(ddx) >= Math.Abs(ddy) ? (ddx < 0 ? PFacing.Left : PFacing.Right) : (ddy < 0 ? PFacing.Up : PFacing.Down);
                    m.HoldUntil = m.Swing + 40;
                }
                want = m.HoldFace;
            }
            else
            {
                // still flying in: face where it comes from (live) and swing as it arrives
                bool curHoriz = c.Facing == PFacing.Left || c.Facing == PFacing.Right;
                bool horizNow = curHoriz ? Math.Abs(ddy) <= Math.Abs(ddx) + 8 : Math.Abs(ddx) > Math.Abs(ddy) + 8; // hysteresis: do not flip axis on a near-diagonal
                want = horizNow ? (ddx < 0 ? PFacing.Left : PFacing.Right) : (ddy < 0 ? PFacing.Up : PFacing.Down);
                m.HoldUntil = 0;
            }
            if (c.Facing != want)
            {
                return K(want == PFacing.Left ? PKey.Left : want == PFacing.Right ? PKey.Right : want == PFacing.Up ? PKey.Up : PKey.Down);
            }
            if ((orbiting || bestD < 27) && !c.Swinging)
            {
                m.LastSwing = m.Swing;
                return K(PKey.Accept);
            }
            return None;
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
                if (c.Hazard(pb) && gap > 2 && Ahead(c, PKey.Down, 2))
                {
                    return K(PKey.Down); // standing in a beam: get through it to the safe side of the swing spot instead of swinging here
                }
                if (c.Facing != PFacing.Down)
                {
                    return gap >= 4 ? K(PKey.Down) : K(PKey.Up);
                }
                if (gap > 9 && Ahead(c, PKey.Down, 2))
                {
                    return K(PKey.Down);
                }
                if (gap <= 12 && !c.Hazard(pb))
                {
                    m.Swing++;
                    if (!c.Swinging && m.Swing - m.LastSwing >= 24)
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
                if (gap >= 12 && gap <= 17 && !c.Swinging && m.Swing - m.LastSwing >= 24)
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
            if (gap >= 3 && gap <= 8 && !c.Swinging && m.Swing - m.LastSwing >= 24)
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
            if (c.Fast)
            {
                // A bullet Slime moves twice as fast: deal with where it will be in a few frames (its touch comes before we react otherwise).
                sb.Offset((int)Math.Round(c.TV.X * 0.15f), (int)Math.Round(c.TV.Y * 0.15f));
            }
            var pb = c.P;
            float dx = sb.Center.X - pb.Center.X, dy = sb.Center.Y - pb.Center.Y;
            int gx = Math.Max(sb.Left - pb.Right, pb.Left - sb.Right);
            int gy = Math.Max(sb.Top - pb.Bottom, pb.Top - sb.Bottom);
            if (Math.Max(gx, gy) < 4)
            {
                var awayNow = gx >= gy ? (dx < 0 ? PKey.Right : PKey.Left) : (dy < 0 ? PKey.Down : PKey.Up);
                if (Math.Max(gx, gy) < 1 || Ahead(c, awayNow, 4))
                {
                    return K(awayNow);
                }
                // pinned against a wall with it right there: backing off is not possible, so deal with it from where we are
            }
            // Another one drifting into reach while we deal with this one: step away from it first.
            foreach (var o in c.Others)
            {
                int ox = Math.Max(o.Left - pb.Right, pb.Left - o.Right);
                int oy = Math.Max(o.Top - pb.Bottom, pb.Top - o.Bottom);
                if (Math.Max(ox, oy) < 6)
                {
                    var awayO = ox >= oy ? (o.Center.X < pb.Center.X ? PKey.Right : PKey.Left) : (o.Center.Y < pb.Center.Y ? PKey.Down : PKey.Up);
                    if (Ahead(c, awayO, 4))
                    {
                        return K(awayO);
                    }
                }
            }
            bool horiz = Math.Abs(dx) >= Math.Abs(dy);
            float along = horiz ? dx : dy, across = horiz ? dy : dx;
            float gap = horiz ? gx : gy; // (edge to edge: a Gas Guy is 24px tall, not a Slime's 16)
            var face = horiz ? (dx < 0 ? PKey.Left : PKey.Right) : (dy < 0 ? PKey.Up : PKey.Down);
            var away = horiz ? (dx < 0 ? PKey.Right : PKey.Left) : (dy < 0 ? PKey.Down : PKey.Up);
            var side = horiz ? (dy < 0 ? PKey.Up : PKey.Down) : (dx < 0 ? PKey.Left : PKey.Right);
            m.Swing++;
            if (Math.Abs(across) <= 7)
            {
                if (gap < 4 && (gap < 1 || Ahead(c, away, 4)))
                {
                    return K(away); // room to back off: do
                }
                if (gap <= 9 || gap < 4)
                {
                    if (!c.Swinging && m.Swing - m.LastSwing >= 4)
                    {
                        var want = face == PKey.Up ? PFacing.Up : face == PKey.Down ? PFacing.Down : face == PKey.Left ? PFacing.Left : PFacing.Right;
                        if (c.Facing != want)
                        {
                            // turn first (a swing pressed with the direction goes the old way); the turn nudges us ~1px towards it, so
                            // only with 3px to spare, else wait for it to drift off
                            if (gap >= 3)
                            {
                                return K(face);
                            }
                            // pinned: slip sideways along the wall so it is no longer straight above/below us
                            var slip = across <= 0 ? PKey.Right : PKey.Left;
                            if (!Ahead(c, slip, 4))
                            {
                                slip = slip == PKey.Right ? PKey.Left : PKey.Right;
                            }
                            return Ahead(c, slip, 4) ? K(slip) : None;
                        }
                        m.LastSwing = m.Swing;
                        return K(PKey.Accept);
                    }
                    return None;
                }
                return K(face);
            }
            return gap < 6 ? K(away) : K(side);
        }
    }
}
