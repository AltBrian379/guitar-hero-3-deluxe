#include "log.h"

#include <windows.h>
#include <share.h>

#include <cstdarg>
#include <cstdio>

namespace uinput {
namespace {
FILE* g_file = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
}  // namespace

void LogOpen(const wchar_t* path) {
    if (!path) return;
    AcquireSRWLockExclusive(&g_lock);
    // Shared for reading, so the log can be opened while the game is running.
    if (!g_file) g_file = _wfsopen(path, L"w", _SH_DENYWR);
    ReleaseSRWLockExclusive(&g_lock);
}

void Log(const char* fmt, ...) {
    if (!g_file) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    AcquireSRWLockExclusive(&g_lock);
    if (g_file) {
        fprintf(g_file, "[%10lu] %s\n", GetTickCount(), buf);
        fflush(g_file);
    }
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace uinput
