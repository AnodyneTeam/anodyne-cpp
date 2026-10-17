#!/bin/bash
# Rebuilds the C# game (optimised: the bot's planners are slow in an unoptimised build) when any source is newer than the binary,
# so a bot run never uses a stale build. Exits 3 on a build error. Source this or run it before starting the game.
SRC=/home/snake/dev/AnodyneSharp/AnodyneSharp
CONF=${CONF:-Debug}
DLL=$SRC/AnodyneSharp.Multiplatform/bin/$CONF/net10.0/AnodyneSharp.dll
export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$PATH
if [ ! -f "$DLL" ] || [ -n "$(find $SRC/AnodyneSharp.Shared -name '*.cs' -newer $DLL 2>/dev/null | head -1)" ]; then
    echo "rebuilding (sources newer than the binary)"
    mkdir -p /tmp/bot
    (cd $SRC/AnodyneSharp.Multiplatform && dotnet build -c $CONF -p:Optimize=true 2>&1 | grep -E " error |rror\(s\)" | sort -u | head -5 | tee /tmp/bot/build.log)
    if grep -q " error " /tmp/bot/build.log; then echo "BUILD FAILED"; exit 3; fi
fi
