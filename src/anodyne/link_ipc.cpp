// The bot link over the link port (link_ipc.h). Only built into ANODYNE_LINK ROMs.
#include "link_ipc.h"

#include <stdio.h>
#include <string.h>

#include "tonc.h"

#include "game_probe.h"

namespace
{
constexpr uint16_t kRcntJoyBus = 0xC000;
constexpr uint16_t kJoyCntRecv = 2;    // JOYCNT: the host wrote JOY_RECV (write 1 to clear)
constexpr uint16_t kJoyStatTrans = 8;  // JOYSTAT: JOY_TRANS holds a word the host hasn't read yet
constexpr char kWantLine = 0x01;       // ROM -> host: send me your next line

uint32_t gWord = 0; // bytes on their way out, low byte first
int gWordBytes = 0;
uint16_t gKeys = 0;
uint32_t gHold = 0; // frames the current keys still have to be held
uint32_t gFrame = 0;
char gLine[600] __attribute__((section(".sbss")));
char gScratch[1024] __attribute__((section(".sbss")));

void SendWord(uint32_t w)
{
    while (REG_JOYSTAT & kJoyStatTrans)
    {
    }
    REG_JOY_TRANS = w;
}

void SendByte(char c)
{
    gWord |= static_cast<uint32_t>(static_cast<uint8_t>(c)) << (8 * gWordBytes);
    if (++gWordBytes == 4)
    {
        SendWord(gWord);
        gWord = 0;
        gWordBytes = 0;
    }
}

// Sends what is buffered, the last word padded with zero bytes.
void SendFlush()
{
    if (gWordBytes > 0)
    {
        SendWord(gWord);
        gWord = 0;
        gWordBytes = 0;
    }
}

void Send(const char* data, int len)
{
    for (int i = 0; i < len; i++)
    {
        SendByte(data[i]);
    }
}

void SendLine(const char* s)
{
    Send(s, static_cast<int>(strlen(s)));
    SendByte('\n');
    SendFlush();
}

// The host's next line, without its '\n' (cut short past the buffer).
void ReadLine()
{
    SendByte(kWantLine);
    SendFlush();
    int len = 0;
    for (;;)
    {
        while (!(REG_JOYCNT & kJoyCntRecv))
        {
        }
        // Acked before it's read: the host sends the next word once this one is read, and an ack after that could
        // clear the next word's flag along with this one's.
        REG_JOYCNT = kJoyCntRecv;
        const uint32_t w = REG_JOY_RECV;
        for (int i = 0; i < 4; i++)
        {
            const char c = static_cast<char>(w >> (8 * i));
            if (c == '\n')
            {
                gLine[len] = '\0';
                return;
            }
            if (c != '\0' && c != '\r' && len < static_cast<int>(sizeof(gLine)) - 1)
            {
                gLine[len++] = c;
            }
        }
    }
}

void SendState()
{
    char head[48];
    snprintf(head, sizeof(head), "{\"frame\":%lu,\"state\":", static_cast<unsigned long>(gFrame));
    Send(head, static_cast<int>(strlen(head)));
    GameProbe_JsonTo(Send, gScratch, sizeof(gScratch));
    SendLine("}");
}

void SendPeek(uint32_t addr, uint32_t len)
{
    static const char kHex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < len; i++)
    {
        const uint8_t b = *reinterpret_cast<const volatile uint8_t*>(static_cast<uintptr_t>(addr + i));
        SendByte(kHex[b >> 4]);
        SendByte(kHex[b & 15]);
    }
    SendByte('\n');
    SendFlush();
}
} // namespace

void Link_Init()
{
    REG_RCNT = kRcntJoyBus;
    REG_JOYCNT = 7; // (clears any stale reset/receive/send flags)
}

uint16_t Link_Keys()
{
    return gKeys;
}

// sim::Ipc_OnFrame, command for command ("shot" has no screen to save here).
void Link_OnFrame()
{
    gFrame++;
    if (gHold > 1)
    {
        gHold--;
        return;
    }
    gHold = 0;
    SendState();
    for (;;)
    {
        ReadLine();
        char cmd[16] = {};
        char arg[64] = {};
        unsigned a = 0;
        unsigned b = 0;
        sscanf(gLine, "%15s", cmd);
        if (!strcmp(cmd, "keys"))
        {
            unsigned frames = 1;
            if (sscanf(gLine, "keys %x %u", &a, &frames) >= 1)
            {
                gKeys = static_cast<uint16_t>(a & 0x3FF);
                gHold = frames ? frames : 1;
                return;
            }
            SendLine("error usage: keys <hex mask> [frames]");
        }
        else if (!strcmp(cmd, "peek") && sscanf(gLine, "peek %x %u", &a, &b) == 2 && b <= 65536)
        {
            SendPeek(a, b);
        }
        else if (!strcmp(cmd, "gameframe"))
        {
            const bool set = sscanf(gLine, "gameframe %u", &a) == 1;
            snprintf(arg, sizeof(arg), "%u", GameProbe_GameFrame(set, a));
            SendLine(arg);
        }
        else if (!strcmp(cmd, "state"))
        {
            SendState();
        }
        else if (!strcmp(cmd, "collision"))
        {
            GameProbe_CollisionTo(Send, gScratch, sizeof(gScratch));
            SendLine("");
        }
        else if (!strcmp(cmd, "warp") && sscanf(gLine, "warp %63s %u %u", arg, &a, &b) == 3)
        {
            SendLine(GameProbe_Warp(arg, static_cast<int>(a), static_cast<int>(b)) ? "ok" : "error no such map");
        }
        else if (!strcmp(cmd, "rng") && sscanf(gLine, "rng %u", &a) == 1)
        {
            GameProbe_SetRng(a, sscanf(gLine, "rng %*u %63s", arg) == 1 && !strcmp(arg, "keep"));
            SendLine("ok");
        }
        else if (!strcmp(cmd, "checkpoint") && sscanf(gLine, "checkpoint %63s %u %u", arg, &a, &b) == 3)
        {
            SendLine(GameProbe_SetCheckpoint(arg, static_cast<int>(a), static_cast<int>(b)) ? "ok" : "error no such map");
        }
        else if (!strcmp(cmd, "event") && sscanf(gLine, "event %63s %u", arg, &a) == 2)
        {
            GameProbe_SetEvent(arg, static_cast<int>(a));
            SendLine("ok");
        }
        else if (!strcmp(cmd, "heal"))
        {
            GameProbe_Heal();
            SendLine("ok");
        }
        else if (!strcmp(cmd, "shot"))
        {
            SendLine("error no screen to save on the ROM");
        }
        else
        {
            SendLine("error unknown command");
        }
    }
}
