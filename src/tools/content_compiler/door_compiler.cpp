#include "door_compiler.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

namespace
{
struct DoorPreset final
{
    int mMap = 0;
    int mX = 0;
    int mY = 0;
    int mLink = 0; // "frame"
    int mType = 0;
};

// World facing order: 0 down, 1 right, 2 up, 3 left; 255 = keep the current facing.
struct DoorKind final
{
    int mType;
    int mHx; // hitbox relative to the preset position
    int mHy;
    int mW;
    int mH;
    int mOffX; // teleport offset applied to the linked door's position
    int mOffY;
    int mFacing;
    int mInteract; // needs A pressed (Interactable in the C#)
};

// From Entities/Gadget/Doors/*.cs.
const DoorKind kKinds[] = {
    { 1, 0, 0, 16, 16, 0, 16, 0, 0 },      // DownDoor
    { 5, 4, 4, 8, 8, 0, -20, 2, 0 },       // UpDoor (8x8, centred)
    { 10, 0, 0, 16, 16, -16, 0, 3, 0 },    // LeftDoor
    { 11, 0, 0, 16, 16, 17, 0, 1, 0 },     // RightDoor
    { 12, -32, 0, 80, 16, 0, 16, 0, 0 },   // WideDownDoor
    { 13, 0, -32, 16, 80, -16, 0, 3, 0 },  // TallLeftDoor
    { 14, -32, 0, 80, 16, 0, -20, 2, 0 },  // WideUpDoor
    { 15, 0, -32, 16, 80, 17, 0, 1, 0 },   // TallRightDoor
    { 9, 0, 0, 16, 16, 0, 0, 255, 0 },     // NoMoveDoor
    { 8, 0, 0, 16, 16, 0, 0, 255, 0 },     // FallDoor (plain here)
    { 7, 0, 0, 16, 16, 0, 0, 255, 0 },     // WhirlPool (REDSEA's is patched below)
    { 4, 7, 7, 2, 2, 0, 0, 255, 0 },       // BlankPortal (2x2, centred)
    { 16, 6, 6, 22, 18, 10, 34, 255, 1 },  // NexusPad
    { 17, 0, 6, 32, 26, 10, 8, 255, 1 },   // NexusDoor
};
} // namespace

std::string BuildDoorTables(const ContentPaths& contentPaths, const std::vector<std::string>& mapNames)
{
    std::ifstream file(contentPaths.GetEntitiesPath());
    std::stringstream ss;
    ss << file.rdbuf();
    const std::string xml = ss.str();

    std::map<std::string, int> mapIndex;
    for (size_t i = 0; i < mapNames.size(); i++)
    {
        mapIndex[mapNames[i]] = static_cast<int>(i);
    }

    // Walk the file: <map name="X" ...> sections containing <Door x= y= frame= type= .../>.
    std::vector<DoorPreset> doors;
    std::vector<std::string> docMaps;
    std::regex tagRe("<(map|Door)\\s([^>]*)>");
    std::regex attrRe("(\\w+)=\"([^\"]*)\"");
    int currentMap = -1;
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), tagRe); it != std::sregex_iterator(); ++it)
    {
        std::map<std::string, std::string> attrs;
        const std::string body = (*it)[2].str();
        for (auto a = std::sregex_iterator(body.begin(), body.end(), attrRe); a != std::sregex_iterator(); ++a)
        {
            attrs[(*a)[1].str()] = (*a)[2].str();
        }
        if ((*it)[1].str() == "map")
        {
            auto m = mapIndex.find(attrs["name"]);
            currentMap = (m == mapIndex.end()) ? -1 : m->second;
            continue;
        }
        if (currentMap < 0)
        {
            continue;
        }
        DoorPreset d;
        d.mMap = currentMap;
        d.mX = std::atoi(attrs["x"].c_str());
        d.mY = std::atoi(attrs["y"].c_str());
        d.mLink = std::atoi(attrs["frame"].c_str());
        d.mType = std::atoi(attrs["type"].c_str());
        doors.push_back(d);
    }

    std::string out = "static const MapDoor kNoDoors[1] = {};\n";
    std::vector<std::vector<std::string>> perMap(mapNames.size());
    int emitted = 0;
    for (const DoorPreset& d : doors)
    {
        const DoorKind* kind = nullptr;
        for (const DoorKind& k : kKinds)
        {
            if (k.mType == d.mType)
            {
                kind = &k;
            }
        }
        if (!kind)
        {
            continue; // type 6 (one way target) and anything unknown: no trigger
        }

        // The other door with the same link id is the destination (EntityManager.GetLinkedDoor).
        const DoorPreset* dest = nullptr;
        for (const DoorPreset& o : doors)
        {
            if (&o != &d && o.mLink == d.mLink)
            {
                dest = &o;
            }
        }
        if (!dest)
        {
            continue;
        }

        int offY = kind->mOffY;
        if (d.mType == 7 && mapNames[d.mMap] == "REDSEA")
        {
            offY = -36;
        }

        char buf[256];
        snprintf(buf, sizeof(buf), "    { %d, %d, %d, %d, %d, %d, %d, %d, %d, %d },\n", d.mX + kind->mHx, d.mY + kind->mHy,
                 kind->mW, kind->mH, dest->mX + kind->mOffX, dest->mY + offY, dest->mMap, kind->mFacing,
                 kind->mInteract, d.mType == 4 ? 1 : 0);
        perMap[d.mMap].push_back(buf);
        emitted++;
    }

    for (size_t i = 0; i < mapNames.size(); i++)
    {
        if (perMap[i].empty())
        {
            continue;
        }
        out += "static const MapDoor kDoors_" + mapNames[i] + "[] = {\n";
        for (const std::string& line : perMap[i])
        {
            out += line;
        }
        out += "};\n";
    }
    out += "static const MapDoorList kMapDoors[] =\n{\n";
    for (size_t i = 0; i < mapNames.size(); i++)
    {
        char buf[160];
        if (perMap[i].empty())
        {
            out += "    { 0, kNoDoors },\n";
        }
        else
        {
            snprintf(buf, sizeof(buf), "    { %zu, kDoors_%s },\n", perMap[i].size(), mapNames[i].c_str());
            out += buf;
        }
    }
    out += "};\n";
    printf("Doors: %d triggers\n", emitted);
    return out;
}
