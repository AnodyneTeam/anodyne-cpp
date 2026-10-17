#!/bin/bash
# usage: next.sh <part> : sync+commit sources, run the chain with section snapshots, show where it fails
cd "$(dirname "$0")"
./sync_csharp.sh >/dev/null
(cd ../../.. && git add src/tools/bot && git commit -qm "Bot: ${MSG:-wip}

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Mx4qWC5cPZhEisrvCdHVzj" >/dev/null 2>&1)
rm -rf /tmp/bot/sd /tmp/bot/sec; mkdir -p /tmp/bot/sd /tmp/bot/sec
ANODYNE_BOT_SAVEDIR=/tmp/bot/sd ANODYNE_BOT_SECSAVE=/tmp/bot/sec ANODYNE_BOT_DEBUG=1 timeout 3500 ./run_bot.sh ${1:-temple1} /tmp/bot/g_next 2>&1 | grep -aE "BOT FAILED|PLAYBACK|HIT" | tail -3 | cut -c1-300
ls /tmp/bot/sec | tail -2
