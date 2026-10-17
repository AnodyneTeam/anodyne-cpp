#!/bin/bash
# Macro-level regression suite: the planner unit tests, then every scene in scenes/scenes.txt on the real display (never headless).
# A scene passes with 0 health lost and no "BOT FAILED". usage: regress.sh [name-filter]
HERE=$(cd "$(dirname "$0")" && pwd)
export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$PATH
fail=0
echo "== planner unit tests"
(cd "$HERE/planner_test" && dotnet run -c Release 2>&1 | tail -12) || fail=1
dotnet_status=$?
echo "== scenes"
grep -v '^#' "$HERE/scenes/scenes.txt" | while IFS='|' read -r name save tokens secs; do
    name=$(echo $name); save=$(echo $save); tokens=$(echo $tokens); secs=$(echo $secs)
    [ -n "$1" ] && [[ "$name" != *$1* ]] && continue
    out=$("$HERE/scene.sh" "$HERE/scenes/$save" "$tokens" "$secs" 2>&1)
    lost=$(echo "$out" | grep -a "health lost" | sed -E 's/.*: ([0-9]+) health lost.*/\1/' | head -1)
    if echo "$out" | grep -aq "BOT FAILED" || [ "${lost:-x}" != "0" ]; then
        echo "FAIL  $name (lost=${lost:-?}) $(echo "$out" | grep -a 'BOT FAILED\|first hit' | head -2 | tr '\n' ' ')"
        echo FAIL >> /tmp/bot/regress_fail.flag
    else
        echo "PASS  $name"
    fi
done
[ -f /tmp/bot/regress_fail.flag ] && { rm -f /tmp/bot/regress_fail.flag; echo "REGRESSIONS"; exit 1; }
echo "ALL PASS"
