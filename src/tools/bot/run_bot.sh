#!/bin/bash
# usage: [SHOW=1 DISPLAY=:0] run_bot.sh <script> <outdir> [realtime]
#   SHOW=1 shows the bot run on the real display; the determinism replay always runs on the private Xvfb.
#   Runs a bot script in the C# AnodyneSharp (headless on a private Xvfb), recording to <outdir>:
#   <script>.rec (C# input recording), .gbainput (GBA button runs), .trace.tsv (per-frame map/pos/hp/dialogue).
#   Then plays the .rec back with no bot and diffs the traces (determinism check).
set -e
SCRIPT=$1; OUT=$2; RT=$3
mkdir -p "$OUT"
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -n "$SHOW" ]; then export DISPLAY=${DISPLAY:-:0}; else export DISPLAY=$("$HERE/xvfb.sh"); fi
export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$PATH
cd /home/snake/dev/AnodyneSharp/AnodyneSharp/AnodyneSharp.Multiplatform/bin/Debug/net10.0
FAST=-fast; [ -n "$RT" ] && FAST=
./AnodyneSharp $FAST -bot="$SCRIPT" -record="$OUT/$SCRIPT.rec" -gbainput="$OUT/$SCRIPT.gbainput" -trace="$OUT/$SCRIPT.trace.tsv" 2>&1 | grep -v "AL lib" | tee "$OUT/bot.log" || true
# A failed bot run leaves a recording that cannot replay (it would only sit on the title screen): skip the determinism replay.
if grep -q "BOT FAILED" "$OUT/bot.log"; then echo "BOT RUN FAILED: replay skipped"; exit 2; fi
DISPLAY=$("$HERE/xvfb.sh") ./AnodyneSharp -fast -playback="$OUT/$SCRIPT.rec" -trace="$OUT/$SCRIPT.play.tsv" 2>&1 | grep -v "AL lib" || true
N=$(wc -l < "$OUT/$SCRIPT.trace.tsv")
if head -"$N" "$OUT/$SCRIPT.play.tsv" | diff -q - "$OUT/$SCRIPT.trace.tsv" >/dev/null; then
    echo "PLAYBACK OK: $((N-1)) frames identical"
else
    echo "PLAYBACK DIVERGED"; head -"$N" "$OUT/$SCRIPT.play.tsv" | diff - "$OUT/$SCRIPT.trace.tsv" | head; exit 1
fi
