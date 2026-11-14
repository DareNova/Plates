#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef void (*PlateSyncLog)(const char*,...);
void PlateSyncRunRender(HMODULE,const wchar_t*,const wchar_t*,PlateSyncLog);
