// The link partner for an ANODYNE_LINK ROM (cmake -DANODYNE_LINK=ON) on mGBA: runs the ROM with this tool on the other
// end of its link cable (JOY Bus), and passes the lines it sends and takes (anodyne/link_ipc.h) to and from a TCP
// socket, so SimHost drives the real ROM exactly as it drives the simulator:
//
//   cc -O2 mgba_link.c -o mgba_link -lmgba   (Debian/Ubuntu: apt install libmgba-dev)
//   ANODYNE_LINK_ROM=first.gba ./mgba_link [--headless] --ipc <port> [--no-render]
//   dotnet run -- ./mgba_link <port> --replay ...   (SimHost, with ANODYNE_LINK_ROM set)
//
// It takes the simulator's arguments so SimHost can start it in the simulator's place (--headless and --no-render
// change nothing here). "quit" ends it; so does the socket closing.
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/gba/interface.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/sio.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

enum
{
    kJoyCnt = 0x04000140,
    kJoyStat = 0x04000158,
    kWantLine = 0x01, // ROM -> host: send me your next line
};

static struct mCore* gCore;
static struct GBASIODriver gJoy;
static int gConn = -1;

// What the ROM has sent of its current line.
static char gOut[1 << 16];
static int gOutLen;

// The words of the host's line still to go to the ROM.
static unsigned gIn[1 << 12];
static int gInHead, gInTail;

// The socket's bytes not yet taken as lines.
static char gSock[1 << 16];
static int gSockLen;

static void Fatal(const char* msg)
{
    fprintf(stderr, "mgba_link: %s\n", msg);
    exit(1);
}

static void SendAll(const char* data, int len)
{
    while (len > 0)
    {
        const ssize_t n = send(gConn, data, len, 0);
        if (n <= 0)
        {
            exit(0); // (the bot went away)
        }
        data += n;
        len -= (int)n;
    }
}

// The bot's next line, '\n' included, into `line`; exits when the socket closes.
static int ReadSocketLine(char* line, int cap)
{
    for (;;)
    {
        char* nl = memchr(gSock, '\n', gSockLen);
        if (nl)
        {
            const int len = (int)(nl - gSock) + 1;
            if (len >= cap)
            {
                Fatal("line too long");
            }
            memcpy(line, gSock, len);
            line[len] = '\0';
            memmove(gSock, gSock + len, gSockLen - len);
            gSockLen -= len;
            return len;
        }
        if (gSockLen == (int)sizeof(gSock))
        {
            Fatal("line too long");
        }
        const ssize_t n = recv(gConn, gSock + gSockLen, sizeof(gSock) - gSockLen, 0);
        if (n <= 0)
        {
            exit(0);
        }
        gSockLen += (int)n;
    }
}

// The ROM asked for a line: queue the bot's next one as JOY Bus words.
static void QueueLine(void)
{
    char line[4096];
    const int len = ReadSocketLine(line, sizeof(line));
    if (!strcmp(line, "quit\n") || !strcmp(line, "quit\r\n"))
    {
        exit(0);
    }
    for (int i = 0; i < len; i += 4)
    {
        unsigned w = 0;
        for (int b = 0; b < 4 && i + b < len; b++)
        {
            w |= (unsigned)(unsigned char)line[i + b] << (8 * b);
        }
        gIn[gInTail++] = w;
    }
}

static void TakeByte(unsigned char c)
{
    if (c == 0)
    {
        return; // (padding)
    }
    if (c == kWantLine)
    {
        QueueLine();
        return;
    }
    if (gOutLen == (int)sizeof(gOut))
    {
        Fatal("the ROM's line is too long");
    }
    gOut[gOutLen++] = (char)c;
    if (c == '\n')
    {
        SendAll(gOut, gOutLen);
        gOutLen = 0;
    }
}

// Moves what's waiting on either side of the cable.
static void Pump(void)
{
    if (gCore->busRead16(gCore, kJoyStat) & JOYSTAT_TRANS)
    {
        uint8_t data[5];
        GBASIOJOYSendCommand(&gJoy, JOY_TRANS, data);
        for (int i = 0; i < 4; i++)
        {
            TakeByte(data[i]);
        }
    }
    // (the ROM reads a word after it acks it, so a word read is a word done with)
    if (gInHead < gInTail && !(gCore->busRead16(gCore, kJoyStat) & JOYSTAT_RECV) &&
        !(gCore->busRead16(gCore, kJoyCnt) & JOYCNT_RECV))
    {
        const unsigned w = gIn[gInHead++];
        uint8_t data[4] = { (uint8_t)w, (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24) };
        GBASIOJOYSendCommand(&gJoy, JOY_RECV, data);
        if (gInHead == gInTail)
        {
            gInHead = gInTail = 0;
        }
    }
}

static void Listen(int port)
{
    const int listener = socket(AF_INET, SOCK_STREAM, 0);
    const int one = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(listener, 1) != 0)
    {
        Fatal("can't listen");
    }
    fprintf(stderr, "mgba_link: waiting for the bot on 127.0.0.1:%d\n", port);
    gConn = accept(listener, NULL, NULL);
    if (gConn < 0)
    {
        Fatal("accept failed");
    }
    setsockopt(gConn, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    close(listener);
}

static void Quiet(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args)
{
    (void)logger;
    (void)category;
    (void)level;
    (void)format;
    (void)args;
}

int main(int argc, char** argv)
{
    int port = -1;
    const char* rom = getenv("ANODYNE_LINK_ROM");
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--ipc") && i + 1 < argc)
        {
            port = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--rom") && i + 1 < argc)
        {
            rom = argv[++i];
        }
    }
    if (port < 0 || !rom)
    {
        fprintf(stderr, "usage: ANODYNE_LINK_ROM=<first.gba> %s --ipc <port>   (or --rom <first.gba>)\n", argv[0]);
        return 1;
    }

    static struct mLogger logger = { .log = Quiet };
    mLogSetDefaultLogger(&logger);
    gCore = mCoreFind(rom);
    if (!gCore || !gCore->init(gCore))
    {
        Fatal("can't start mGBA");
    }
    mCoreInitConfig(gCore, NULL);
    // (nothing looks at the screen: don't draw it)
    mCoreConfigSetDefaultIntValue(&gCore->config, "frameskip", 1 << 30);
    mCoreLoadConfig(gCore);
    unsigned w, h;
    gCore->desiredVideoDimensions(gCore, &w, &h);
    static color_t video[256 * 256];
    gCore->setVideoBuffer(gCore, video, w);
    if (!mCoreLoadFile(gCore, rom))
    {
        Fatal("can't load the ROM");
    }
    gCore->reset(gCore);
    GBASIOJOYCreate(&gJoy);
    GBASIOSetDriver(&((struct GBA*)gCore->board)->sio, &gJoy, SIO_JOYBUS);

    Listen(port);
    for (;;)
    {
        gCore->runLoop(gCore);
        Pump();
    }
}
