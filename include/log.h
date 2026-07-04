#pragma once
#include <cstdio>

void Log_Init(const char* filename);
void Log_Close();
void Log_Printf(const char* fmt, ...);

// Convenience macro; compiles to nothing if POP2_NO_LOG is defined.
#ifdef POP2_NO_LOG
  #define LOG(...) ((void)0)
#else
  #define LOG(...) Log_Printf(__VA_ARGS__)
#endif
