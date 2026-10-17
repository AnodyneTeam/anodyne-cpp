#!/bin/bash
# usage: try.sh <save dir> "<tokens>"  - like scene.sh but damage is FATAL and the last frames are dumped to /tmp/bot/fail_ring.txt
SAVE=$1; TOK=$2
D=$(mktemp -d /tmp/bot/try_XXXXXX); cp -r "$SAVE"/. "$D"/
export DISPLAY=${DISPLAY:-:0} DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$PATH
cd /home/snake/dev/AnodyneSharp/AnodyneSharp/AnodyneSharp.Multiplatform/bin/${CONF:-Debug}/net10.0
ANODYNE_BOT_SAVEDIR=$D ANODYNE_BOT_KEYS="$TOK" timeout -k 5 ${SECS:-300} ./AnodyneSharp -fast -bot=keys -trace="$D/trace.tsv" 2>&1 | grep -aE "BOT FAILED" | cut -c1-300
echo "frames: $(wc -l < $D/trace.tsv)"
rm -rf "$D"
