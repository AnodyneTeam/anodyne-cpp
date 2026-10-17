#!/bin/bash
# Copies the bot sources (new files in the C# checkout) and the hook diff into this repo so they are not only in the
# separate, uncommitted C# working tree.
set -e
S=/home/snake/dev/AnodyneSharp/AnodyneSharp/AnodyneSharp.Shared
cd "$(dirname "$0")"
mkdir -p csharp
cp $S/Recording/{Bot,BotNav,BotBehaviors,StartToNexus,NexusToRoadway,Roadway,BlueForest,BlueForest2,Temple,Fields,Beach}.cs csharp/
(cd /home/snake/dev/AnodyneSharp && git diff -- AnodyneSharp/AnodyneSharp.Shared/Recording/RecordingManager.cs AnodyneSharp/AnodyneSharp.Shared/AnodyneGame.cs AnodyneSharp/AnodyneSharp.Multiplatform/Program.cs AnodyneSharp/AnodyneSharp.Shared/States/PlayState.cs AnodyneSharp/AnodyneSharp.Shared/MapData/Map.cs AnodyneSharp/AnodyneSharp.Shared/AnodyneSharp.Shared.projitems) > csharp/hooks.diff
echo "synced: $(ls csharp | wc -l) files"
