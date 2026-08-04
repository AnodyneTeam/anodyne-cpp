#pragma once

#include "string.hpp"
#include <optional>
#include <map>
#include <stdint.h>

// Compile Content/Dialogue/dialogue_[LANG_CODE].txt into a binary file
// that is used by the engine
class DialogCompiler final
{
public:
    enum class ParseState
    {
        START,
        NPC,
        AREA,
        SCENE,
    };

    class DialogueState final
    {
    public:
        bool dirty = false;
        bool finished = false;
        int line = 0;
    };

    class DialogueScene final
    {
    public:
        DialogueScene() {}

        DialogueScene(bool alignTop, std::optional<std::size_t> loopId, std::vector<std::string> &dialog)
            : AlignTop(alignTop), LoopID(loopId), _lines(dialog)
        {
        }

        bool AlignTop = false;
        std::optional<std::size_t> LoopID;
        std::vector<std::string> _lines;

        DialogueState state;
    };

    class DialogueArea final
    {
    public:
        std::map<std::string, DialogueScene> _scenes;
    };

    class DialogueNPC final
    {
    public:
        bool doesReset;

        std::map<std::string, DialogueArea> _areas;
    };

    std::string GetName(std::string line)
    {
        return string::split(line, " ")[1];
    }

    std::map<std::string, DialogueNPC> newSceneTree;

    bool Load(const std::string &path, const std::string &langCode);
    void Compile();
    void DebugPrint();
};
