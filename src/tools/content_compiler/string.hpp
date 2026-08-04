#pragma once

#include <string>
#include <vector>
#include <fstream>

namespace string
{
    inline std::vector<std::string> split(const std::string &str, const std::string &delimiter)
    {
        std::vector<std::string> result;

        if (delimiter.empty())
        {
            result.push_back(str);
            return result;
        }

        std::size_t start = 0;
        while (true)
        {
            std::size_t pos = str.find(delimiter, start);
            if (pos == std::string::npos)
            {
                result.push_back(str.substr(start));
                break;
            }

            result.push_back(str.substr(start, pos - start));
            start = pos + delimiter.size();
        }

        return result;
    }

    inline bool startsWith(const std::string &str, const std::string &startsWithStr)
    {
        if (startsWithStr.size() > str.size())
            return false;

        return std::equal(
            startsWithStr.begin(),
            startsWithStr.end(),
            str.begin());
    }

    inline bool IsEmptyOrWhiteSpace(const std::string &str)
    {
        if (str.empty())
        {
            return true;
        }

        for (std::string::size_type i = 0; i < str.size(); ++i)
        {
            if (!std::isspace(static_cast<unsigned char>(str[i])))
            {
                return false;
            }
        }
        return true;
    }

    inline std::string trim(const std::string &s)
    {
        std::string::size_type start = 0;
        std::string::size_type end = s.size();

        // Find first non‑whitespace
        while (start < end && std::isspace(static_cast<unsigned char>(s[start])))
        {
            ++start;
        }

        // Find last non‑whitespace
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        {
            --end;
        }

        return s.substr(start, end - start);
    }
}
