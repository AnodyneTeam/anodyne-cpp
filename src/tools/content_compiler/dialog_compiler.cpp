#include "dialog_compiler.hpp"
#include <set>
#include <cstring>
#include <sstream>

bool DialogCompiler::Load(const std::string &fullPath)
{

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

namespace
{
// GBA button names for the dialogue key placeholders ([SOMEKEY-C] = Accept, [SOMEKEY-X] = Cancel on PC).
std::string ReplaceKeys(std::string t)
{
    const char* const kKeys[][2] = {
        { "[SOMEKEY-C]", "A" },     { "[SOMEKEY-X]", "B" },       { "[SOMEKEY-LEFT]", "LEFT" }, { "[SOMEKEY-UP]", "UP" },
        { "[SOMEKEY-RIGHT]", "RIGHT" }, { "[SOMEKEY-DOWN]", "DOWN" }, { "[SOMEKEY-ENTER]", "START" },
    };
    for (const auto& k : kKeys)
    {
        size_t pos;
        while ((pos = t.find(k[0])) != std::string::npos)
        {
            t.replace(pos, strlen(k[0]), k[1]);
        }
    }
    return t;
}

// Regex.Unescape for the one escape the dialogue uses (\n), curly quotes / ellipsis to the font's ASCII, anything
// else outside ASCII dropped (the 7x8 font has no glyph for it either).
std::string ToGbaText(const std::string& in)
{
    std::string out;
    for (size_t i = 0; i < in.size(); i++)
    {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == '\\' && i + 1 < in.size() && in[i + 1] == 'n')
        {
            out += '\n';
            i++;
        }
        else if (c < 128)
        {
            out += static_cast<char>(c);
        }
        else if (in.compare(i, 3, "\xE2\x80\x99") == 0 || in.compare(i, 3, "\xE2\x80\x98") == 0)
        {
            out += '\'';
            i += 2;
        }
        else if (in.compare(i, 3, "\xE2\x80\x9C") == 0 || in.compare(i, 3, "\xE2\x80\x9D") == 0)
        {
            out += '"';
            i += 2;
        }
        else if (in.compare(i, 3, "\xE2\x80\xA6") == 0)
        {
            out += "...";
            i += 2;
        }
        else
        {
            while (i + 1 < in.size() && (static_cast<unsigned char>(in[i + 1]) & 0xC0) == 0x80)
            {
                i++;
            }
        }
    }
    return out;
}

std::string CLiteral(const std::string& s)
{
    std::string out = "\"";
    for (const char c : s)
    {
        char buf[8];
        if (c == '"' || c == '\\')
        {
            out += '\\';
            out += c;
        }
        else if (c < 32)
        {
            snprintf(buf, sizeof(buf), "\\%03o", c);
            out += buf;
        }
        else
        {
            out += c;
        }
    }
    return out + "\"";
}
} // namespace

// C++ header with every scene (npc / area / scene name, flags, loop line) and the lines, for anodyne/dialog.cpp.
std::string DialogCompiler::Emit()
{
    std::string lines;
    std::string scenes;
    size_t lineCount = 0;
    size_t sceneCount = 0;
    for (auto& [npcName, npc] : newSceneTree)
    {
        for (auto& [areaName, area] : npc._areas)
        {
            for (auto& [sceneName, scene] : area._scenes)
            {
                char buf[256];
                snprintf(buf, sizeof(buf), "    { \"%s\", \"%s\", \"%s\", %zu, %zu, %d, %d },\n", npcName.c_str(),
                         areaName.c_str(), sceneName.c_str(), lineCount, scene._lines.size(),
                         scene.LoopID ? static_cast<int>(*scene.LoopID) : 0, scene.AlignTop ? 1 : 0);
                scenes += buf;
                sceneCount++;
                for (const std::string& l : scene._lines)
                {
                    lines += "    " + CLiteral(ReplaceKeys(ToGbaText(l))) + ",\n";
                    lineCount++;
                }
            }
        }
    }
    std::string out = "// Generated by the content compiler from dialogue_EN.txt - do not edit.\n#pragma once\n";
    out += "struct DialogSceneData\n{\n    const char* mNpc;\n    const char* mArea;\n    const char* mScene;\n";
    out += "    unsigned short mFirstLine;\n    unsigned short mLineCount;\n    unsigned char mLoop;\n    unsigned char mTop;\n};\n";
    out += "static const char* const kDialogLines[] =\n{\n" + lines + "};\n";
    out += "static const DialogSceneData kDialogScenes[] =\n{\n" + scenes + "};\n";
    char buf[64];
    snprintf(buf, sizeof(buf), "static const unsigned kDialogSceneCount = %zu;\n", sceneCount);
    out += buf;
    printf("Dialog: %zu scenes, %zu lines\n", sceneCount, lineCount);
    return out;
}
