// QOA_NO_STDIO: we never read/write whole .qoa files, only encode/decode
// individual frames into our own blob format (see audio_compiler.cpp).
#define QOA_IMPLEMENTATION
#define QOA_NO_STDIO
#include "qoa.h"
