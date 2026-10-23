#!/bin/bash
# usage: chain.sh [first-part]  : runs the parts in order, each from the end state of the one before (the first from a new game)
cd /home/user/anodyne-cpp/src/tools/bot && source cloud/headless.sh >/dev/null
PARTS=(nexus roadway overworld1 temple temple1 forest2 fields beach beach2 beach3 redsea redcave redcave2 redcave3 redcave4 cliff crowd windmill temple2 suburb apartment hotel hotel2 hotel3 hotel4 hotel5 hotel6 cards circus circusb circus2 circus3 cards2 endgame postgame)
start=${1:-nexus}; go=0; prev=""
for i in "${!PARTS[@]}"; do
  p=${PARTS[$i]}
  [ "$p" = "$start" ] && go=1
  if [ $go = 0 ]; then prev=$p; continue; fi
  echo "== $p $(date +%H:%M)"
  if [ $i = 0 ]; then
    rm -rf /tmp/bot/sd_nexus /tmp/bot/sec/end_nexus; mkdir -p /tmp/bot/sd_nexus /tmp/bot/sec
    ANODYNE_BOT_SAVEDIR=/tmp/bot/sd_nexus ANODYNE_BOT_SECSAVE=/tmp/bot/sec ANODYNE_BOT_END_SNAP=end_nexus ANODYNE_BOT_DEBUG=1 timeout 3000 ./run_bot.sh nexus /tmp/bot/g_nexus 2>&1 | grep -aE "BOT FAILED" | head -2
    rm -rf /tmp/bot/keep/end_nexus; [ -d /tmp/bot/sec/end_nexus ] && cp -r /tmp/bot/sec/end_nexus /tmp/bot/keep/end_nexus
  else
    SECS=6000 ./part.sh $p /tmp/bot/keep/end_$prev end_$p
  fi
  if [ ! -d /tmp/bot/keep/end_$p ] || grep -aq "BOT FAILED" /tmp/bot/g_$p/bot.log; then echo "CHAIN STOPPED at $p"; exit 1; fi
  prev=$p
done
echo "CHAIN DONE"
