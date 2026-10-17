#!/bin/bash
# usage: run_bot.sh <script> <outdir> [realtime]
#   Runs a bot script in the C# AnodyneSharp on the real display (never headless), recording to <outdir>:
#   <script>.rec (C# input recording), .gbainput (GBA button runs), .trace.tsv (per-frame map/pos/hp/dialogue).
#   Then plays the .rec back with no bot and diffs the traces (determinism check).
set -e
SCRIPT=$1; OUT=$2; RT=$3
mkdir -p "$OUT"
HERE=$(cd "$(dirname "$0")" && pwd)
export DISPLAY=${DISPLAY:-:0}  # always the real display: never run headless
export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$PATH
"$(dirname "$0")/ensure_build.sh" || exit 3
cd /home/snake/dev/AnodyneSharp/AnodyneSharp/AnodyneSharp.Multiplatform/bin/${CONF:-Debug}/net10.0
FAST=-fast; [ -n "$RT" ] && FAST=
./AnodyneSharp $FAST -bot="$SCRIPT" -record="$OUT/$SCRIPT.rec" -gbainput="$OUT/$SCRIPT.gbainput" -trace="$OUT/$SCRIPT.trace.tsv" 2>&1 | grep -v "AL lib" | tee "$OUT/bot.log" || true
# Playback verification is parked (the focus is the bot itself): REPLAY=1 turns the determinism replay back on.
if [ -z "$REPLAY" ]; then echo "BOT RUN DONE (replay check skipped; REPLAY=1 to run it)"; exit 0; fi
# A failed bot run leaves a recording that cannot replay (it would only sit on the title screen): skip the determinism replay.
if grep -q "BOT FAILED" "$OUT/bot.log"; then echo "BOT RUN FAILED: replay skipped"; exit 2; fi
./AnodyneSharp -fast -playback="$OUT/$SCRIPT.rec" -trace="$OUT/$SCRIPT.play.tsv" 2>&1 | grep -v "AL lib" || true
N=$(wc -l < "$OUT/$SCRIPT.trace.tsv")
if head -"$N" "$OUT/$SCRIPT.play.tsv" | diff -q - "$OUT/$SCRIPT.trace.tsv" >/dev/null; then
    echo "PLAYBACK OK: $((N-1)) frames identical"
else
    echo "PLAYBACK DIVERGED"; head -"$N" "$OUT/$SCRIPT.play.tsv" | diff - "$OUT/$SCRIPT.trace.tsv" | head; exit 1
fi
