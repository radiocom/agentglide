#include "common.h"
#include <stdio.h>

static BOOL absolute(const wchar_t *in, wchar_t *out) {
    DWORD n = GetFullPathNameW(in, AGENTGLIDE_PATH, out, NULL);
    if (!n || n >= AGENTGLIDE_PATH) return FALSE;
    while (n > 3 && out[n - 1] == L'\\') out[--n] = 0;
    return TRUE;
}
static BOOL within(const wchar_t *path, const wchar_t *root) {
    size_t n = wcslen(root);
    return !_wcsnicmp(path, root, n) && (path[n] == 0 || path[n] == L'\\');
}
/* Windows command-line quoting, including trailing backslashes. */
static BOOL arg(wchar_t *cmd, size_t cap, const wchar_t *value) {
    size_t p = wcslen(cmd), slashes = 0;
    if (p && p < cap - 1) cmd[p++] = L' ';
    if (p >= cap - 2) return FALSE;
    cmd[p++] = L'"';
    for (;;) {
        wchar_t c = *value++;
        if (c == L'\\') { slashes++; continue; }
        size_t count = (c == L'"' || !c) ? slashes * 2 : slashes;
        if (p + count + 3 >= cap) return FALSE;
        while (count--) cmd[p++] = L'\\';
        slashes = 0;
        if (!c) break;
        if (c == L'"') cmd[p++] = L'\\';
        cmd[p++] = c;
    }
    cmd[p++] = L'"'; cmd[p] = 0; return TRUE;
}
int wmain(int argc, wchar_t **argv) {
    AGENTGLIDE_CONFIG config = { AGENTGLIDE_MAGIC };
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    wchar_t command[32768] = L"";
    DWORD code;
    if (argc > 1 && !wcscmp(argv[1], L"--layout-v1")) {
        config.magic = AGENTGLIDE_MAGIC_LAYOUT1; argc--; argv++;
    }
    if (argc < 5 || wcscmp(argv[3], L"--")) {
        fwprintf(stderr, L"Usage: agentglide-run.exe BASE INSTANCE -- PROGRAM [ARGS...]\n"); return 2;
    }
    if (!absolute(argv[1], config.base) || !absolute(argv[2], config.instance) ||
        wcslen(config.base) <= 3 || config.base[1] != L':' || config.base[2] != L'\\' ||
        wcslen(config.instance) <= 3 || config.instance[1] != L':' || config.instance[2] != L'\\' ||
        within(config.instance, config.base) || within(config.base, config.instance) ||
        GetFileAttributesW(config.base) == INVALID_FILE_ATTRIBUTES ||
        !(GetFileAttributesW(config.base) & FILE_ATTRIBUTE_DIRECTORY)) {
        fwprintf(stderr, L"BASE must be an existing directory; BASE and INSTANCE must be disjoint local drive subdirectories.\n"); return 2;
    }
    GetModuleFileNameW(NULL, config.dll, AGENTGLIDE_PATH);
    wcscpy_s(wcsrchr(config.dll, L'\\') + 1, 64, L"agentglide.dll");
    for (int i = 4; i < argc; ++i) if (!arg(command, 32768, argv[i])) return 2;
    if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, CREATE_SUSPENDED,
        NULL, config.base, &si, &pi)) {
        fwprintf(stderr, L"CreateProcess failed: %lu\n", GetLastError()); return 3;
    }
    if (!agentglide_inject(pi.hProcess, &config)) {
        fwprintf(stderr, L"Injection failed: %lu; target terminated before resume\n", GetLastError());
        TerminateProcess(pi.hProcess, 125); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return 125;
    }
    ResumeThread(pi.hThread); CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess);
    return (int)code;
}
