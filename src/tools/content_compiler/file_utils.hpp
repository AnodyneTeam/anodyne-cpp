#pragma once

#include <cstdio>
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
