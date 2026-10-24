// nocash_buffer lives in its own file: tonc puts it and nocash_message (code) both in ".ewram", which one host
// object file can't hold.
#include "tonc.h"

EWRAM_DATA char nocash_buffer[NOCASH_BUF_SIZE];
