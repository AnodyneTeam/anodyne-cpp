#!/bin/bash
# usage: [TOKENS='explicit tokens'] from.sh <section index> [extra tokens]   (TOKENS replaces the chain: a short dev script run from the snapshot)
#   Starts from the saved snapshot of section N (made by next.sh: /tmp/bot/sec/NN_*) and runs the bodies of sections N..end in order,
#   FATAL on the first damage/stuck (no replay of the earlier route). Shows the failure and the last frames (/tmp/bot/fail_ring.txt).
HERE=$(cd "$(dirname "$0")" && pwd)
N=$1; shift
if [[ "$N" =~ ^[0-9]+$ ]]; then SNAP=$(ls -d /tmp/bot/sec/*$(printf %02d $N)_* 2>/dev/null | head -1); else SNAP=/tmp/bot/sec/$N; N=0; fi; [ -d "$SNAP" ] || { echo "no snapshot for section $N (run next.sh first)"; exit 1; }
TOK="W30 O1 ${TOKENS:-$(SNAPS=${SNAPS:-0} python3 "$HERE/section_chain.py" $N)} $*"
D=$(mktemp -d /tmp/bot/from_XXXXXX); cp -r "$SNAP"/. "$D"/
export DISPLAY=${DISPLAY:-:0} DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$PATH
"$HERE/ensure_build.sh" || exit 3
SRC=/home/snake/dev/AnodyneSharp/AnodyneSharp
cd $SRC/AnodyneSharp.Multiplatform/bin/${CONF:-Debug}/net10.0
ANODYNE_BOT_SECSAVE=/tmp/bot/sec ANODYNE_BOT_SAVEDIR=$D ANODYNE_BOT_KEYS="$TOK" timeout -k 5 ${SECS:-900} ./AnodyneSharp -fast -bot=keys -trace="$D/trace.tsv" 2>&1 | tee ${FROMLOG:-/tmp/bot/from.log} | grep -aE "BOT FAILED|HIT near" | cut -c1-300
echo "frames: $(wc -l < $D/trace.tsv)  last: $(tail -1 $D/trace.tsv | cut -f2-6)"
[ -n "$KEEPTRACE" ] && cp $D/trace.tsv /tmp/bot/last_from_trace.tsv; rm -rf "$D"
