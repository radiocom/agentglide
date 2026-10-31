#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <intrin.h>

static int failures;
#define CHECK(test, name) do { if (test) printf("PASS %s\n", name); else { printf("FAIL %s error=%lu\n", name, GetLastError()); failures++; } } while (0)
static BOOL read_is(const wchar_t *path, const char *expected) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    char buffer[256] = {0}; DWORD got;
    BOOL ok = ReadFile(h, buffer, sizeof(buffer) - 1, &got, NULL); CloseHandle(h);
    return ok && !strcmp(buffer, expected);
}
static BOOL write_text(const wchar_t *path, const char *text, DWORD disposition) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disposition, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written; BOOL ok = WriteFile(h, text, (DWORD)strlen(text), &written, NULL);
    CloseHandle(h); return ok && written == strlen(text);
}
static BOOL child(const wchar_t *mode, BOOL suspended) {
    wchar_t exe[2048], command[4096]; STARTUPINFOW si = {sizeof(si)}; PROCESS_INFORMATION pi;
    GetModuleFileNameW(NULL, exe, 2048); swprintf_s(command, 4096, L"\"%s\" %s", exe, mode);
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, suspended ? CREATE_SUSPENDED : 0, NULL, NULL, &si, &pi)) return FALSE;
    if (suspended) {
        if (WaitForSingleObject(pi.hProcess, 30) != WAIT_TIMEOUT) return FALSE;
        ResumeThread(pi.hThread);
    }
    WaitForSingleObject(pi.hProcess, 30000); DWORD code = 999;
    GetExitCodeProcess(pi.hProcess, &code); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return code == 0;
}
static void enumerate_test(void) {
    WIN32_FIND_DATAW data; HANDLE find = FindFirstFileW(L"*", &data);
    int fallback = 0, created = 0, deleted = 0, renamed = 0, old = 0, edited = 0;
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!_wcsicmp(data.cFileName, L"fallback.txt")) fallback++;
            if (!_wcsicmp(data.cFileName, L"new.txt")) created++;
            if (!_wcsicmp(data.cFileName, L"delete.txt")) deleted++;
            if (!_wcsicmp(data.cFileName, L"renamed.txt")) renamed++;
            if (!_wcsicmp(data.cFileName, L"rename.txt")) old++;
            if (!_wcsicmp(data.cFileName, L"existing.txt")) edited++;
        } while (FindNextFileW(find, &data)); FindClose(find);
    }
    CHECK(fallback == 1 && created == 1 && deleted == 0 && renamed == 1 && old == 0 && edited == 1, "merged-enumeration-deduplicated");
    find = FindFirstFileW(L"fall*.txt", &data);
    CHECK(find != INVALID_HANDLE_VALUE && !_wcsicmp(data.cFileName, L"fallback.txt"), "wildcard-enumeration");
    if (find != INVALID_HANDLE_VALUE) FindClose(find);
}
int wmain(int argc, wchar_t **argv) {
    /* Regression for loader/runtime threads without the static TLS table.
       Do not call CRT or report assertions until the pointer is restored. */
    {
        typedef NTSTATUS (NTAPI *CLOSE_NATIVE)(HANDLE);
        CLOSE_NATIVE native_close = (CLOSE_NATIVE)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtClose");
        HANDLE event = CreateEventW(NULL, FALSE, FALSE, NULL);
        unsigned __int64 static_tls = __readgsqword(0x58);
        __writegsqword(0x58, 0);
        NTSTATUS status = native_close(event);
        __writegsqword(0x58, static_tls);
        CHECK(status == 0, "ntclose-without-static-tls");
    }
    if (argc > 1 && (!wcscmp(argv[1], L"raceone") || !wcscmp(argv[1], L"racetwo"))) {
        const char *tag = !wcscmp(argv[1], L"raceone") ? "RACE-ONE" : "RACE-TWO";
        CHECK(read_is(L"existing.txt", "ORIGINAL"), "concurrent-initial-read");
        CHECK(write_text(L"existing.txt", tag, CREATE_ALWAYS), "concurrent-instance-write");
        for (int i = 0; i < 30; ++i) { if (!read_is(L"existing.txt", tag)) { failures++; break; } Sleep(10); }
        CHECK(failures == 0, "concurrent-instance-retains-own-content"); return failures;
    }
    if (argc > 1 && !wcscmp(argv[1], L"grandchild")) {
        CHECK(write_text(L"grandchild.txt", "grandchild", CREATE_NEW), "grandchild-write"); return failures;
    }
    if (argc > 1 && !wcscmp(argv[1], L"child")) {
        CHECK(read_is(L"existing.txt", "EDITINAL"), "child-overlay-read");
        CHECK(GetFileAttributesW(L"delete.txt") == INVALID_FILE_ATTRIBUTES, "child-tombstone");
        CHECK(write_text(L"child.txt", "child", CREATE_NEW), "child-write");
        CHECK(child(L"grandchild", TRUE), "grandchild-injection-suspended"); return failures;
    }
    if (argc > 1 && !wcscmp(argv[1], L"isolate")) {
        CHECK(read_is(L"existing.txt", "ORIGINAL"), "instance-original-read");
        CHECK(GetFileAttributesW(L"new.txt") == INVALID_FILE_ATTRIBUTES, "instance-new-hidden");
        CHECK(read_is(L"delete.txt", "DELETE"), "instance-delete-isolated");
        CHECK(write_text(L"existing.txt", "SECOND", CREATE_ALWAYS), "instance-own-write"); return failures;
    }
    if (argc > 1 && !wcscmp(argv[1], L"persist")) {
        CHECK(read_is(L"existing.txt", "EDITINAL"), "persistent-overlay");
        CHECK(GetFileAttributesW(L"delete.txt") == INVALID_FILE_ATTRIBUTES, "persistent-tombstone");
        enumerate_test(); return failures;
    }
    CHECK(read_is(L"fallback.txt", "FALLBACK"), "read-fallback");
    CHECK(!CreateDirectoryW(L".", NULL) && GetLastError() == ERROR_ALREADY_EXISTS, "mkdir-existing-root-reports-collision");
    CHECK(write_text(L"existing.txt", "EDIT", OPEN_EXISTING), "existing-write");
    CHECK(read_is(L"existing.txt", "EDITINAL"), "copy-on-write-preserves-tail");
    {
        typedef NTSTATUS (NTAPI *QUERY_BY_NAME)(POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);
        QUERY_BY_NAME query = (QUERY_BY_NAME)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationByName");
        CHECK(write_text(L"stat.txt", "S", CREATE_ALWAYS), "stat-file-truncate-overlay");
        if (query) {
            wchar_t cwd[2048], path[2300]; GetCurrentDirectoryW(2048, cwd);
            swprintf_s(path, 2300, L"\\??\\%s\\stat.txt", cwd);
            UNICODE_STRING name = {(USHORT)(wcslen(path)*2),(USHORT)((wcslen(path)+1)*2),path};
            OBJECT_ATTRIBUTES oa = {sizeof(oa),NULL,&name,OBJ_CASE_INSENSITIVE,NULL,NULL};
            IO_STATUS_BLOCK io; ULONGLONG info[32] = {0};
            NTSTATUS status = query(&oa,&io,info,sizeof(info),(FILE_INFORMATION_CLASS)68);
            CHECK(status == 0 && info[6] == 1, "query-by-name-sees-overlay-size");
            CHECK(DeleteFileW(L"stat.txt"), "stat-file-delete");
            status = query(&oa,&io,info,sizeof(info),(FILE_INFORMATION_CLASS)68);
            CHECK(status < 0, "query-by-name-sees-tombstone");
        } else printf("SKIP query-by-name unavailable on this Windows version\n");
    }
    CHECK(write_text(L"new.txt", "NEW", CREATE_NEW), "create-new");
    CHECK(!write_text(L"fallback.txt", "BAD", CREATE_NEW), "create-new-collision-base");
    CHECK(DeleteFileW(L"delete.txt"), "delete-base-file");
    CHECK(GetFileAttributesW(L"delete.txt") == INVALID_FILE_ATTRIBUTES, "delete-hidden");
    CHECK(MoveFileExW(L"rename.txt", L"renamed.txt", 0), "rename-base-file");
    CHECK(read_is(L"renamed.txt", "RENAME") && GetFileAttributesW(L"rename.txt") == INVALID_FILE_ATTRIBUTES, "rename-visible");
    CHECK(!MoveFileExW(L"fallback.txt", L"existing.txt", 0), "rename-collision");
    CHECK(MoveFileExW(L"replace-source.txt", L"replace-target.txt", MOVEFILE_REPLACE_EXISTING), "rename-replace-base");
    CHECK(read_is(L"replace-target.txt", "SOURCE"), "rename-replace-content");
    CHECK(CreateDirectoryW(L"newdir", NULL), "create-directory");
    CHECK(write_text(L"newdir\\nested.txt", "NESTED", CREATE_NEW), "create-nested");
    CHECK(write_text(L"newdir\\\x4e2d\x6587.txt", "UNICODE", CREATE_NEW) && read_is(L"newdir\\\x4e2d\x6587.txt", "UNICODE"), "unicode-path");
    CHECK(!RemoveDirectoryW(L"newdir"), "directory-delete-explicitly-rejected");
    CHECK(!MoveFileExW(L"newdir", L"moveddir", 0), "directory-rename-explicitly-rejected");
    CHECK(!CreateHardLinkW(L"hardlink.txt", L"fallback.txt", NULL), "hardlink-explicitly-rejected");
    CHECK(!write_text(L"missing\\bad.txt", "BAD", CREATE_NEW), "missing-parent-rejected");
    CHECK(write_text(L"recreate.txt", "FIRST", CREATE_NEW) && DeleteFileW(L"recreate.txt") &&
        write_text(L"recreate.txt", "SECOND", CREATE_NEW) && read_is(L"recreate.txt", "SECOND"), "delete-recreate");
    HANDLE dc = CreateFileW(L"doc.txt", GENERIC_READ | GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    CHECK(dc != INVALID_HANDLE_VALUE, "delete-on-close-open");
    if (dc != INVALID_HANDLE_VALUE) CloseHandle(dc);
    CHECK(GetFileAttributesW(L"doc.txt") == INVALID_FILE_ATTRIBUTES, "delete-on-close-hidden");
    enumerate_test();
    {
        wchar_t cwd[2048], outside[2048], target[2048]; GetCurrentDirectoryW(2048, cwd);
        swprintf_s(outside, 2048, L"%s\\..\\outside.txt", cwd);
        swprintf_s(target, 2048, L"%s\\cross-root.txt", cwd);
        CHECK(write_text(outside, "OUTSIDE", CREATE_NEW), "outside-root-pass-through");
        CHECK(!MoveFileExW(outside, target, 0), "cross-root-move-in-rejected");
        CHECK(!CreateHardLinkW(target, outside, NULL), "cross-root-hardlink-in-rejected");
    }
    CHECK(child(L"child", FALSE), "child-inheritance");
    CHECK(read_is(L"child.txt", "child") && read_is(L"grandchild.txt", "grandchild"), "descendant-writes-visible");
    printf("RESULT failures=%d\n", failures); return failures;
}
