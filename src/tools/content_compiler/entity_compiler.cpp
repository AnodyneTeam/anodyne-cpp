#include "entity_compiler.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>

namespace
{
struct EntityLook final
{
    const char* mType;  // Entities.xml element name
    const char* mSheet; // sprite sheet (PNG stem), nullptr = invisible in the game
    bool mUseFrame;     // the entity's "frame" attribute picks the sheet frame
};

// Hand-written guesses (the C# decides sprites in each entity's constructor); anything missing from this table
// gets the generic placeholder box. Sheets must be listed in sprite_defs.cpp.
const EntityLook kLooks[] = {
    { "Treasure", "treasureboxes", false },
    { "Dash_Trap", "dash_trap", false },
    { "KeyBlock", "keyhole", false },
    { "Gate", "gates", true },
    { "Rat", "rat", false },
    { "Silverfish", "silverfish", false },
    { "Gasguy", "gas_guy", false },
    { "Switch_Pillar", "dame-switch-pillar", false },
    { "Pillar_Switch", "pillar_switch", false },
    { "Teleguy", "teleport_guy", false },
    { "Dog", "dog", false },
    { "Annoyer", "annoyer", false },
    { "Shieldy", "shieldy", false },
    { "Slime", "slime", false },
    { "Button", "buttons", false },
    { "Slasher", "f_slasher", false },
    { "Splitboss", "splitboss", false },
    { "Health_Cicada", "life_cicada", false },
    { "NPC", "npc", true },
    { "Frog", "frog", false },
    { "Dust", "dust", false },
    { "Spike_Roller", "spike_roller", false },
    { "Rotator", "f_rotator", false },
    { "Burst_Plant", "burst_plant", false },
    { "Four_Shooter", "f_four_shooter", false },
    { "CardGate", "gate_green", false },
    { "Pew_Laser", "pew_laser", false },
    { "Sun_Guy", "sun_guy", false },
    { "Sage", "sage", false },
    { "Eye_Light", "eyelight", false },
    { "Hole", "hole", false },
    { "CrackedTile", "crackedtiles", false },
    { "Dungeon_Statue", "big_statue", false },
    { "Console", "console", false },
    { "Dash_Pad", "dash_pads", false },
    { "Fire_Pillar", "fire_pillar", false },
    { "Steam_Pipe", "steam_pipe", false },
    { "On_Off_Laser", "on_off_shooter", false },
    { "Happy_NPC", "happy_npcs", true },
    { "Chaser", "chaser", false },
    { "Contort", "contort_small", false },
    { "Lion", "lion", false },
    { "Mitra", "mitra", false },
    { "Person", "person", true },
    { "WallBoss", "f_wallboss_face", false },
    { "Key", "key", false },
    { "Big_Door", "big_door", false },
    { "Trade_NPC", "npc", true },
    { "Forest_NPC", "forest_npcs", true },
    { "Huge_Fucking_Stag", "forest_stag", false },
    { "Dustmaid", "dustmaid", false },
    { "Elevator", "elevator", false },
    { "Sadbro", "sadman", false },
    { "Mover", "f_mover", false },
    { "Red_Boss", "red_boss", false },
    { "Red_Pillar", "red_pillar", false },
    { "Red_Walker", "redwalker", false },
    { "Redsea_NPC", "redsea_npcs", true },
    { "Suburb_Killer", "suburb_killers", false },
    { "Suburb_Walker", "suburb_walkers", false },
    { "Space_NPC", "space_npcs", true },
    { "Follower_Bro", "follower_bro", false },
    { "Sage_Boss", "sage_boss", false },
    { "Fisherman", "beach_npcs", false },
    { "Shadow_Briar", "briar", false },
    { "Health_Dropper", "small_health_pickup", false },
    // Triggers / invisible helpers.
    { "Event", nullptr, false },
    { "Jump_Trigger", nullptr, false },
    { "Stop_Marker", nullptr, false },
    { "solid_tile", nullptr, false },
    { "Go_Detector", nullptr, false },
    { "GoQuestDoorBlocker", nullptr, false },
    { "Water_Anim", nullptr, false },
    { "Propelled", nullptr, false },
};

// Doors that are visible objects (by the Door "type" attribute).
struct DoorLook final
{
    int mType;
    const char* mSheet;
};
const DoorLook kDoorLooks[] = {
    { 4, "whiteportal" },
    { 7, "whirlpool" },
    { 16, "nexus_pad" },
};
} // namespace

std::string BuildEntityTables(const ContentPaths& contentPaths, const std::vector<std::string>& mapNames)
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

    std::vector<std::string> sheets;
    auto sheetId = [&](const std::string& name) -> int
    {
        for (size_t i = 0; i < sheets.size(); i++)
        {
            if (sheets[i] == name)
            {
                return static_cast<int>(i);
            }
        }
        sheets.push_back(name);
        return static_cast<int>(sheets.size() - 1);
    };
    sheetId("placeholder");

    std::vector<std::vector<std::string>> perMap(mapNames.size());
    std::regex tagRe("<([A-Za-z_0-9]+)\\s([^>]*)>");
    std::regex attrRe("(\\w+)=\"([^\"]*)\"");
    int currentMap = -1;
    int count = 0;
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), tagRe); it != std::sregex_iterator(); ++it)
    {
        const std::string tag = (*it)[1].str();
        std::map<std::string, std::string> attrs;
        const std::string body = (*it)[2].str();
        for (auto a = std::sregex_iterator(body.begin(), body.end(), attrRe); a != std::sregex_iterator(); ++a)
        {
            attrs[(*a)[1].str()] = (*a)[2].str();
        }
        if (tag == "map")
        {
            auto m = mapIndex.find(attrs["name"]);
            currentMap = (m == mapIndex.end()) ? -1 : m->second;
            continue;
        }
        if (currentMap < 0 || !attrs.count("x"))
        {
            continue;
        }

        const int frame = std::atoi(attrs["frame"].c_str());
        std::string sheet = "placeholder";
        bool useFrame = false;
        if (tag == "Door")
        {
            const int type = std::atoi(attrs["type"].c_str());
            sheet.clear();
            for (const DoorLook& d : kDoorLooks)
            {
                if (d.mType == type)
                {
                    sheet = d.mSheet;
                }
            }
        }
        else
        {
            for (const EntityLook& l : kLooks)
            {
                if (tag == l.mType)
                {
                    sheet = l.mSheet ? l.mSheet : "";
                    useFrame = l.mUseFrame;
                }
            }
        }
        if (sheet.empty())
        {
            continue;
        }

        char buf[96];
        snprintf(buf, sizeof(buf), "    { %d, %d, %d, %d },\n", std::atoi(attrs["x"].c_str()),
                 std::atoi(attrs["y"].c_str()), sheetId(sheet), useFrame ? frame : 0);
        perMap[currentMap].push_back(buf);
        count++;
    }

    std::string out = "static const char* const kEntitySheets[] = {\n";
    for (const std::string& s : sheets)
    {
        out += "    \"" + s + "\",\n";
    }
    out += "};\nstatic const MapEntity kNoEntities[1] = {};\n";
    for (size_t i = 0; i < mapNames.size(); i++)
    {
        if (perMap[i].empty())
        {
            continue;
        }
        out += "static const MapEntity kEntities_" + mapNames[i] + "[] = {\n";
        for (const std::string& line : perMap[i])
        {
            out += line;
        }
        out += "};\n";
    }
    out += "static const MapEntityList kMapEntities[] =\n{\n";
    for (size_t i = 0; i < mapNames.size(); i++)
    {
        char buf[160];
        if (perMap[i].empty())
        {
            out += "    { 0, kNoEntities },\n";
        }
        else
        {
            snprintf(buf, sizeof(buf), "    { %zu, kEntities_%s },\n", perMap[i].size(), mapNames[i].c_str());
            out += buf;
        }
    }
    out += "};\n";
    printf("Entities: %d placeholder sprites, %zu sheets\n", count, sheets.size());
    return out;
}
