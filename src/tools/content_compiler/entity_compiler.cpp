#include "entity_compiler.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>

#include "../../anodyne/entity_types.h"

namespace
{
struct EntityLook final
{
    const char* mType;  // Entities.xml element name
    const char* mSheet; // sprite sheet (PNG stem), nullptr = invisible in the game
    bool mUseFrame;     // the entity's "frame" attribute picks the sheet frame
};

// What a placeholder (an entity the port doesn't implement yet) looks like: hand-written guesses (the C# decides
// sprites in each entity's constructor); anything missing from this table gets the generic placeholder box. Sheets
// must be listed in sprite_defs.cpp.
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

// Doors that are visible objects (by the Door "type" attribute). Their warps are door_compiler.cpp's.
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

// The C# [NamedEntity(xmlName, type, map, frames)] attributes of the classes the port implements. An element takes
// the most specific match (most of type/map/frames given), like EntityManager.ReadEntities; no match = placeholder.
struct NamedEntity final
{
    const char* mXml;
    const char* mTypeValue; // nullptr = any
    const char* mMap;       // nullptr = any
    std::set<int> mFrames;  // empty = any
    EntityPresetType mType;
};

const NamedEntity kNamed[] = {
    { "Console", nullptr, nullptr, { 0 }, EPT_Console },
    { "Console", nullptr, "BLANK", { 1 }, EPT_BlankConsole },
    { "Gate", nullptr, nullptr, {}, EPT_Gate },
    { "Button", nullptr, nullptr, {}, EPT_Button },
    { "Treasure", nullptr, nullptr, {}, EPT_Treasure },
    { "solid_tile", nullptr, nullptr, {}, EPT_SolidTile },
    { "Solid_Sprite", "blocker", nullptr, {}, EPT_Blocker },
    { "Solid_Sprite", "vblock", nullptr, {}, EPT_VBlocker },
    { "Dust", nullptr, nullptr, {}, EPT_Dust },
    { "Jump_Trigger", "", nullptr, {}, EPT_JumpTrigger },
    { "Jump_Trigger", "1", nullptr, {}, EPT_SpringPad },
    { "CrackedTile", nullptr, nullptr, {}, EPT_CrackedTile },
    { "Hole", nullptr, nullptr, {}, EPT_Hole },
    { "KeyBlock", nullptr, nullptr, { 0 }, EPT_SmallKeyGate },
    { "KeyBlock", nullptr, nullptr, { 1, 2, 3 }, EPT_BigKeyGate },
    { "CardGate", nullptr, nullptr, {}, EPT_CardGate },
    { "Key", nullptr, nullptr, {}, EPT_Key },
    { "Health_Dropper", nullptr, nullptr, {}, EPT_HealthEntity },
    { "Event", nullptr, nullptr, { 2 }, EPT_Checkpoint },
    { "Event", "entrance", nullptr, { 5 }, EPT_DungeonEntrance },
    { "Event", "entrance_ref", nullptr, { 5 }, EPT_DungeonEntranceRef },
    { "Event", nullptr, nullptr, { 1 }, EPT_VolumeEvent },
    { "Event", nullptr, nullptr, { 8 }, EPT_BossKeyReset },
    { "Event", "bedroom_bounce", nullptr, {}, EPT_BedroomBounceLight },
    { "Eye_Light", nullptr, nullptr, {}, EPT_EyeLight },
    { "Dungeon_Statue", nullptr, nullptr, {}, EPT_DungeonStatue },
    { "NPC", "rock", nullptr, {}, EPT_Rock },
    { "NPC", "statue", nullptr, {}, EPT_Statue },
    { "NPC", "big_key", nullptr, {}, EPT_BigKey },
    { "Sage", nullptr, "BLANK", {}, EPT_SageBlank },
    { "Sage", nullptr, "NEXUS", {}, EPT_SageNexus },
    { "Sage", nullptr, "OVERWORLD", {}, EPT_SageOverworld },
    { "Sage", nullptr, "BEDROOM", {}, EPT_SageBedroom },
    { "Mitra", nullptr, "OVERWORLD", {}, EPT_MitraOverworld },
    { "Mitra", nullptr, "FIELDS", {}, EPT_MitraFields },
    { "Trade_NPC", nullptr, nullptr, { 0 }, EPT_MiaoXiao },
    { "Trade_NPC", nullptr, nullptr, { 1 }, EPT_InsideMonster },
    { "Trade_NPC", nullptr, nullptr, { 2 }, EPT_OutsideMonster },
    { "Trade_NPC", nullptr, nullptr, { 3 }, EPT_ShopKeep },
    { "Health_Cicada", nullptr, nullptr, {}, EPT_HealthCicada },
    { "Sadbro", nullptr, nullptr, {}, EPT_Sadbro },
    { "Follower_Bro", nullptr, nullptr, {}, EPT_FollowerBro },
    { "Slime", nullptr, nullptr, {}, EPT_Slime },
    { "Shieldy", nullptr, nullptr, {}, EPT_Shieldy },
    { "Pew_Laser", nullptr, nullptr, {}, EPT_PewLaser },
    { "Annoyer", nullptr, nullptr, {}, EPT_Annoyer },
    { "Sun_Guy", nullptr, nullptr, {}, EPT_Seer },
    { "Dog", nullptr, nullptr, {}, EPT_Dog },
    { "Switch_Pillar", nullptr, nullptr, {}, EPT_SwitchPillar },
    { "Pillar_Switch", nullptr, nullptr, {}, EPT_PillarSwitch },
    { "Burst_Plant", nullptr, nullptr, {}, EPT_BurstPlant },
    { "Frog", nullptr, nullptr, {}, EPT_Frog },
    { "Dash_Trap", nullptr, nullptr, {}, EPT_DashTrap },
    { "Rotator", nullptr, nullptr, {}, EPT_Rotator },
    { "Door", nullptr, nullptr, {}, EPT_Door },
};

const char* const kTypeNames[] = {
#define ANODYNE_ENTITY_NAME(name) #name,
    ANODYNE_ENTITY_TYPES(ANODYNE_ENTITY_NAME)
#undef ANODYNE_ENTITY_NAME
};

// Returns the implemented type for an element, EPT_Placeholder if none matches. Sets *conflict if two equally
// specific classes match (the C# logs that and skips the entity).
EntityPresetType Resolve(const std::string& tag, const std::string& typeValue, const std::string& map, int frame,
                         bool* conflict)
{
    int bestScore = -1;
    EntityPresetType best = EPT_Placeholder;
    *conflict = false;
    for (const NamedEntity& n : kNamed)
    {
        if (tag != n.mXml || (n.mTypeValue && typeValue != n.mTypeValue) || (n.mMap && map != n.mMap) ||
            (!n.mFrames.empty() && !n.mFrames.count(frame)))
        {
            continue;
        }
        const int score = (n.mTypeValue ? 1 : 0) + (n.mMap ? 1 : 0) + (n.mFrames.empty() ? 0 : 1);
        if (score > bestScore)
        {
            bestScore = score;
            best = n.mType;
            *conflict = false;
        }
        else if (score == bestScore)
        {
            *conflict = true;
        }
    }
    return best;
}

struct Element final
{
    int mMap;
    std::string mTag;
    std::map<std::string, std::string> mAttrs;
};
} // namespace

// CardLookup.card (CardDataManager): which card a "growth" chest holds, by map and screen.
std::map<std::string, std::map<std::pair<int, int>, int>> ReadCardLookup(const std::string& path)
{
    std::map<std::string, std::map<std::pair<int, int>, int>> cards;
    std::ifstream f(path);
    std::string line;
    std::string map;
    while (std::getline(f, line))
    {
        // trim (and a UTF-8 BOM)
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        {
            line.pop_back();
        }
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t' || static_cast<unsigned char>(line[start]) >= 0x80))
        {
            start++;
        }
        line = line.substr(start);
        if (line.empty() || line == "{")
        {
            continue;
        }
        if (line == "}")
        {
            map.clear();
            continue;
        }
        int id = 0;
        int x = 0;
        int y = 0;
        if (!map.empty() && sscanf(line.c_str(), "%d %d %d", &id, &x, &y) == 3)
        {
            cards[map][{ x, y }] = id;
        }
        else
        {
            map = line;
        }
    }
    return cards;
}

std::string BuildEntityTables(const ContentPaths& contentPaths, const std::vector<std::string>& mapNames)
{
    const auto cardLookup = ReadCardLookup(contentPaths.GetBasePath() + "/CardLookup.card");
    std::ifstream file(contentPaths.GetEntitiesPath());
    std::stringstream ss;
    ss << file.rdbuf();
    const std::string xml = ss.str();

    std::map<std::string, int> mapIndex;
    for (size_t i = 0; i < mapNames.size(); i++)
    {
        mapIndex[mapNames[i]] = static_cast<int>(i);
    }

    // Pass 1: every element of the maps we build, in file order (the C#'s preset order).
    std::vector<Element> elements;
    std::regex tagRe("<([A-Za-z_0-9]+)\\s([^>]*)>");
    std::regex attrRe("(\\w+)=\"([^\"]*)\"");
    int currentMap = -1;
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), tagRe); it != std::sregex_iterator(); ++it)
    {
        Element e;
        e.mTag = (*it)[1].str();
        const std::string body = (*it)[2].str();
        for (auto a = std::sregex_iterator(body.begin(), body.end(), attrRe); a != std::sregex_iterator(); ++a)
        {
            e.mAttrs[(*a)[1].str()] = (*a)[2].str();
        }
        if (e.mTag == "map")
        {
            auto m = mapIndex.find(e.mAttrs["name"]);
            currentMap = (m == mapIndex.end()) ? -1 : m->second;
            continue;
        }
        if (currentMap < 0 || !e.mAttrs.count("x"))
        {
            continue;
        }
        e.mMap = currentMap;
        elements.push_back(e);
    }

    // Link groups (EntityManager._linkGroups): linkid -> its presets; a "linkgroup" list merges several ids.
    std::map<int, std::vector<size_t>> linkGroups;
    std::map<int, int> groupOf; // linkid -> representative
    auto findGroup = [&](int id)
    {
        while (groupOf.count(id) && groupOf[id] != id)
        {
            id = groupOf[id];
        }
        return id;
    };
    for (size_t i = 0; i < elements.size(); i++)
    {
        const auto& a = elements[i].mAttrs;
        auto l = a.find("linkid");
        if (l == a.end())
        {
            continue;
        }
        const int id = std::atoi(l->second.c_str());
        if (!groupOf.count(id))
        {
            groupOf[id] = id;
        }
        linkGroups[id].push_back(i);
        auto g = a.find("linkgroup");
        if (g != a.end())
        {
            std::stringstream list(g->second);
            std::string item;
            while (std::getline(list, item, ','))
            {
                const int other = std::atoi(item.c_str());
                if (!groupOf.count(other))
                {
                    groupOf[other] = other;
                }
                groupOf[findGroup(other)] = findGroup(id);
            }
        }
    }
    // The "entrance" Event of a link group, for DungeonEntranceRef.
    auto groupEntrance = [&](int id) -> const Element*
    {
        const int root = findGroup(id);
        for (auto& [lid, members] : linkGroups)
        {
            if (findGroup(lid) != root)
            {
                continue;
            }
            for (size_t m : members)
            {
                auto t = elements[m].mAttrs.find("type");
                if (t != elements[m].mAttrs.end() && t->second == "entrance")
                {
                    return &elements[m];
                }
            }
        }
        return nullptr;
    };

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

    std::vector<std::string> typeValues;
    auto typeValueId = [&](const std::string& value) -> int
    {
        for (size_t i = 0; i < typeValues.size(); i++)
        {
            if (typeValues[i] == value)
            {
                return static_cast<int>(i);
            }
        }
        typeValues.push_back(value);
        return static_cast<int>(typeValues.size() - 1);
    };
    typeValueId("");

    std::vector<std::vector<std::string>> perMap(mapNames.size());
    int typeCounts[EPT_Count] = {};
    int globalId = 0;
    for (const Element& e : elements)
    {
        const std::string& tag = e.mTag;
        auto attr = [&](const char* name) -> std::string
        {
            auto f = e.mAttrs.find(name);
            return f == e.mAttrs.end() ? std::string() : f->second;
        };
        const int frame = std::atoi(attr("frame").c_str());
        const std::string typeValue = attr("type");
        bool conflict = false;
        const EntityPresetType type = Resolve(tag, typeValue, mapNames[e.mMap], frame, &conflict);
        if (conflict)
        {
            printf("Entities: conflicting classes for %s-%d-'%s' in %s\n", tag.c_str(), frame, typeValue.c_str(),
                   mapNames[e.mMap].c_str());
        }

        // The placeholder look (also the fallback sprite of implemented types, which mostly pick their own).
        std::string sheet = "placeholder";
        bool useFrame = false;
        if (tag == "Door")
        {
            const int doorType = std::atoi(typeValue.c_str());
            sheet.clear();
            for (const DoorLook& d : kDoorLooks)
            {
                if (d.mType == doorType)
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
        if (type == EPT_Placeholder && sheet.empty())
        {
            continue; // invisible and not implemented: nothing to do
        }
        if (sheet.empty())
        {
            sheet = "placeholder";
        }

        int x = static_cast<int>(std::atof(attr("x").c_str()));
        int y = static_cast<int>(std::atof(attr("y").c_str()));
        int outFrame = frame;
        int linkId = attr("linkid").empty() ? -1 : std::atoi(attr("linkid").c_str());
        if (type == EPT_DungeonEntranceRef)
        {
            // The linked "entrance" Event's position travels in frame/linkid (the ref only needs that).
            const Element* entrance = linkId >= 0 ? groupEntrance(linkId) : nullptr;
            if (!entrance)
            {
                printf("Entities: entrance_ref without an entrance in %s\n", mapNames[e.mMap].c_str());
                continue;
            }
            outFrame = static_cast<int>(std::atof(entrance->mAttrs.at("x").c_str()));
            linkId = static_cast<int>(std::atof(entrance->mAttrs.at("y").c_str()));
        }
        if (type == EPT_Treasure)
        {
            // CardDataManager.GetCardId for a growth (card) chest: the card of its screen, 48 in the Nexus.
            linkId = -1;
            const std::string& mapName = mapNames[e.mMap];
            if (mapName == "NEXUS")
            {
                linkId = 48;
            }
            else
            {
                const auto m = cardLookup.find(mapName);
                if (m != cardLookup.end())
                {
                    const auto c = m->second.find({ x / 160, y / 160 });
                    linkId = c != m->second.end() ? c->second : -1;
                }
            }
        }
        const int permanence = attr("p").empty() ? PERM_GRID_LOCAL : std::atoi(attr("p").c_str());

        char buf[160];
        snprintf(buf, sizeof(buf), "    { %d, %d, %d, %d, %d, EPT_%s, %d, %d, %d, %d },\n", x, y, outFrame, linkId,
                 globalId, kTypeNames[type], permanence, sheetId(sheet), useFrame ? frame : 0,
                 typeValueId(typeValue));
        perMap[e.mMap].push_back(buf);
        typeCounts[type]++;
        globalId++;
    }

    std::string out = "static const char* const kEntitySheets[] = {\n";
    for (const std::string& s : sheets)
    {
        out += "    \"" + s + "\",\n";
    }
    out += "};\nstatic const char* const kEntityTypeValues[] = {\n";
    for (const std::string& s : typeValues)
    {
        out += "    \"" + s + "\",\n";
    }
    out += "};\n";
    out += "static const unsigned kEntityPresetCount = " + std::to_string(globalId) + ";\n";
    out += "static const MapEntity kNoEntities[1] = {};\n";
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
    printf("Entities: %d presets, %zu sheets, %d placeholders\n", globalId, sheets.size(), typeCounts[EPT_Placeholder]);
    return out;
}
