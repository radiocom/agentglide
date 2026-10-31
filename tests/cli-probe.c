/* Local CLI contract fixture, never presented as a real agent test. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <io.h>
#include <fcntl.h>
#include <string.h>
int wmain(int argc, wchar_t **argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    const wchar_t *mode = argc > 1 ? argv[argc-1] : L"";
    wchar_t cwd[2048]; GetCurrentDirectoryW(2048, cwd); wprintf(L"cwd=%s\n",cwd);
    if (!wcscmp(mode,L"probe-write")) {
        HANDLE f=CreateFileW(L"existing.txt",GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,0,NULL);
        DWORD n=0; if(f==INVALID_HANDLE_VALUE)return 4;
        BOOL ok=WriteFile(f,"PROBE",5,&n,NULL); CloseHandle(f); return ok&&n==5 ? 0:5;
    }
    if (!wcscmp(mode,L"probe-read")) {
        char data[6]={0}; DWORD n=0;
        HANDLE f=CreateFileW(L"existing.txt",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
        if(f==INVALID_HANDLE_VALUE)return 6;
        ReadFile(f,data,5,&n,NULL); CloseHandle(f); return !strcmp(data,"PROBE")?0:7;
    }
    if (!wcscmp(mode,L"probe-wait")) { Sleep(8000); return 0; }
    return 8;
}
