/* Tiny native entry: keeps Windows argv exact; PowerShell 7 hosts orchestration. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
static int append(wchar_t *cmd, const wchar_t *s) {
    size_t n = wcslen(cmd), bs = 0;
    if (n) cmd[n++] = L' ';
    if (n + 4 >= 32768) return 0;
    cmd[n++] = L'"';
    for (;;) {
        wchar_t c = *s++;
        if (c == L'\\') { bs++; continue; }
        size_t count = (c == L'"' || !c) ? bs * 2 : bs;
        if (n + count + 4 >= 32768) return 0;
        while (count--) cmd[n++] = L'\\';
        bs = 0;
        if (!c) break;
        if (c == L'"') cmd[n++] = L'\\';
        cmd[n++] = c;
    }
    cmd[n++] = L'"'; cmd[n] = 0; return 1;
}
int wmain(int argc, wchar_t **argv) {
    wchar_t exe[32768], script[32768], cmd[32768] = L"";
    DWORD n = SearchPathW(NULL, L"pwsh.exe", NULL, 32768, exe, NULL);
    if (!n || n >= 32768) { fputs("agentglide requires PowerShell 7 (pwsh.exe) on PATH\n", stderr); return 2; }
    GetModuleFileNameW(NULL, script, 32768);
    wcscpy_s(wcsrchr(script, L'\\') + 1, 32, L"agentglide.ps1");
    if (!append(cmd, exe) || !append(cmd, L"-NoLogo") || !append(cmd, L"-NoProfile") ||
        !append(cmd, L"-File") || !append(cmd, script)) return 2;
    for (int i = 1; i < argc; i++) if (!append(cmd, argv[i])) return 2;
    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi = {0};
    if (!CreateProcessW(exe, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) return 3;
    CloseHandle(pi.hThread); WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 3; GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hProcess);
    return (int)code;
}
