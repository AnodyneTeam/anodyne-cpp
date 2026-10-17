#!/bin/bash
# usage: scene.sh <savedir> "<tokens>" [seconds]    (runs in a visible window, unlocked speed)
#   Copies a saved game (a dir such as /tmp/bot/save_f2, made with ANODYNE_BOT_SAVEDIR + the V token), boots into it and runs
#   the bot tokens, e.g. "wBEDROOM:370,528 O1 X0 H1,2" (w<MAP>:<x>,<y> warps straight into a map position). Prints the debug log
#   and a damage summary, so a problem screen can be tried in seconds instead of replaying the whole route.
HERE=$(cd "$(dirname "$0")" && pwd)
SAVE=$1; TOK=$2; SECS=${3:-120}
D=$(mktemp -d /tmp/bot/scene_XXXXXX); cp -r "$SAVE"/. "$D"/
export ANODYNE_BOT_ALLOW_DAMAGE=1
export DISPLAY=${DISPLAY:-:0}; FAST=-fast   # always the real display (never headless), unlocked speed
export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$PATH
cd /home/snake/dev/AnodyneSharp/AnodyneSharp/AnodyneSharp.Multiplatform/bin/Debug/net10.0
ANODYNE_BOT_SAVEDIR=$D ANODYNE_BOT_DEBUG=1 ANODYNE_BOT_KEYS="$TOK" timeout -k 5 "$SECS" ./AnodyneSharp $FAST -bot=keys -trace="$D/trace.tsv" 2>&1 | grep -av "AL lib" | cut -c1-260
python3 "$HERE/damage_report.py" "$D/trace.tsv"
[ -n "$KEEP" ] && cp "$D/trace.tsv" "$KEEP"; rm -rf "$D"
