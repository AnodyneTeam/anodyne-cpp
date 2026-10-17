#!/bin/bash
# usage: snapfrom.sh <section N> <frame> <outdir>: run sections N.. from their snapshot and save the game at <frame> into <outdir>
HERE=$(cd "$(dirname "$0")" && pwd)
N=$1; AT=$2; OUT=$3
SNAP=$(ls -d /tmp/bot/sec/$(printf %02d $N)_* 2>/dev/null | head -1); [ -d "$SNAP" ] || { echo "no snapshot for section $N (run next.sh first)"; exit 1; }
D=$(mktemp -d /tmp/bot/snapf_XXXXXX); cp -r "$SNAP"/. "$D"/
export DISPLAY=${DISPLAY:-:0} DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$PATH
cd /home/snake/dev/AnodyneSharp/AnodyneSharp/AnodyneSharp.Multiplatform/bin/${CONF:-Debug}/net10.0
rm -rf "$OUT"
ANODYNE_BOT_SAVEDIR=$D ANODYNE_BOT_SNAP_AT=$AT:$OUT ANODYNE_BOT_KEYS="W30 $(python3 $HERE/section_chain.py $N)" timeout -k 5 600 ./AnodyneSharp -fast -bot=keys -trace=$D/trace.tsv 2>&1 | grep -aE "BOT FAILED" | cut -c1-200
rm -rf "$D"; ls "$OUT" 2>/dev/null | head -3
