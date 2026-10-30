#!/usr/bin/env python3
"""Makes a save to start the bot's Boss Rush part from (it isn't on the route: a test of the BOSSRUSH map).

    cloud/bossrush_save.py <from save dir> <to save dir>
    ANODYNE_BOT_MAX_AVG_MS=60 SECS=3000 ./part.sh bossrush <to save dir> end_bossrush

<from save dir> is any saved game (Saves/Save_1.dat + InputConfig.dat) whose checkpoint is in the Nexus, such as the end
of the route's nexus part. The new save has every broom, the boots, all 49 cards (the Boss Rush portal wants them) and
16 hearts, and puts us back at that checkpoint.
"""
import json
import shutil
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    save = json.loads((src / "Saves" / "Save_1.dat").read_text(encoding="utf-8-sig"))
    if save["checkpoint"]["map"] != "NEXUS":
        sys.exit(f"{src}: checkpoint is in {save['checkpoint']['map']}, not the Nexus")
    inv = save["inventory"]
    inv["CardStatus"] = [True] * len(inv["CardStatus"])
    for k in ("CanJump", "HasBroom", "HasLengthen", "HasWiden", "HasTransformer"):
        inv[k] = True
    inv["EquippedBroom"] = inv["_equippedBroom"] = 1  # (the plain broom)
    save["max_health"] = save["current_health"] = 16
    if "BOSSRUSH" not in save["events"]["VisitedMaps"]:
        save["events"]["VisitedMaps"] = sorted(save["events"]["VisitedMaps"] + ["BOSSRUSH"])
    (dst / "Saves").mkdir(parents=True, exist_ok=True)
    if (src / "InputConfig.dat").exists():
        shutil.copy(src / "InputConfig.dat", dst / "InputConfig.dat")
    (dst / "Saves" / "Save_1.dat").write_text(json.dumps(save), encoding="utf-8")


if __name__ == "__main__":
    main()
