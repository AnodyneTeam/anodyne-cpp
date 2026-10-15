#pragma once

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// True if `input` doesn't exist, or is newer than any of `outputs` (or any
// output is missing) - i.e. `outputs` need regenerating from `input`.
inline bool IsFileNewer(const std::string& input, const std::vector<std::string>& outputs)
{
    namespace fs = std::filesystem;

    if (!fs::exists(input))
    {
        printf("%s doesn't exist\n", input.c_str());
        return false;
    }

    auto inputTime = fs::last_write_time(input);

    for (const auto& out : outputs)
    {
        if (!fs::exists(out))
        {
            printf("%s doesn't exist\n", out.c_str());
            return true;
        }

        auto outTime = fs::last_write_time(out);
        if (inputTime > outTime)
        {
            return true;
        }
    }

    return false;
}

// Writes `content` to `path` unless the file already holds exactly that, so an unchanged generated header keeps its
// timestamp and doesn't trigger a rebuild of everything that includes it. Returns true if the file was written.
inline bool WriteFileIfChanged(const char* path, const std::string& content)
{
    if (FILE* existing = fopen(path, "rb"))
    {
        std::string old;
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), existing)) > 0)
        {
            old.append(buf, n);
        }
        fclose(existing);
        if (old == content)
        {
            return false;
        }
    }

    FILE* f = fopen(path, "wb");
    if (!f)
    {
        printf("ERROR: can't write %s\n", path);
        return false;
    }
    fwrite(content.data(), 1, content.size(), f);
    fclose(f);
    return true;
}

