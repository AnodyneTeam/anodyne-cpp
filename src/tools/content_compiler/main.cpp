#include <stdio.h>
#include "dialog_compiler.hpp"
#include "map_compiler.hpp"
#include "content_paths.hpp"

static void CompileDialog(ContentPaths& contentPaths)
{
    DialogCompiler dc;
    if (dc.Load(contentPaths.GetDialogPath("EN")))
    {
        dc.Compile();
        dc.DebugPrint();
    }
    else
    {
        printf("Dialog OVER!\n");
    }
}

static void CompileMap(ContentPaths& contentPaths, const std::string& mapName)
{
    MapCompiler mc;
    if (mc.Load(contentPaths.GetMapBGCSVPath(mapName), contentPaths.GetMapTilesetPath(mapName)))
    {
        mc.Compile(mapName);
    }
    else
    {
        printf("Map OVER!\n");
    }
}

int main(int argc, char **argv)
{
    printf("Content compiler exec from %s\n", argv[0]);

    ContentPaths contentPaths("../../old/Content");

    CompileDialog(contentPaths);

    const std::vector<std::string> mapNames = 
    {
        "APARTMENT",
        "BLANK",
        "CELL",
        "CROWD",
        "FIELDS",
        "HAPPY",
        "OVERWORLD",
        "SPACE",
        "TERMINAL",
        "BEACH",
        "BLUE",
        "CIRCUS",
        "DEBUG",
        "FOREST",
        "HOTEL",
        "REDCAVE",
        "STREET",
        "WINDMILL",
        "BEDROOM",
        "BOSSRUSH",
        "CLIFF",
        "DRAWER",
        "GO",
        "NEXUS",
        "REDSEA",
        "SUBURB"
    };

    for (auto& mapName : mapNames)
    {
        CompileMap(contentPaths, mapName);
    }

    return 0;
}
