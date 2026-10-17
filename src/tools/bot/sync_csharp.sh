#!/bin/bash
# Copies the bot sources (new files in the C# checkout) and the hook diff into this repo so they are not only in the
# separate, uncommitted C# working tree.
set -e
S=/home/snake/dev/AnodyneSharp/AnodyneSharp/AnodyneSharp.Shared
cd "$(dirname "$0")"
mkdir -p csharp
# The C# checkout is the source of truth (it is what gets built); csharp/ here is only a copy. Refuse to overwrite a copy that was
# edited here after the checkout's file was last changed, which would silently throw the edit away (FORCE=1 overrides).
for f in Bot BotNav BotBehaviors StartToNexus NexusToRoadway Roadway BlueForest BlueForest2 Temple Fields Beach RedSea SpaceTime FightPolicies; do
    if [ -z "$FORCE" ] && [ -f csharp/$f.cs ] && ! cmp -s csharp/$f.cs $S/Recording/$f.cs && [ csharp/$f.cs -nt $S/Recording/$f.cs ] \
        && ! (cd .. && git diff --quiet HEAD -- bot/csharp/$f.cs 2>/dev/null; [ $? -eq 0 ]); then
        echo "sync_csharp: csharp/$f.cs was edited here and is newer than the checkout's copy: edit $S/Recording/$f.cs instead (or FORCE=1 to overwrite)" >&2
        exit 1
    fi
done
cp $S/Recording/{Bot,BotNav,BotBehaviors,StartToNexus,NexusToRoadway,Roadway,BlueForest,BlueForest2,Temple,Fields,Beach,RedSea,SpaceTime,FightPolicies}.cs csharp/
(cd /home/snake/dev/AnodyneSharp && git diff -- AnodyneSharp/AnodyneSharp.Shared/Recording/RecordingManager.cs AnodyneSharp/AnodyneSharp.Shared/AnodyneGame.cs AnodyneSharp/AnodyneSharp.Multiplatform/Program.cs AnodyneSharp/AnodyneSharp.Shared/States/PlayState.cs AnodyneSharp/AnodyneSharp.Shared/MapData/Map.cs AnodyneSharp/AnodyneSharp.Shared/AnodyneSharp.Shared.projitems) > csharp/hooks.diff
echo "synced: $(ls csharp | wc -l) files"
