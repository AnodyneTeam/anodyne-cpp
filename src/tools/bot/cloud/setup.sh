#!/bin/bash
# Sets up a headless Linux box (e.g. a Claude Code cloud session) to run the bot without the private AnodyneSharp checkout:
# clones upstream AnodyneSharp, applies glue.diff (a reconstruction of the recording/bot hooks that only exist in the
# unpublished checkout), fix16.diff (the determinism hooks and the gameplay maths in Fix16, matching the port's fx) and
# wide.diff (the wide view: a 240px game screen, the graphics menu's "Wide view" or ANODYNE_WIDE=1), copies the bot sources from ../csharp, compiles the shaders under wine and builds the game.
# usage: setup.sh [dest]   (default /home/snake/dev/AnodyneSharp, the path every other bot script expects)
# Afterwards: start Xvfb + pulseaudio (headless.sh) and use run_bot.sh / from.sh / part.sh as usual.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DEST=${1:-/home/snake/dev/AnodyneSharp}
# The upstream commit glue.diff was made against.
BASE=0b038d0be87b740578ab55061add0a3098e8c71f

# (a fresh container's package lists are empty)
sudo apt-get update -qq >/dev/null
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y xvfb x11-utils libgl1-mesa-dri libglx-mesa0 libopenal1 libsdl2-2.0-0 \
    pulseaudio wine64 wine p7zip-full >/dev/null

if [ ! -x "$HOME/.dotnet/dotnet" ]; then
    curl -sSL https://dot.net/v1/dotnet-install.sh -o /tmp/dotnet-install.sh
    bash /tmp/dotnet-install.sh --channel 10.0 --install-dir "$HOME/.dotnet"
fi
export DOTNET_ROOT=$HOME/.dotnet PATH=$HOME/.dotnet:$PATH DOTNET_CLI_TELEMETRY_OPTOUT=1

if [ ! -d "$DEST/.git" ]; then
    mkdir -p "$(dirname "$DEST")"
    git clone https://github.com/PixieCatSupreme/AnodyneSharp "$DEST"
fi
cd "$DEST"
git checkout -q -B bot $BASE
git submodule update --init
git apply "$HERE/glue.diff"
git apply "$HERE/fix16.diff"
git apply "$HERE/wide.diff"
sed -i 's#<TargetFramework>net6.0</TargetFramework>#<TargetFramework>net10.0</TargetFramework>#' Fluent-State-Machine/*.csproj
mkdir -p AnodyneSharp/AnodyneSharp.Shared/Recording
cp "$HERE"/../csharp/*.cs "$HERE"/RecordingManager.cs AnodyneSharp/AnodyneSharp.Shared/Recording/

# MonoGame 3.8.1 compiles effects with mgfxc under wine: a 64-bit prefix with the x64 .NET 6 runtime and d3dcompiler_47.
export WINEARCH=win64 WINEPREFIX=$HOME/.winemonogame MGFXC_WINE_PATH=$HOME/.winemonogame
WINEBIN=$HOME/.winemonogame-bin
mkdir -p "$WINEBIN"
ln -sf /usr/lib/wine/wine64 "$WINEBIN/wine64"
export PATH=$WINEBIN:$PATH
if [ ! -f "$WINEPREFIX/drive_c/windows/system32/d3dcompiler_47.dll" ]; then
    T=$(mktemp -d)
    wine64 wineboot >/dev/null 2>&1
    curl -sSL -o "$T/dn.zip" https://builds.dotnet.microsoft.com/dotnet/Runtime/6.0.36/dotnet-runtime-6.0.36-win-x64.zip
    7z x -y "$T/dn.zip" -o"$WINEPREFIX/drive_c/windows/system32/" >/dev/null
    curl -sSL -o "$T/ff.exe" "https://download-installer.cdn.mozilla.net/pub/firefox/releases/62.0.3/win64/ach/Firefox%20Setup%2062.0.3.exe"
    7z x -y "$T/ff.exe" -o"$T/ff" >/dev/null
    cp "$T/ff/core/d3dcompiler_47.dll" "$WINEPREFIX/drive_c/windows/system32/"
    rm -rf "$T"
fi

cd AnodyneSharp/AnodyneSharp.Multiplatform
dotnet build -p:Optimize=true 2>&1 | grep -E " error |rror\(s\)" | sort -u
echo "built: $DEST/AnodyneSharp/AnodyneSharp.Multiplatform/bin/Debug/net10.0/AnodyneSharp"
