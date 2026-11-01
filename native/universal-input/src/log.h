#pragma once

namespace uinput {

// Opens (truncates) the log file. Safe to call once; no-op if path is null.
void LogOpen(const wchar_t* path);
// printf-style; thread safe; no-op when the log is not open.
void Log(const char* fmt, ...);

}  // namespace uinput
