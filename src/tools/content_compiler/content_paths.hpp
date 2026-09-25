#pragma once

#include <string>

class ContentPaths final
{
public:
    explicit ContentPaths(const std::string& basePath)
        : mBasePath(basePath)
    {

    }

    std::string GetDialogPath(const std::string& language) const
    {
        return CombinePaths(mBasePath, "Dialogue", "dialogue_" + language + ".txt");
    }

    std::string GetMapBGCSVPath(const std::string& mapName) const
    {
        return CombinePaths(CombinePaths(mBasePath, "Maps", mapName), "BG.csv");
    }

    std::string GetMapTilesetPath(const std::string& mapName) const
    {
        std::string mapNameLower = ToLowerCase(mapName);
        return CombinePaths(mBasePath, "textures/tilemaps", mapNameLower + "_tilemap.png");
    }

    std::string GetBgmPath(const std::string& trackName) const
    {
        return CombinePaths(mBasePath, "bgm", trackName + ".ogg");
    }

    std::string GetAmbiencePath(const std::string& trackName) const
    {
        return CombinePaths(mBasePath, "ambience", trackName + ".ogg");
    }

    std::string GetSfxPath(const std::string& category, const std::string& clipName) const
    {
        return CombinePaths(mBasePath, "sfx/" + category, clipName + ".mp3");
    }

private:
    std::string ToLowerCase(const std::string& str) const
    {
        std::string result = str;
        for (char& c : result)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return result;
    }

    std::string CombinePaths(const std::string& path1, const std::string& path2) const
    {
        if (path1.empty())
        {
            return path2;
        }

        if (path2.empty())
        {
            return path1;
        }
        if (path1.back() == '/' || path1.back() == '\\')
        {
            return path1.substr(0, path1.length() - 1) + "/" + path2.substr(1);
        }
        else
        {
            return path1 + "/" + path2;
        }
    }

    std::string CombinePaths(const std::string& path1, const std::string& path2, const std::string& path3) const
    {
        return CombinePaths(CombinePaths(path1, path2), path3);
    }

    std::string mBasePath;
};
