#pragma once

// Pulls in qoa.h's declarations only (QOA_IMPLEMENTATION is intentionally not
// defined here - that lives in qoa_impl.c, compiled as plain C, since qoa.h's
// malloc()-returning-void* usage isn't valid C++). Wrapped for C linkage so
// C++ translation units can call straight into the C-compiled implementation.
#define QOA_NO_STDIO
extern "C"
{
#include "qoa.h"
}
