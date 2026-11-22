/* Linux-only compatibility hooks shared by the normal loader and native test binaries.
 * The GPU library calls these optional Windows hardware-breakpoint diagnostics when enabled.
 * Linux does not implement that diagnostic path, so these hooks intentionally do nothing. */
#include <stdint.h>

#ifndef _WIN32
void runtime_exec_watch_retarget(uint64_t address) {
    (void)address;
}

void runtime_exec_watch_set_reads(const uint64_t *addresses, int count) {
    (void)addresses;
    (void)count;
}
#endif
