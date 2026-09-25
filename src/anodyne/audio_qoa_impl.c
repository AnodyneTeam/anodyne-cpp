// Host-tooling only now (tools/audio_sim uses qoa_decode_frame as an
// independent reference) - the ROM decodes via audio_decode_core.cpp's
// streaming slice decoder and doesn't link this.
//
// qoa.h's implementation uses malloc()-returning-void* the C way, which isn't
// valid C++ - compiled here as plain C (like the host tool's qoa_impl.c) so
// audio_manager.cpp (C++) can call straight into it via extern "C".
// QOA_NO_STDIO: no file I/O on-device, we only ever decode frames out of the
// in-ROM audio_blob.bin pool.
#define QOA_IMPLEMENTATION
#define QOA_NO_STDIO
#include "qoa.h"
