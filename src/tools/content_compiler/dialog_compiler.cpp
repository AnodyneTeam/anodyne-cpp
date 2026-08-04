#include "dialog_compiler.hpp"
#include <set>
#include <sstream>

bool DialogCompiler::Load(const std::string &path, const std::string &langCode)
{
    const std::string fullPath = path + "/dialogue_" + langCode + ".txt";
    printf("Path = %s\n", fullPath.c_str());

    std::ifstream textStream(fullPath.c_str(), std::ios::in);
    if (!textStream.is_open())
    {
        return false;
    }

    ParseState state = ParseState::START;

    DialogueNPC *npc = nullptr;
    std::string npcName;
    std::string areaName;
    std::string scene;

    std::vector<std::string> dialogue;
    std::optional<std::size_t> loopID;
    bool alignTop = false;

    std::string line;
    while (std::getline(textStream, line))
    {
        line = string::trim(line);
        // printf("line = %s\n", line.c_str());

        // Skip comments
        if (string::startsWith(line, "#"))
        {
            continue;
        }

        switch (state)
        {
        case ParseState::START:
            if (string::startsWith(line, "npc"))
            {
                state = ParseState::NPC;
                npcName = GetName(line);
                newSceneTree[npcName] = {};
                npc = &newSceneTree[npcName];
            }
            break;

        case ParseState::NPC:
            if (line == "does reset")
            {
                npc->doesReset = true;
                continue;
            }
            if (line == "end npc")
            {
                state = ParseState::START;
                continue;
            }
            if (string::startsWith(line, "area"))
            {
                areaName = GetName(line);
                state = ParseState::AREA;
                npc->_areas[areaName] = {};
            }
            break;

        case ParseState::AREA:
            if (line == "end area")
            {
                state = ParseState::NPC;
                continue;
            }

            if (string::startsWith(line, "scene"))
            {
                scene = GetName(line);
                state = ParseState::SCENE;

                loopID = {};
                dialogue.clear();
                alignTop = false;
            }
            break;

        case ParseState::SCENE:
            if (string::IsEmptyOrWhiteSpace(line))
            {
                continue;
            }

            if (line == "TOP")
            {
                alignTop = true;
                continue;
            }
            if (line == "end scene")
            {
                state = ParseState::AREA;
                DialogueScene s(alignTop, loopID, dialogue);
                // TODO: Not required ??
                // s.state = newSceneTree[npcName].GetArea(areaName)->_scenes[scene].state;
                npc->_areas[areaName]._scenes[scene] = s;
                continue;
            }
            if (line == "LOOP")
            {
                loopID = dialogue.size();
                continue;
            }
            dialogue.push_back(line);
            break;
        }
    }
    return true;
}

void DialogCompiler::DebugPrint()
{
    printf("Dialog compiled %d scenes\n", newSceneTree.size());
    for (auto &[npcName, sceneDialogNpc] : newSceneTree)
    {
        for (auto &[areaName, area] : sceneDialogNpc._areas)
        {
            for (auto &[sceneName, sceneDialog] : area._scenes)
            {
                printf("scene name [%s] npc name [%s] area name [%s] lines [%d]\n", sceneName.c_str(), npcName.c_str(), areaName.c_str(), sceneDialog._lines.size());
                if (!sceneDialog._lines.empty())
                {
                    printf(" lines:\n");
                    for (auto &line : sceneDialog._lines)
                    {
                        printf("  %s\n", line.c_str());
                    }
                }
            }
        }
    }
}

class BinaryWriter final
{
public:
    void Write(uint32_t value)
    {
        WriteGeneric(value);
    }

private:
    template <typename T>
    void WriteGeneric(T value)
    {
        mBuffer.write(reinterpret_cast<const char *>(&value), sizeof(T));
    }

    std::stringstream mBuffer;
};

void DialogCompiler::Compile()
{
    BinaryWriter w;

    // Generate scene ids
    std::set<std::string> scenes;

    // Generate area ids
    std::set<std::string> areas;
    
    // Generate npc ids
    std::set<std::string> npcs;

    // Scene count
    //w.Write(newSceneTree.size());

    /*
    given a npc, area and scene get dialog offset

    [index by npc id]
    [npc areas offset 1]
    [npc areas offset 2]

    [index by area id]
    [npc dialog offset 1]
    [npc dialog offset 2]
    
    [index by scene id]
    [dialog offset 1]
    [dialog offset 2]

    [fixed fields]
    [string data 1]
    [string data 2]
    */

    for (auto &[npcName, sceneDialogNpc] : newSceneTree)
    {
        for (auto &[areaName, area] : sceneDialogNpc._areas)
        {
            for (auto &[sceneName, sceneDialog] : area._scenes)
            {
                scenes.insert(sceneName);

                w.Write(sceneDialog.AlignTop);
                w.Write(sceneDialog.state.dirty);
                w.Write(sceneDialog.state.finished);
                w.Write(sceneDialog.state.line);
               
                if (!sceneDialog._lines.empty())
                {
                    for (auto &line : sceneDialog._lines)
                    {
                        printf("  %s\n", line.c_str());
                    }
                }
            }
        }
    }
}
