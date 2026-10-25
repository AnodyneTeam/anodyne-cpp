#!/bin/bash
# usage: part.sh <part> <from snapshot dir> <to snapshot name>
#   Runs one route part from the game saved in <from> (the end of the previous part) and leaves the game as it ends in
#   /tmp/bot/keep/<to> (the start of the next part). Prints the failure, the hits taken and the frame count.
HERE=$(cd "$(dirname "$0")" && pwd)
PART=$1; FROM=$2; TO=$3
rm -rf /tmp/bot/sd_$PART /tmp/bot/sec/$TO; mkdir -p /tmp/bot/sec /tmp/bot/keep
cp -r "$FROM" /tmp/bot/sd_$PART
ANODYNE_BOT_ONLY=1 ANODYNE_BOT_SAVEDIR=/tmp/bot/sd_$PART ANODYNE_BOT_SECSAVE=/tmp/bot/sec ANODYNE_BOT_END_SNAP=$TO ANODYNE_BOT_DEBUG=1 SECS=${SECS:-2400} "$HERE/run_bot.sh" $PART /tmp/bot/g_$PART 2>&1 | grep -aE "BOT FAILED|step chain|rror" | head -4 | cut -c1-250
grep -a "  hit f\|boss hit" /tmp/bot/g_$PART/bot.log | cut -c1-150 | tail -6
echo "frames: $(wc -l < /tmp/bot/g_$PART/$PART.trace.tsv)  last: $(tail -1 /tmp/bot/g_$PART/$PART.trace.tsv | cut -f2-6)"
[ -d /tmp/bot/sec/$TO ] && rm -rf /tmp/bot/keep/$TO && cp -r /tmp/bot/sec/$TO /tmp/bot/keep/$TO && echo "end state kept as /tmp/bot/keep/$TO"
