#include "common.h"
#include <tlhelp32.h>
#include <stdio.h>
#include <psapi.h>

/* Resolve system exports by module + RVA, not by assuming equal ASLR bases. */
static void *remote_export(HANDLE process, const wchar_t *module, const char *name) {
    HMODULE local = GetModuleHandleW(module), owner = NULL;
    FARPROC proc = local ? GetProcAddress(local, name) : NULL;
    wchar_t path[AGENTGLIDE_PATH];
    MODULEENTRY32W ent = { sizeof(ent) };
    HANDLE snap;
    void *result = NULL;
    if (!proc || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)proc, &owner)) return NULL;
    if (!GetModuleFileNameW(owner, path, AGENTGLIDE_PATH)) return NULL;
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetProcessId(process));
    if (snap != INVALID_HANDLE_VALUE && Module32FirstW(snap, &ent)) do {
        if (!_wcsicmp(ent.szModule, wcsrchr(path, L'\\') + 1)) {
            result = ent.modBaseAddr + ((BYTE *)proc - (BYTE *)owner);
            break;
        }
    } while (Module32NextW(snap, &ent));
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    /* A newly suspended process has no initialized loader module list yet.
       ntdll is already mapped: enumerate image allocations for this bootstrap. */
    if (!result) {
        MEMORY_BASIC_INFORMATION mbi;
        BYTE *cursor = NULL;
        while (VirtualQueryEx(process, cursor, &mbi, sizeof(mbi)) == sizeof(mbi)) {
            if (mbi.Type == MEM_IMAGE && mbi.BaseAddress == mbi.AllocationBase) {
                wchar_t mapped[AGENTGLIDE_PATH];
                if (GetMappedFileNameW(process, mbi.AllocationBase, mapped, AGENTGLIDE_PATH)) {
                    const wchar_t *tail = wcsrchr(mapped, L'\\');
                    if (tail && !_wcsicmp(tail + 1, wcsrchr(path, L'\\') + 1)) {
                        result = (BYTE *)mbi.AllocationBase + ((BYTE *)proc - (BYTE *)owner); break;
                    }
                }
            }
            BYTE *next = (BYTE *)mbi.BaseAddress + mbi.RegionSize;
            if (next <= cursor) break; cursor = next;
        }
    }
    return result;
}

BOOL agentglide_inject(HANDLE process, const AGENTGLIDE_CONFIG *config) {
    void *mem = NULL, *loader;
    HANDLE thread = NULL, snap = INVALID_HANDLE_VALUE;
    HMODULE image = NULL;
    MODULEENTRY32W ent = { sizeof(ent) };
    BYTE *remote = NULL;
    DWORD code = 0;
    BOOL ok = FALSE;
    const char *stage = "architecture";
    USHORT processMachine, nativeMachine;
    if (!IsWow64Process2(process, &processMachine, &nativeMachine) ||
        processMachine != IMAGE_FILE_MACHINE_UNKNOWN || nativeMachine != IMAGE_FILE_MACHINE_AMD64) {
        SetLastError(ERROR_BAD_EXE_FORMAT); return FALSE;
    }
    stage = "resolve LoadLibraryW";
    loader = remote_export(process, L"kernel32.dll", "LoadLibraryW");
    if (!loader) {
        stage = "bootstrap loader without resuming primary thread";
        void *exit_thread = remote_export(process, L"ntdll.dll", "RtlExitUserThread");
        if (!exit_thread) goto done;
        thread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)exit_thread, NULL, 0, NULL);
        if (!thread || WaitForSingleObject(thread, 30000) != WAIT_OBJECT_0) goto done;
        CloseHandle(thread); thread = NULL;
        loader = remote_export(process, L"kernel32.dll", "LoadLibraryW");
    }
    if (!loader) goto done;
    stage = "allocate/write remote DLL path";
    mem = VirtualAllocEx(process, NULL, sizeof(*config), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mem || !WriteProcessMemory(process, mem, config->dll,
        (wcslen(config->dll) + 1) * sizeof(wchar_t), NULL)) goto done;
    stage = "LoadLibraryW remote thread";
    thread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)loader, mem, 0, NULL);
    if (!thread || WaitForSingleObject(thread, 30000) != WAIT_OBJECT_0) goto done;
    CloseHandle(thread); thread = NULL;
    stage = "find loaded DLL";
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetProcessId(process));
    if (snap == INVALID_HANDLE_VALUE) goto done;
    if (Module32FirstW(snap, &ent)) do {
        if (!_wcsicmp(ent.szExePath, config->dll)) { remote = ent.modBaseAddr; break; }
    } while (Module32NextW(snap, &ent));
    if (!remote) goto done;
    stage = "map local DLL exports";
    image = LoadLibraryExW(config->dll, NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (!image) goto done;
    {
        FARPROC init = GetProcAddress(image, "UsvfsInitialize");
        if (!init || !WriteProcessMemory(process, mem, config, sizeof(*config), NULL)) goto done;
        stage = "UsvfsInitialize remote thread";
        thread = CreateRemoteThread(process, NULL, 0,
            (LPTHREAD_START_ROUTINE)(remote + ((BYTE *)init - (BYTE *)image)), mem, 0, NULL);
    }
    if (!thread || WaitForSingleObject(thread, 30000) != WAIT_OBJECT_0 ||
        !GetExitCodeThread(thread, &code) || code != AGENTGLIDE_MAGIC) goto done;
    ok = TRUE;
done:
    if (!ok) fprintf(stderr, "inject pid=%lu stage=%s win32=%lu init=%08lx\n", GetProcessId(process), stage, GetLastError(), code);
    if (thread) CloseHandle(thread);
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    if (image) FreeLibrary(image);
    /* On failure the caller terminates the suspended process before it can run.
       Keep remote memory alive on timeout for a possibly still-running thread. */
    if (mem && ok) VirtualFreeEx(process, mem, 0, MEM_RELEASE);
    if (!ok) SetLastError(ERROR_DLL_INIT_FAILED);
    return ok;
}
