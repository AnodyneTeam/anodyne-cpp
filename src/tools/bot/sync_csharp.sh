#!/bin/bash
# Copies the bot sources (new files in the C# checkout) and the hook diff into this repo so they are not only in the
# separate, uncommitted C# working tree.
set -e
S=/home/snake/dev/AnodyneSharp/AnodyneSharp/AnodyneSharp.Shared
cd "$(dirname "$0")"
mkdir -p csharp
# The C# checkout is the source of truth (it is what gets built); csharp/ here is only a copy. Refuse to overwrite a copy that was
# edited here after the checkout's file was last changed, which would silently throw the edit away (FORCE=1 overrides).
for f in Bot BotNav BotBehaviors StartToNexus NexusToRoadway Roadway BlueForest BlueForest2 Temple Fields Beach RedSea RedCave SpaceTime FightPolicies SeerPolicy GameView IGameView SimGameView; do
    if [ -z "$FORCE" ] && [ -f csharp/$f.cs ] && ! cmp -s csharp/$f.cs $S/Recording/$f.cs && [ csharp/$f.cs -nt $S/Recording/$f.cs ] \
        && ! (cd .. && git diff --quiet HEAD -- bot/csharp/$f.cs 2>/dev/null; [ $? -eq 0 ]); then
        echo "sync_csharp: csharp/$f.cs was edited here and is newer than the checkout's copy: edit $S/Recording/$f.cs instead (or FORCE=1 to overwrite)" >&2
        exit 1
    fi
done
cp $S/Recording/{Bot,BotNav,BotBehaviors,StartToNexus,NexusToRoadway,Roadway,BlueForest,BlueForest2,Temple,Fields,Beach,RedSea,RedCave,SpaceTime,FightPolicies,SeerPolicy,GameView,IGameView,SimGameView}.cs csharp/
# fix16.diff: everything outside Recording/ that differs from upstream + glue.diff (the hooks and the Fix16 maths), which
# cloud/setup.sh applies after glue.diff. Diffed against a scratch worktree of upstream with glue.diff committed.
C=/home/snake/dev/AnodyneSharp
W=$(mktemp -d)
git -C $C worktree add -q --detach $W 0b038d0be87b740578ab55061add0a3098e8c71f
(cd $W && git apply "$OLDPWD/cloud/glue.diff" && git add -A && git -c user.name=sync -c user.email=sync@localhost commit -qm glue)
GLUE=$(git -C $W rev-parse HEAD)
(cd $C && git add -N AnodyneSharp/AnodyneSharp.Shared/Utilities && git diff $GLUE -- . ':!Fluent-State-Machine' ':!AnodyneSharp/AnodyneSharp.Shared/Recording') > cloud/fix16.diff
git -C $C worktree remove --force $W
cp $C/AnodyneSharp/AnodyneSharp.Shared/Recording/RecordingManager.cs cloud/
echo "synced: $(ls csharp | wc -l) files"
