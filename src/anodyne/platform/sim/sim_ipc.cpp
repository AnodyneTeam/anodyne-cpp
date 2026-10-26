#include "sim_ipc.h"
#include "sim_frontend.h"
#include "sim_internal.h"

#include "game_probe.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <vector>

// Weak defaults so the simulator links against a game that has no probe yet.
extern "C" __attribute__((weak)) int GameProbe_Json(char* buf, int cap)
{
    return snprintf(buf, static_cast<size_t>(cap), "{}");
}
extern "C" __attribute__((weak)) int GameProbe_Collision(char* buf, int cap)
{
    return snprintf(buf, static_cast<size_t>(cap), "0 0 ");
}
extern "C" __attribute__((weak)) int GameProbe_TraceHeader(char* buf, int cap)
{
    return snprintf(buf, static_cast<size_t>(cap), "frame");
}
extern "C" __attribute__((weak)) int GameProbe_TraceRow(unsigned long long frame, char* buf, int cap)
{
    return snprintf(buf, static_cast<size_t>(cap), "%llu", frame);
}

namespace sim
{
namespace
{
int gListen = -1;
int gConn = -1;
uint16_t gKeys = 0;
uint32_t gHold = 0; // frames the current keys still have to be held
std::string gInBuf;
std::vector<char> gProbeBuf(1 << 16);
FILE* gTrace = nullptr;

void SendLine(const std::string& s)
{
    std::string line = s + "\n";
    size_t sent = 0;
    while (sent < line.size())
    {
        const ssize_t n = send(gConn, line.data() + sent, line.size() - sent, MSG_NOSIGNAL);
        if (n <= 0)
        {
            Log("bot disconnected");
            exit(0);
        }
        sent += static_cast<size_t>(n);
    }
}

bool ReadLine(std::string& out)
{
    for (;;)
    {
        const size_t nl = gInBuf.find('\n');
        if (nl != std::string::npos)
        {
            out = gInBuf.substr(0, nl);
            gInBuf.erase(0, nl + 1);
            if (!out.empty() && out.back() == '\r')
            {
                out.pop_back();
            }
            return true;
        }
        char buf[4096];
        const ssize_t n = recv(gConn, buf, sizeof(buf), 0);
        if (n <= 0)
        {
            return false;
        }
        gInBuf.append(buf, static_cast<size_t>(n));
    }
}

std::string StateLine(uint64_t frame)
{
    GameProbe_Json(gProbeBuf.data(), static_cast<int>(gProbeBuf.size()));
    char head[64];
    snprintf(head, sizeof(head), "{\"frame\":%llu,\"state\":", static_cast<unsigned long long>(frame));
    return std::string(head) + gProbeBuf.data() + "}";
}

std::string Peek(uint32_t addr, uint32_t len)
{
    if (len > 65536 || !IsMapped(addr, len))
    {
        return "error unmapped";
    }
    std::string out;
    out.reserve(len * 2);
    static const char kHex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < len; i++)
    {
        const uint32_t a = addr + i;
        const uint8_t b = (a >= kIoBase && a < kIoBase + kIoSize) ? Io()[a - kIoBase]
                                                                     : *reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(a));
        out += kHex[b >> 4];
        out += kHex[b & 15];
    }
    return out;
}
} // namespace

void Ipc_Listen(int port)
{
    gListen = socket(AF_INET, SOCK_STREAM, 0);
    const int one = 1;
    setsockopt(gListen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(gListen, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(gListen, 1) != 0)
    {
        Fatal("can't listen on 127.0.0.1:%d", port);
    }
    Log("waiting for the bot on 127.0.0.1:%d", port);
    gConn = accept(gListen, nullptr, nullptr);
    if (gConn < 0)
    {
        Fatal("accept failed");
    }
    setsockopt(gConn, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    Log("bot connected");
}

void Ipc_Close()
{
    if (gConn >= 0)
    {
        close(gConn);
        gConn = -1;
    }
    if (gListen >= 0)
    {
        close(gListen);
        gListen = -1;
    }
}

bool Ipc_Active()
{
    return gConn >= 0;
}

uint16_t Ipc_Keys()
{
    return gKeys;
}

void Ipc_OnFrame(uint64_t frame, const uint32_t* pixels)
{
    if (gHold > 1)
    {
        gHold--;
        return;
    }
    gHold = 0;
    SendLine(StateLine(frame));
    std::string line;
    while (ReadLine(line))
    {
        char cmd[16] = {};
        char arg[512] = {};
        unsigned a = 0;
        unsigned b = 0;
        sscanf(line.c_str(), "%15s", cmd);
        if (!strcmp(cmd, "keys"))
        {
            unsigned frames = 1;
            if (sscanf(line.c_str(), "keys %x %u", &a, &frames) >= 1)
            {
                gKeys = static_cast<uint16_t>(a & 0x3FF);
                gHold = frames ? frames : 1;
                return;
            }
            SendLine("error usage: keys <hex mask> [frames]");
        }
        else if (!strcmp(cmd, "shot") && sscanf(line.c_str(), "shot %511s", arg) == 1)
        {
            SendLine(Frontend_SavePng(pixels, arg) ? "ok" : "error can't write");
        }
        else if (!strcmp(cmd, "peek") && sscanf(line.c_str(), "peek %x %u", &a, &b) == 2)
        {
            SendLine(Peek(a, b));
        }
        else if (!strcmp(cmd, "state"))
        {
            SendLine(StateLine(frame));
        }
        else if (!strcmp(cmd, "collision"))
        {
            std::vector<char> buf(1 << 16);
            GameProbe_Collision(buf.data(), static_cast<int>(buf.size()));
            SendLine(buf.data());
        }
        else if (!strcmp(cmd, "warp") && sscanf(line.c_str(), "warp %63s %u %u", arg, &a, &b) == 3)
        {
            SendLine(GameProbe_Warp(arg, static_cast<int>(a), static_cast<int>(b)) ? "ok" : "error no such map");
        }
        else if (!strcmp(cmd, "rng") && sscanf(line.c_str(), "rng %u", &a) == 1)
        {
            // ("rng N keep": the screen shake carries on, as when a replay picks up again after the title)
            GameProbe_SetRng(a, sscanf(line.c_str(), "rng %*u %63s", arg) == 1 && !strcmp(arg, "keep"));
            SendLine("ok");
        }
        else if (!strcmp(cmd, "checkpoint") && sscanf(line.c_str(), "checkpoint %63s %u %u", arg, &a, &b) == 3)
        {
            SendLine(GameProbe_SetCheckpoint(arg, static_cast<int>(a), static_cast<int>(b)) ? "ok" : "error no such map");
        }
        else if (!strcmp(cmd, "heal"))
        {
            GameProbe_Heal();
            SendLine("ok");
        }
        else if (!strcmp(cmd, "quit"))
        {
            exit(0);
        }
        else
        {
            SendLine("error unknown command");
        }
    }
    Log("bot disconnected");
    exit(0);
}

void Trace_Open(const char* path)
{
    if (!path)
    {
        return;
    }
    gTrace = fopen(path, "w");
    if (!gTrace)
    {
        Fatal("can't write %s", path);
    }
    GameProbe_TraceHeader(gProbeBuf.data(), static_cast<int>(gProbeBuf.size()));
    fprintf(gTrace, "%s\n", gProbeBuf.data());
}

void Trace_Frame(uint64_t frame)
{
    if (!gTrace)
    {
        return;
    }
    GameProbe_TraceRow(frame, gProbeBuf.data(), static_cast<int>(gProbeBuf.size()));
    fprintf(gTrace, "%s\n", gProbeBuf.data());
}

void Trace_Close()
{
    if (gTrace)
    {
        fclose(gTrace);
        gTrace = nullptr;
    }
}
} // namespace sim
