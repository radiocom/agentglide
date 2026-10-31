#ifndef AGENTGLIDE_COMMON_H
#define AGENTGLIDE_COMMON_H
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#define AGENTGLIDE_PATH 2048
#define AGENTGLIDE_MAGIC 0x56465331
#define AGENTGLIDE_MAGIC_LAYOUT1 0x56465332
typedef struct AGENTGLIDE_CONFIG {
    DWORD magic;
    wchar_t base[AGENTGLIDE_PATH];
    wchar_t instance[AGENTGLIDE_PATH];
    wchar_t dll[AGENTGLIDE_PATH];
} AGENTGLIDE_CONFIG;
BOOL agentglide_inject(HANDLE process, const AGENTGLIDE_CONFIG *config);
#endif
