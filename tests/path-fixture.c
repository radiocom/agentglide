#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
static int failures;
#define CHECK(test,name) do { if(test) printf("PASS %s\n",name); else {printf("FAIL %s error=%lu\n",name,GetLastError());failures++;}}while(0)
typedef NTSTATUS (NTAPI *INFO_FN)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);
static HANDLE open_file(const wchar_t *path, DWORD access, DWORD creation) {
    return CreateFileW(path,access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,creation,FILE_FLAG_BACKUP_SEMANTICS,NULL);
}
static void names(HANDLE h,const wchar_t *expected) {
    wchar_t result[4096],want[4096],drive[4]={expected[0],L':',L'\\',0},volume[128];
    swprintf_s(want,4096,L"\\\\?\\%s",expected);
    DWORD n=GetFinalPathNameByHandleW(h,result,4096,0);
    CHECK(n==wcslen(want)&&!wcscmp(result,want),"final-dos-virtual");
    CHECK(GetFinalPathNameByHandleW(h,NULL,0,0)==n+1,"final-size-includes-nul");
    wcscpy_s(result,4096,L"GUARD");
    CHECK(GetFinalPathNameByHandleW(h,result,n,0)==n+1&&!wcscmp(result,L"GUARD"),"final-short-buffer-unmodified");
    CHECK(GetFinalPathNameByHandleW(h,result,n+1,0)==n&&!wcscmp(result,want),"final-exact-buffer");
    CHECK(GetFinalPathNameByHandleW(h,result,4096,FILE_NAME_OPENED)==n&&!wcscmp(result,want),"final-opened-name");
    char ansi[8192],wantedAnsi[8192];
    WideCharToMultiByte(CP_ACP,0,want,-1,wantedAnsi,8192,NULL,NULL);
    DWORD an=GetFinalPathNameByHandleA(h,ansi,8192,0);
    CHECK(an==strlen(wantedAnsi)&&!strcmp(ansi,wantedAnsi),"final-ansi");
    DWORD asize=GetFinalPathNameByHandleA(h,NULL,0,0);
    CHECK(asize==an,"final-ansi-size");
    CHECK(GetFinalPathNameByHandleW(h,result,4096,VOLUME_NAME_NONE)&&!wcscmp(result,expected+2),"final-volume-none");
    wchar_t dos[3]={expected[0],L':',0};
    QueryDosDeviceW(dos,volume,128);swprintf_s(want,4096,L"%s%s",volume,expected+2);
    CHECK(GetFinalPathNameByHandleW(h,result,4096,VOLUME_NAME_NT)&&!wcscmp(result,want),"final-volume-nt");
    BOOL volumeOk=GetVolumeNameForVolumeMountPointW(drive,volume,128);swprintf_s(want,4096,L"%s%s",volume,expected+3);
    DWORD guidSize=GetFinalPathNameByHandleW(h,result,4096,VOLUME_NAME_GUID);
    CHECK(volumeOk&&guidSize&&!_wcsicmp(result,want),"final-volume-guid");
    CHECK(GetFinalPathNameByHandleW(h,result,4096,VOLUME_NAME_NT|VOLUME_NAME_GUID)==0,"final-invalid-flags");
    INFO_FN info=(INFO_FN)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtQueryInformationFile");
    for(int k=0;k<2;k++){
        BYTE raw[8192];IO_STATUS_BLOCK io={0};memset(raw,0x5a,sizeof(raw));
        NTSTATUS status=info(h,&io,raw,sizeof(raw),(FILE_INFORMATION_CLASS)(k?48:9));
        ULONG bytes=(ULONG)(wcslen(expected+2)*2);
        CHECK(status==0&&*(ULONG*)raw==bytes&&io.Information==bytes+4&&!memcmp(raw+4,expected+2,bytes),"native-name-virtual");
        memset(raw,0x5a,sizeof(raw));
        status=info(h,&io,raw,8,(FILE_INFORMATION_CLASS)(k?48:9));
        CHECK(status==(NTSTATUS)0x80000005L&&*(ULONG*)raw==bytes&&io.Information==8&&raw[8]==0x5a,"native-name-short-buffer");
    }
}
int wmain(int argc,wchar_t **argv){
    if(argc!=2)return 2;
    wchar_t cwd[2048],path[4096],physical[4096],result[4096];
    GetCurrentDirectoryW(2048,cwd);
    HANDLE h=open_file(L".",0,OPEN_EXISTING);
    CHECK(h!=INVALID_HANDLE_VALUE,"open-root");names(h,cwd);CloseHandle(h);
    swprintf_s(path,4096,L"%s\\fallback.txt",cwd);
    h=open_file(L"fallback.txt",GENERIC_READ,OPEN_EXISTING);names(h,path);CloseHandle(h);
    h=open_file(L"new-\x4e2d\x6587.txt",GENERIC_READ|GENERIC_WRITE,CREATE_NEW);
    DWORD written;CHECK(h!=INVALID_HANDLE_VALUE&&WriteFile(h,"NEW",3,&written,NULL)&&written==3,"create-unicode-overlay");
    swprintf_s(path,4096,L"%s\\new-\x4e2d\x6587.txt",cwd);names(h,path);
    HANDLE duplicate=NULL;
    CHECK(DuplicateHandle(GetCurrentProcess(),h,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS),"duplicate-virtual-handle");
    CloseHandle(h);names(duplicate,path);
    CHECK(MoveFileExW(L"new-\x4e2d\x6587.txt",L"renamed.txt",0),"rename-open-file");
    swprintf_s(path,4096,L"%s\\renamed.txt",cwd);names(duplicate,path);CloseHandle(duplicate);
    swprintf_s(physical,4096,L"%s\\files\\renamed.txt",argv[1]);
    for(int i=0;i<8;i++){
        h=open_file(physical,GENERIC_READ,OPEN_EXISTING);
        swprintf_s(path,4096,L"\\\\?\\%s",physical);
        CHECK(GetFinalPathNameByHandleW(h,result,4096,0)&&!wcscmp(result,path),"direct-overlay-handle-not-virtualized");
        CloseHandle(h);
    }
    wchar_t outside[2048];GetFullPathNameW(L"..\\outside.txt",2048,outside,NULL);
    h=open_file(outside,GENERIC_READ|GENERIC_WRITE,CREATE_NEW);names(h,outside);CloseHandle(h);
    printf("RESULT failures=%d\n",failures);return failures;
}
