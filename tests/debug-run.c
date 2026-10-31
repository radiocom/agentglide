#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <stdio.h>
#include <wchar.h>

typedef struct TRACK { DWORD pid; HANDLE process; } TRACK;
static TRACK processes[128];
static void location(DWORD pid, DWORD64 address) {
    MODULEENTRY32W module = {sizeof(module)};
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap != INVALID_HANDLE_VALUE && Module32FirstW(snap, &module)) do {
        if (address >= (DWORD64)module.modBaseAddr && address < (DWORD64)module.modBaseAddr + module.modBaseSize) {
            wprintf(L" %s+0x%llx", module.szModule, address - (DWORD64)module.modBaseAddr); break;
        }
    } while (Module32NextW(snap, &module));
    if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
}
int wmain(void) {
    wchar_t *command = wcsstr(GetCommandLineW(), L" -- ");
    if (!command) { puts("Usage: debug-run -- COMMAND ARGS"); return 2; }
    STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi;
    if (!CreateProcessW(NULL, command + 4, NULL, NULL, TRUE, DEBUG_PROCESS, NULL, NULL, &si, &pi)) return 3;
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    DWORD active = 0, root_exit = 0;
    DEBUG_EVENT event;
    while (WaitForDebugEvent(&event, INFINITE)) {
        DWORD status = DBG_CONTINUE;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
            for (int i=0;i<128;i++) if (!processes[i].pid) {
                processes[i].pid=event.dwProcessId; processes[i].process=event.u.CreateProcessInfo.hProcess;
                SymInitialize(processes[i].process, NULL, TRUE); break;
            }
            CloseHandle(event.u.CreateProcessInfo.hThread);
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
            active++;
        } else if (event.dwDebugEventCode == CREATE_THREAD_DEBUG_EVENT) CloseHandle(event.u.CreateThread.hThread);
        else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
        else if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
            wprintf(L"EXIT pid=%lu code=%08lx\n",event.dwProcessId,event.u.ExitProcess.dwExitCode);
            if(event.dwProcessId==pi.dwProcessId)root_exit=event.u.ExitProcess.dwExitCode;
            for(int i=0;i<128;i++) if(processes[i].pid==event.dwProcessId) {
                SymCleanup(processes[i].process); CloseHandle(processes[i].process); processes[i].pid=0;
            }
            active--;
        } else if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            EXCEPTION_RECORD *exception=&event.u.Exception.ExceptionRecord;
            if(exception->ExceptionCode != EXCEPTION_BREAKPOINT && exception->ExceptionCode != EXCEPTION_SINGLE_STEP) {
                status=DBG_EXCEPTION_NOT_HANDLED;
                if(exception->ExceptionCode==EXCEPTION_ACCESS_VIOLATION || !event.u.Exception.dwFirstChance) {
                    wprintf(L"EXCEPTION pid=%lu tid=%lu code=%08lx first=%lu addr=%p",event.dwProcessId,event.dwThreadId,exception->ExceptionCode,event.u.Exception.dwFirstChance,exception->ExceptionAddress);
                    location(event.dwProcessId,(DWORD64)exception->ExceptionAddress); wprintf(L"\n");
                    for(DWORD j=0;j<exception->NumberParameters;j++) wprintf(L"param%lu=%llx ",j,(DWORD64)exception->ExceptionInformation[j]);
                    wprintf(L"\n");
                    HANDLE process=NULL;
                    for(int i=0;i<128;i++) if(processes[i].pid==event.dwProcessId) process=processes[i].process;
                    HANDLE thread=OpenThread(THREAD_ALL_ACCESS,FALSE,event.dwThreadId);
                    CONTEXT context={0}; context.ContextFlags=CONTEXT_ALL;
                    if(process && thread && GetThreadContext(thread,&context)) {
                        wprintf(L"rip=%llx rsp=%llx rcx=%llx rdx=%llx r8=%llx r9=%llx\n",context.Rip,context.Rsp,context.Rcx,context.Rdx,context.R8,context.R9);
                        STACKFRAME64 frame={0}; frame.AddrPC.Offset=context.Rip; frame.AddrStack.Offset=context.Rsp; frame.AddrFrame.Offset=context.Rbp;
                        frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
                        SymRefreshModuleList(process);
                        for(int f=0;f<18 && StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,NULL,SymFunctionTableAccess64,SymGetModuleBase64,NULL);f++) {
                            wprintf(L"frame %llx",frame.AddrPC.Offset); location(event.dwProcessId,frame.AddrPC.Offset);
                            char storage[sizeof(SYMBOL_INFO)+1024]; SYMBOL_INFO *symbol=(SYMBOL_INFO *)storage;
                            ZeroMemory(storage,sizeof(storage)); symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=1023;
                            DWORD64 displacement;
                            if(SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol)) wprintf(L" %hs+0x%llx",symbol->Name,displacement);
                            wprintf(L"\n");
                        }
                    }
                    if(thread)CloseHandle(thread);
                }
            }
        }
        ContinueDebugEvent(event.dwProcessId,event.dwThreadId,status);
        fflush(stdout);
        if(!active)break;
    }
    return (int)root_exit;
}
