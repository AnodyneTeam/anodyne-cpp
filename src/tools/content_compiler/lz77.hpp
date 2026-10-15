#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

// GBA BIOS LZ77 (SWI 0x11/0x12). Header word = (size << 8) | 0x10, then groups of a flag byte (MSB first,
// 1 = back reference) and 8 items: a literal byte, or two bytes holding length-3 (4 bits) and disp-1 (12 bits).
// The VRAM variant (0x12, 16-bit writes) can't reference the byte just written, so disp >= 2.
inline std::vector<uint8_t> CompressLz77(const uint8_t* data, size_t size)
{
    std::vector<uint8_t> out = { 0x10, static_cast<uint8_t>(size), static_cast<uint8_t>(size >> 8),
                                 static_cast<uint8_t>(size >> 16) };
    std::vector<int> head(1 << 15, -1);
    std::vector<int> prev(size, -1);
    auto Hash = [](const uint8_t* p) -> size_t
    {
        return ((static_cast<size_t>(p[0]) << 10) ^ (static_cast<size_t>(p[1]) << 5) ^ p[2]) & ((1 << 15) - 1);
    };
    auto Insert = [&](size_t at)
    {
        if (at + 3 <= size)
        {
            const size_t h = Hash(data + at);
            prev[at] = head[h];
            head[h] = static_cast<int>(at);
        }
    };

    size_t pos = 0;
    while (pos < size)
    {
        const size_t flagAt = out.size();
        out.push_back(0);
        for (int bit = 0; bit < 8 && pos < size; bit++)
        {
            size_t bestLen = 0;
            size_t bestDisp = 0;
            if (pos + 3 <= size)
            {
                // Hash chain over 3-byte prefixes: only candidates that share the next 3 bytes are compared.
                const size_t maxDisp = std::min<size_t>(pos, 4096);
                int candidate = head[Hash(data + pos)];
                for (int chain = 0; candidate >= 0 && chain < 256; chain++)
                {
                    const size_t disp = pos - static_cast<size_t>(candidate);
                    if (disp > maxDisp)
                    {
                        break;
                    }
                    if (disp >= 2)
                    {
                        size_t len = 0;
                        while (len < 18 && pos + len < size && data[pos + len - disp] == data[pos + len])
                        {
                            len++;
                        }
                        if (len > bestLen)
                        {
                            bestLen = len;
                            bestDisp = disp;
                            if (len == 18)
                            {
                                break;
                            }
                        }
                    }
                    candidate = prev[static_cast<size_t>(candidate)];
                }
            }

            if (bestLen >= 3)
            {
                out[flagAt] |= static_cast<uint8_t>(0x80 >> bit);
                out.push_back(static_cast<uint8_t>(((bestLen - 3) << 4) | ((bestDisp - 1) >> 8)));
                out.push_back(static_cast<uint8_t>((bestDisp - 1) & 0xFF));
                for (size_t i = 0; i < bestLen; i++)
                {
                    Insert(pos + i);
                }
                pos += bestLen;
            }
            else
            {
                Insert(pos);
                out.push_back(data[pos++]);
            }
        }
    }
    while (out.size() % 4)
    {
        out.push_back(0);
    }
    return out;
}

