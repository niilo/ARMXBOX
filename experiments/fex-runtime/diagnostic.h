#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
// One-shot, process-owned probe. No FEX/C++ types cross this boundary.
// Returns 0 on fixture success, 1 on failure, 2 on an unsupported prerequisite.
// Output is always NUL-terminated when capacity > 0; no ownership transfer.
__attribute__((visibility("default"))) int armxbox_fex_selftest(char* output, size_t capacity);
// Borrowed descriptor for a bounded persistent progress journal. Install before
// execution; reset to -1 before the caller closes it. No signal-handler use.
__attribute__((visibility("default"))) void armxbox_fex_set_trace_fd(int fd);
#ifdef __cplusplus
}
#endif
