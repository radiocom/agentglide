#include "common.h"
#include <winternl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <wctype.h>
#include "MinHook.h"

#define S_NOT_FOUND ((NTSTATUS)0xC0000034L)
#define S_PATH_NOT_FOUND ((NTSTATUS)0xC000003AL)
#define S_DENIED ((NTSTATUS)0xC0000022L)
#define S_COLLISION ((NTSTATUS)0xC0000035L)
#define S_UNSUPPORTED ((NTSTATUS)0xC00000BBL)
#define S_NO_MORE ((NTSTATUS)0x80000006L)
#define S_OVERFLOW ((NTSTATUS)0x80000005L)
#define S_SMALL ((NTSTATUS)0xC0000023L)
#define S_INVALID ((NTSTATUS)0xC000000DL)
#define S_MEMORY ((NTSTATUS)0xC0000017L)
#define SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define DELETE_ON_CLOSE 0x1000
#define DIRECTORY_FILE 1

typedef NTSTATUS (NTAPI *CREATE_FN)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,PLARGE_INTEGER,ULONG,ULONG,ULONG,ULONG,PVOID,ULONG);
typedef NTSTATUS (NTAPI *OPEN_FN)(PHANDLE,ACCESS_MASK,POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,ULONG,ULONG);
typedef NTSTATUS (NTAPI *ATTR_FN)(POBJECT_ATTRIBUTES,PVOID);
typedef NTSTATUS (NTAPI *BYNAME_FN)(POBJECT_ATTRIBUTES,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);
typedef NTSTATUS (NTAPI *SET_FN)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);
typedef NTSTATUS (NTAPI *CLOSE_FN)(HANDLE);
typedef NTSTATUS (NTAPI *INFO_FN)(HANDLE,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS);
typedef NTSTATUS (NTAPI *DUP_FN)(HANDLE,HANDLE,HANDLE,PHANDLE,ACCESS_MASK,ULONG,ULONG);
typedef DWORD (WINAPI *FINAL_W_FN)(HANDLE,LPWSTR,DWORD,DWORD);
typedef DWORD (WINAPI *FINAL_A_FN)(HANDLE,LPSTR,DWORD,DWORD);
typedef NTSTATUS (NTAPI *QUERY_FN)(HANDLE,HANDLE,PIO_APC_ROUTINE,PVOID,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS,BOOLEAN,PUNICODE_STRING,BOOLEAN);
typedef NTSTATUS (NTAPI *QUERY_EX_FN)(HANDLE,HANDLE,PIO_APC_ROUTINE,PVOID,PIO_STATUS_BLOCK,PVOID,ULONG,FILE_INFORMATION_CLASS,ULONG,PUNICODE_STRING);
typedef BOOL (WINAPI *PROCESS_FN)(HANDLE,LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION,PHANDLE);
typedef BOOLEAN (NTAPI *MATCH_FN)(PUNICODE_STRING,PUNICODE_STRING,BOOLEAN,PWCH);
static CREATE_FN real_create;
static OPEN_FN real_open;
static ATTR_FN real_attr, real_fullattr;
static BYNAME_FN real_byname;
static SET_FN real_set;
static CLOSE_FN real_close;
static INFO_FN real_info;
static DUP_FN real_dup;
static FINAL_W_FN real_final_w;
static FINAL_A_FN real_final_a;
static QUERY_FN real_query;
static QUERY_EX_FN real_query_ex;
static PROCESS_FN real_process;
static MATCH_FN match_name;
static AGENTGLIDE_CONFIG cfg;
static wchar_t files[AGENTGLIDE_PATH], deleted[AGENTGLIDE_PATH], device[AGENTGLIDE_PATH];
static HANDLE mutex;
static CRITICAL_SECTION local_lock;
static DWORD bypass_index = TLS_OUT_OF_INDEXES;
/* NtClose can run while the loader's static TLS table is absent (observed
   with Claude Code/Bun). Win32 TLS slots do not dereference that table. */
static ULONG_PTR bypass_get(void) {
    DWORD error = GetLastError();
    ULONG_PTR depth = (ULONG_PTR)TlsGetValue(bypass_index);
    SetLastError(error); return depth;
}
static void bypass_set(ULONG_PTR depth) {
    DWORD error = GetLastError();
    TlsSetValue(bypass_index, (LPVOID)depth);
    SetLastError(error);
}
static void bypass_enter(void) { bypass_set(bypass_get() + 1); }
static void bypass_leave(void) { bypass_set(bypass_get() - 1); }
static LONG initialized;
/* Tag only handles opened through Base. Direct Overlay and outside handles
   retain their physical identity. Names are queried live (rename/duplicate). */
typedef struct VIRTUAL_HANDLE { HANDLE h; struct VIRTUAL_HANDLE *next; } VIRTUAL_HANDLE;
static VIRTUAL_HANDLE *virtual_handles;
static BOOL tagged(HANDLE h) {
    for (VIRTUAL_HANDLE *p = virtual_handles; p; p = p->next) if (p->h == h) return TRUE;
    return FALSE;
}
static BOOL tag(HANDLE h) {
    if (tagged(h)) return TRUE;
    VIRTUAL_HANDLE *p = malloc(sizeof(*p)); if (!p) return FALSE;
    p->h = h; p->next = virtual_handles; virtual_handles = p; return TRUE;
}
static void untag(HANDLE h) {
    VIRTUAL_HANDLE **p = &virtual_handles;
    while (*p) {
        if ((*p)->h == h) { VIRTUAL_HANDLE *old = *p; *p = old->next; free(old); return; }
        p = &(*p)->next;
    }
}

static BOOL join(wchar_t *out, const wchar_t *a, const wchar_t *b) {
    return swprintf_s(out, AGENTGLIDE_PATH, L"%s%s%s", a, (*b ? L"\\" : L""), b) >= 0;
}
static BOOL under(const wchar_t *path, const wchar_t *root) {
    size_t n = wcslen(root);
    return !_wcsnicmp(path, root, n) && (!path[n] || path[n] == L'\\');
}
static void lock(void) { EnterCriticalSection(&local_lock); WaitForSingleObject(mutex, INFINITE); }
static void unlock(void) { ReleaseMutex(mutex); LeaveCriticalSection(&local_lock); }
static BOOL mkdirs(const wchar_t *path) {
    wchar_t temp[AGENTGLIDE_PATH]; wcscpy_s(temp, AGENTGLIDE_PATH, path);
    for (wchar_t *p = temp + 3; ; ++p) {
        wchar_t c = *p;
        if (c == L'\\' || !c) {
            *p = 0;
            DWORD a = GetFileAttributesW(temp);
            if (a == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectoryW(temp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return FALSE;
                a = GetFileAttributesW(temp);
            }
            if (!(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT)) return FALSE;
            *p = c; if (!c) break;
        }
    }
    return TRUE;
}
static BOOL parents(const wchar_t *path) {
    wchar_t temp[AGENTGLIDE_PATH]; wcscpy_s(temp, AGENTGLIDE_PATH, path);
    wchar_t *p = wcsrchr(temp, L'\\'); if (!p) return FALSE; *p = 0;
    return mkdirs(temp);
}
static BOOL no_reparse(const wchar_t *path) {
    wchar_t temp[AGENTGLIDE_PATH]; wcscpy_s(temp, AGENTGLIDE_PATH, path);
    for (wchar_t *p = temp + 3; ; ++p) {
        wchar_t c = *p;
        if (c == L'\\' || !c) {
            *p = 0; DWORD a = GetFileAttributesW(temp); *p = c;
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT)) return FALSE;
            if (!c) break;
        }
    }
    return TRUE;
}
static void log_event(const wchar_t *event, const wchar_t *path) {
    wchar_t name[AGENTGLIDE_PATH], line[AGENTGLIDE_PATH + 128];
    swprintf_s(name, AGENTGLIDE_PATH, cfg.magic == AGENTGLIDE_MAGIC_LAYOUT1 ? L"%s\\.vfs\\logs\\trace-%lu.log" : L"%s\\trace-%lu.log", cfg.instance, GetCurrentProcessId());
    HANDLE h = CreateFileW(name, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        int n = swprintf_s(line, AGENTGLIDE_PATH + 128, L"pid=%lu %s %s\r\n", GetCurrentProcessId(), event, path ? path : L"");
        DWORD written; if (n > 0) WriteFile(h, line, (DWORD)(n * sizeof(wchar_t)), &written, NULL);
        CloseHandle(h);
    }
}
static BOOL handle_path(HANDLE h, wchar_t *out) {
    wchar_t raw[AGENTGLIDE_PATH];
    DWORD n = GetFinalPathNameByHandleW(h, raw, AGENTGLIDE_PATH, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n || n >= AGENTGLIDE_PATH) return FALSE;
    const wchar_t *p = !wcsncmp(raw, L"\\\\?\\", 4) ? raw + 4 : raw;
    return wcscpy_s(out, AGENTGLIDE_PATH, p) == 0;
}
/* Caller holds local_lock and bypass, so engine internals still see physical
   names. A virtual handle can point to either layer, including a new file. */
static BOOL virtual_handle_path(HANDLE h, wchar_t *out) {
    wchar_t physical[AGENTGLIDE_PATH];
    if (!tagged(h) || !handle_path(h, physical)) return FALSE;
    if (under(physical, files))
        return swprintf_s(out, AGENTGLIDE_PATH, L"%s%s", cfg.base, physical + wcslen(files)) >= 0;
    if (under(physical, cfg.base)) return wcscpy_s(out, AGENTGLIDE_PATH, physical) == 0;
    return FALSE;
}
static BOOL virtual_final(HANDLE h, DWORD flags, wchar_t *out) {
    wchar_t path[AGENTGLIDE_PATH], checked[AGENTGLIDE_PATH];
    /* Preserve Windows' validation of flags and handle type. */
    DWORD n = real_final_w(h, checked, AGENTGLIDE_PATH, flags);
    if (!n || n >= AGENTGLIDE_PATH || !virtual_handle_path(h, path)) return FALSE;
    switch (flags & ~FILE_NAME_OPENED) {
    case VOLUME_NAME_DOS: return swprintf_s(out,AGENTGLIDE_PATH,L"\\\\?\\%s",path) >= 0;
    case VOLUME_NAME_NONE: return wcscpy_s(out,AGENTGLIDE_PATH,path + 2) == 0;
    case VOLUME_NAME_NT: return swprintf_s(out,AGENTGLIDE_PATH,L"%s%s",device,path + 2) >= 0;
    case VOLUME_NAME_GUID: {
        wchar_t drive[4] = {cfg.base[0],L':',L'\\',0}, volume[64];
        if (!GetVolumeNameForVolumeMountPointW(drive,volume,64)) return FALSE;
        return swprintf_s(out,AGENTGLIDE_PATH,L"%s%s",volume,path + 3) >= 0;
    }
    default: return FALSE;
    }
}
static DWORD WINAPI hook_final_w(HANDLE h, LPWSTR buffer, DWORD cap, DWORD flags) {
    if (bypass_get()) return real_final_w(h,buffer,cap,flags);
    DWORD saved = GetLastError(); wchar_t name[AGENTGLIDE_PATH];
    bypass_enter(); EnterCriticalSection(&local_lock);
    BOOL mapped = virtual_final(h,flags,name);
    LeaveCriticalSection(&local_lock); bypass_leave();
    SetLastError(saved);
    if (!mapped) return real_final_w(h,buffer,cap,flags);
    DWORD n = (DWORD)wcslen(name);
    if (cap <= n) return n + 1; /* required size includes the terminating NUL */
    if (!buffer) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    memcpy(buffer,name,(n + 1)*sizeof(wchar_t)); return n;
}
static DWORD WINAPI hook_final_a(HANDLE h, LPSTR buffer, DWORD cap, DWORD flags) {
    if (bypass_get()) return real_final_a(h,buffer,cap,flags);
    DWORD saved = GetLastError(); wchar_t name[AGENTGLIDE_PATH];
    bypass_enter(); EnterCriticalSection(&local_lock);
    BOOL mapped = virtual_final(h,flags,name);
    LeaveCriticalSection(&local_lock); bypass_leave();
    SetLastError(saved);
    if (!mapped) return real_final_a(h,buffer,cap,flags);
    UINT cp = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    int bytes = WideCharToMultiByte(cp,0,name,-1,NULL,0,NULL,NULL);
    if (!bytes) return 0;
    /* Windows A variant reports the encoded byte count excluding NUL even
       for a length-only/short-buffer call (unlike the W variant). */
    if (cap < (DWORD)bytes) return (DWORD)bytes - 1;
    if (!buffer) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
    if (!WideCharToMultiByte(cp,0,name,-1,buffer,bytes,NULL,NULL)) return 0;
    SetLastError(saved); return (DWORD)bytes - 1;
}
static NTSTATUS NTAPI hook_info(HANDLE h, PIO_STATUS_BLOCK io, PVOID buffer, ULONG len, FILE_INFORMATION_CLASS type) {
    if (bypass_get() || ((int)type != 9 && (int)type != 48) || len < 8 || !buffer || !io)
        return real_info(h,io,buffer,len,type);
    wchar_t path[AGENTGLIDE_PATH]; BYTE check[sizeof(ULONG) + AGENTGLIDE_PATH*sizeof(wchar_t)]; IO_STATUS_BLOCK tempio;
    bypass_enter(); EnterCriticalSection(&local_lock);
    BOOL mapped = virtual_handle_path(h,path);
    NTSTATUS status = mapped ? real_info(h,&tempio,check,sizeof(check),type) : S_UNSUPPORTED;
    LeaveCriticalSection(&local_lock); bypass_leave();
    if (!mapped || !SUCCESS(status)) return real_info(h,io,buffer,len,type);
    ULONG bytes = (ULONG)(wcslen(path + 2)*sizeof(wchar_t));
    ULONG copy = len - sizeof(ULONG); if (copy > bytes) copy = bytes;
    copy &= ~1UL;
    *(ULONG *)buffer = bytes; memcpy((BYTE *)buffer + sizeof(ULONG),path + 2,copy);
    io->Information = sizeof(ULONG) + copy; io->Status = copy == bytes ? S_OK : S_OVERFLOW;
    return io->Status;
}
static NTSTATUS NTAPI hook_dup(HANDLE source, HANDLE h, HANDLE target, PHANDLE copy,
    ACCESS_MASK access, ULONG attrs, ULONG options) {
    if (bypass_get()) return real_dup(source,h,target,copy,access,attrs,options);
    bypass_enter(); EnterCriticalSection(&local_lock);
    BOOL local_source = GetProcessId(source) == GetCurrentProcessId();
    BOOL virtual_source = local_source && tagged(h);
    NTSTATUS status = real_dup(source,h,target,copy,access,attrs,options);
    if (local_source && (options & DUPLICATE_CLOSE_SOURCE)) untag(h);
    if (SUCCESS(status) && virtual_source && copy && GetProcessId(target) == GetCurrentProcessId() && !tag(*copy)) {
        real_close(*copy); *copy = NULL; status = S_MEMORY;
    }
    LeaveCriticalSection(&local_lock); bypass_leave(); return status;
}
/* Return 1 inside the virtual root, 0 outside, -1 rejected. */
static int relative_path(const wchar_t *raw, wchar_t *rel) {
    wchar_t full[AGENTGLIDE_PATH], input[AGENTGLIDE_PATH];
    const wchar_t *p = raw;
    if (!wcsncmp(p, L"\\??\\", 4) || !wcsncmp(p, L"\\\\?\\", 4)) p += 4;
    if (*device && !_wcsnicmp(p, device, wcslen(device)) && p[wcslen(device)] == L'\\') {
        if (swprintf_s(input, AGENTGLIDE_PATH, L"%c:%s", cfg.base[0], p + wcslen(device)) < 0) return -1;
        p = input;
    }
    if (!p[0] || p[1] != L':') return 0;
    /* An NT volume handle (\\??\\C:) is not Win32's drive-relative C: cwd. */
    if (!p[2]) return 0;
    DWORD n = GetFullPathNameW(p, AGENTGLIDE_PATH, full, NULL);
    if (!n || n >= AGENTGLIDE_PATH) return -1;
    while (n > 3 && full[n - 1] == L'\\') full[--n] = 0;
    if (!under(full, cfg.base)) return 0;
    p = full + wcslen(cfg.base); if (*p == L'\\') ++p;
    if (wcschr(p, L':')) return -1; /* streams intentionally unsupported */
    /* Reject Win32 aliases (trailing dots/spaces) instead of mapping inconsistently. */
    for (const wchar_t *q = p; *q; ++q)
        if ((*q == L'.' || *q == L' ') && (!q[1] || q[1] == L'\\')) return -1;
    wcscpy_s(rel, AGENTGLIDE_PATH, p); return 1;
}
static int object_path(POBJECT_ATTRIBUTES oa, wchar_t *rel) {
    wchar_t raw[AGENTGLIDE_PATH], root[AGENTGLIDE_PATH], combined[AGENTGLIDE_PATH];
    if (!oa || !oa->ObjectName || !oa->ObjectName->Buffer) return 0;
    size_t n = oa->ObjectName->Length / sizeof(wchar_t);
    if (n >= AGENTGLIDE_PATH) return -1;
    memcpy(raw, oa->ObjectName->Buffer, n * sizeof(wchar_t)); raw[n] = 0;
    if (oa->RootDirectory && *raw != L'\\') {
        /* MSYS/Git Bash creates pipes relative to a named-pipe namespace
           handle, which cannot be resolved as a disk filesystem path. */
        if (GetFileType(oa->RootDirectory) == FILE_TYPE_PIPE) return 0;
        if (!handle_path(oa->RootDirectory, root)) return -1;
        if (under(root, files)) {
            wchar_t virtual_root[AGENTGLIDE_PATH];
            swprintf_s(virtual_root, AGENTGLIDE_PATH, L"%s%s", cfg.base, root + wcslen(files));
            wcscpy_s(root, AGENTGLIDE_PATH, virtual_root);
        }
        if (!join(combined, root, raw)) return -1;
        return relative_path(combined, rel);
    }
    return relative_path(raw, rel);
}
static BOOL marker_path(const wchar_t *rel, wchar_t *out) {
    wchar_t temp[AGENTGLIDE_PATH]; return join(temp, deleted, rel) && join(out, temp, L".deleted");
}
static BOOL hidden(const wchar_t *rel) {
    wchar_t part[AGENTGLIDE_PATH], mark[AGENTGLIDE_PATH]; wcscpy_s(part, AGENTGLIDE_PATH, rel);
    for (;;) {
        if (!marker_path(part, mark)) return TRUE;
        if (GetFileAttributesW(mark) != INVALID_FILE_ATTRIBUTES) return TRUE;
        wchar_t *p = wcsrchr(part, L'\\'); if (!p) break; *p = 0;
    }
    return FALSE;
}
static BOOL tombstone(const wchar_t *rel, BOOL value) {
    wchar_t mark[AGENTGLIDE_PATH]; if (!marker_path(rel, mark)) return FALSE;
    if (!value) return DeleteFileW(mark) || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    if (!parents(mark)) return FALSE;
    HANDLE h = CreateFileW(mark, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(h); return TRUE;
}
static DWORD visible(const wchar_t *rel, wchar_t *physical) {
    if (!join(physical, cfg.base, rel)) return INVALID_FILE_ATTRIBUTES;
    if (hidden(rel)) return INVALID_FILE_ATTRIBUTES;
    if (!join(physical, files, rel)) return INVALID_FILE_ATTRIBUTES;
    DWORD a = GetFileAttributesW(physical);
    if (a != INVALID_FILE_ATTRIBUTES) return a;
    if (!join(physical, cfg.base, rel)) return INVALID_FILE_ATTRIBUTES;
    return GetFileAttributesW(physical);
}
static BOOL valid_parent(const wchar_t *rel) {
    wchar_t parent[AGENTGLIDE_PATH], physical[AGENTGLIDE_PATH]; wcscpy_s(parent, AGENTGLIDE_PATH, rel);
    wchar_t *p = wcsrchr(parent, L'\\'); if (p) *p = 0; else *parent = 0;
    DWORD a = visible(parent, physical);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static NTSTATUS materialize(const wchar_t *rel, const wchar_t *source, DWORD attrs, wchar_t *target) {
    if (!join(target, files, rel) || !parents(target) || !no_reparse(source) || !no_reparse(target)) return S_DENIED;
    if (GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES) return S_OK;
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
            if (!CreateDirectoryW(target, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) return S_DENIED;
        } else {
            if (!CopyFileW(source, target, TRUE)) return S_DENIED;
            log_event(L"copy-up", rel);
        }
    }
    return S_OK;
}
static void mapped_oa(POBJECT_ATTRIBUTES original, OBJECT_ATTRIBUTES *oa, UNICODE_STRING *name,
    wchar_t *nt, const wchar_t *physical) {
    swprintf_s(nt, AGENTGLIDE_PATH, L"\\??\\%s", physical);
    name->Buffer = nt; name->Length = (USHORT)(wcslen(nt) * 2); name->MaximumLength = name->Length + 2;
    *oa = *original; oa->RootDirectory = NULL; oa->ObjectName = name;
}
static NTSTATUS finish(PIO_STATUS_BLOCK io, NTSTATUS status) {
    if (io) { io->Status = status; io->Information = 0; } return status;
}

static NTSTATUS NTAPI hook_create(PHANDLE h, ACCESS_MASK access, POBJECT_ATTRIBUTES oa,
    PIO_STATUS_BLOCK io, PLARGE_INTEGER size, ULONG attr, ULONG share, ULONG disposition,
    ULONG options, PVOID ea, ULONG ealen) {
    if (bypass_get()) return real_create(h, access, oa, io, size, attr, share, disposition, options, ea, ealen);
    wchar_t rel[AGENTGLIDE_PATH], physical[AGENTGLIDE_PATH], target[AGENTGLIDE_PATH], nt[AGENTGLIDE_PATH];
    OBJECT_ATTRIBUTES mapped; UNICODE_STRING name;
    bypass_enter(); int kind = object_path(oa, rel);
    if (!kind) { bypass_leave(); return real_create(h, access, oa, io, size, attr, share, disposition, options, ea, ealen); }
    if (kind < 0) { bypass_leave(); return finish(io, S_DENIED); }
    lock();
    BOOL write = (access & (GENERIC_WRITE | GENERIC_ALL | MAXIMUM_ALLOWED | DELETE | WRITE_DAC | WRITE_OWNER |
        FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES)) ||
        disposition != 1 || (options & DELETE_ON_CLOSE);
    DWORD attrs = visible(rel, physical);
    NTSTATUS status = S_OK;
    if (!join(target, files, rel) || !no_reparse(target) ||
        !join(nt, cfg.base, rel) || !no_reparse(nt)) status = S_DENIED;
    else if (attrs != INVALID_FILE_ATTRIBUTES && disposition == 2) status = S_COLLISION;
    else if (!*rel && write) status = S_DENIED;
    else if ((options & DELETE_ON_CLOSE) && ((options & DIRECTORY_FILE) || (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)))) status = S_UNSUPPORTED;
    else if (attrs == INVALID_FILE_ATTRIBUTES && (disposition == 1 || disposition == 4)) status = S_NOT_FOUND;
    else if (write && !valid_parent(rel)) status = S_PATH_NOT_FOUND;
    else if (write) status = materialize(rel, physical, attrs, target);
    if (SUCCESS(status)) {
        if (write) wcscpy_s(physical, AGENTGLIDE_PATH, target);
        mapped_oa(oa, &mapped, &name, nt, physical);
        status = real_create(h, access, &mapped, io, size, attr, share, disposition, options, ea, ealen);
        if (SUCCESS(status) && write) {
            if (!tombstone(rel, !!(options & DELETE_ON_CLOSE))) {
                real_close(*h); *h = NULL; status = S_DENIED;
            }
            log_event((options & DELETE_ON_CLOSE) ? L"delete-on-close" : L"write-open", rel);
        }
        if (SUCCESS(status) && !tag(*h)) {
            real_close(*h); *h = NULL; status = finish(io,S_MEMORY);
        }
    } else finish(io, status);
    unlock(); bypass_leave(); return status;
}
static NTSTATUS NTAPI hook_open(PHANDLE h, ACCESS_MASK access, POBJECT_ATTRIBUTES oa,
    PIO_STATUS_BLOCK io, ULONG share, ULONG options) {
    if (bypass_get()) return real_open(h, access, oa, io, share, options);
    return hook_create(h, access, oa, io, NULL, 0, share, 1, options, NULL, 0);
}
static NTSTATUS attributes(POBJECT_ATTRIBUTES oa, PVOID info, ATTR_FN fn) {
    if (bypass_get()) return fn(oa, info);
    wchar_t rel[AGENTGLIDE_PATH], physical[AGENTGLIDE_PATH], nt[AGENTGLIDE_PATH];
    OBJECT_ATTRIBUTES mapped; UNICODE_STRING name;
    bypass_enter(); int kind = object_path(oa, rel);
    if (!kind) { bypass_leave(); return fn(oa, info); }
    if (kind < 0) { bypass_leave(); return S_DENIED; }
    lock(); NTSTATUS status;
    if (visible(rel, physical) == INVALID_FILE_ATTRIBUTES) status = S_NOT_FOUND;
    else if (!no_reparse(physical)) status = S_DENIED;
    else { mapped_oa(oa, &mapped, &name, nt, physical); status = fn(&mapped, info); }
    unlock(); bypass_leave(); return status;
}
static NTSTATUS NTAPI hook_attr(POBJECT_ATTRIBUTES oa, PVOID info) { return attributes(oa, info, real_attr); }
static NTSTATUS NTAPI hook_fullattr(POBJECT_ATTRIBUTES oa, PVOID info) { return attributes(oa, info, real_fullattr); }
static NTSTATUS NTAPI hook_byname(POBJECT_ATTRIBUTES oa, PIO_STATUS_BLOCK io, PVOID info, ULONG len, FILE_INFORMATION_CLASS type) {
    if (bypass_get()) return real_byname(oa, io, info, len, type);
    wchar_t rel[AGENTGLIDE_PATH], physical[AGENTGLIDE_PATH], nt[AGENTGLIDE_PATH];
    OBJECT_ATTRIBUTES mapped; UNICODE_STRING name;
    bypass_enter(); int kind = object_path(oa, rel);
    if (!kind) { bypass_leave(); return real_byname(oa, io, info, len, type); }
    if (kind < 0) { bypass_leave(); return finish(io, S_DENIED); }
    lock(); NTSTATUS status;
    if (visible(rel, physical) == INVALID_FILE_ATTRIBUTES) status = finish(io, S_NOT_FOUND);
    else if (!no_reparse(physical)) status = finish(io, S_DENIED);
    else {
        mapped_oa(oa, &mapped, &name, nt, physical);
        status = real_byname(&mapped, io, info, len, type);
    }
    unlock(); bypass_leave(); return status;
}

typedef struct RENAME_INFO { ULONG Flags; HANDLE RootDirectory; ULONG FileNameLength; WCHAR FileName[1]; } RENAME_INFO;
static NTSTATUS NTAPI hook_set(HANDLE h, PIO_STATUS_BLOCK io, PVOID data, ULONG len, FILE_INFORMATION_CLASS type) {
    if (bypass_get()) return real_set(h, io, data, len, type);
    if ((int)type == 14 || (int)type == 16 || (int)type == 43) return real_set(h, io, data, len, type);
    wchar_t physical[AGENTGLIDE_PATH], rel[AGENTGLIDE_PATH];
    bypass_enter();
    if (!handle_path(h, physical)) { bypass_leave(); return real_set(h, io, data, len, type); }
    BOOL overlay = under(physical, files);
    int kind = overlay ? 1 : relative_path(physical, rel);
    if (overlay) {
        const wchar_t *p = physical + wcslen(files); if (*p == L'\\') p++;
        wcscpy_s(rel, AGENTGLIDE_PATH, p);
    }
    if (!kind) {
        /* An outside source can still target the protected repository. Reject
           cross-root moves and links instead of letting them bypass CoW. */
        if ((int)type == 10 || (int)type == 65 || (int)type == 11 || (int)type == 72) {
            RENAME_INFO *r = (RENAME_INFO *)data;
            if (len < offsetof(RENAME_INFO, FileName) || r->FileNameLength > len - offsetof(RENAME_INFO, FileName) || r->FileNameLength > 4094) {
                bypass_leave(); return finish(io, S_INVALID);
            }
            UNICODE_STRING name = { (USHORT)r->FileNameLength, (USHORT)r->FileNameLength, r->FileName };
            OBJECT_ATTRIBUTES target = { sizeof(target), r->RootDirectory, &name, OBJ_CASE_INSENSITIVE, NULL, NULL };
            if (object_path(&target, rel) != 0) { bypass_leave(); return finish(io, S_DENIED); }
        }
        bypass_leave(); return real_set(h, io, data, len, type);
    }
    if (kind < 0 || !overlay || !*rel) { bypass_leave(); return finish(io, S_DENIED); }
    lock(); NTSTATUS status = S_UNSUPPORTED;
    if ((int)type == 13 || (int)type == 64) { /* FileDispositionInformation[Ex] */
        BOOL remove = len && (*(BYTE *)data & 1);
        DWORD a = GetFileAttributesW(physical);
        /* Nonempty merged directories must not be deleted through an empty shadow. */
        if (a & FILE_ATTRIBUTE_DIRECTORY) status = S_UNSUPPORTED;
        else if (!remove || tombstone(rel, TRUE)) {
            status = real_set(h, io, data, len, type);
            if (!SUCCESS(status) || !remove) tombstone(rel, FALSE);
            if (SUCCESS(status)) log_event(L"delete", rel);
        } else status = S_DENIED;
    } else if ((int)type == 10 || (int)type == 65) { /* FileRenameInformation[Ex] */
        RENAME_INFO *r = (RENAME_INFO *)data;
        wchar_t destrel[AGENTGLIDE_PATH], target[AGENTGLIDE_PATH], nt[AGENTGLIDE_PATH], existing[AGENTGLIDE_PATH];
        if (len < offsetof(RENAME_INFO, FileName) || r->FileNameLength > len - offsetof(RENAME_INFO, FileName) ||
            (r->FileNameLength & 1) || r->FileNameLength > 4094) status = S_INVALID;
        else if (GetFileAttributesW(physical) & FILE_ATTRIBUTE_DIRECTORY) status = S_UNSUPPORTED;
        else {
            UNICODE_STRING name = { (USHORT)r->FileNameLength, (USHORT)r->FileNameLength, r->FileName };
            OBJECT_ATTRIBUTES oa = { sizeof(oa), r->RootDirectory, &name, OBJ_CASE_INSENSITIVE, NULL, NULL };
            int target_kind = object_path(&oa, destrel);
            if (target_kind != 1 || !*destrel || !valid_parent(destrel)) status = S_DENIED;
            else if (!(r->Flags & 1) && visible(destrel, existing) != INVALID_FILE_ATTRIBUTES) status = S_COLLISION;
            else if (!join(target, files, destrel) || !no_reparse(target) || !parents(target) || !tombstone(rel, TRUE)) status = S_DENIED;
            else {
                swprintf_s(nt, AGENTGLIDE_PATH, L"\\??\\%s", target);
                ULONG bytes = (ULONG)(wcslen(nt) * 2), total = (ULONG)offsetof(RENAME_INFO, FileName) + bytes;
                RENAME_INFO *mapped = (RENAME_INFO *)calloc(1, total);
                if (!mapped) status = S_MEMORY;
                else {
                    mapped->Flags = r->Flags; mapped->FileNameLength = bytes;
                    memcpy(mapped->FileName, nt, bytes);
                    status = real_set(h, io, mapped, total, type); free(mapped);
                }
                if (SUCCESS(status)) { tombstone(destrel, FALSE); log_event(L"rename-from", rel); log_event(L"rename-to", destrel); }
                else tombstone(rel, FALSE);
            }
        }
    } else if ((int)type == 4 || (int)type == 14 || (int)type == 19 || (int)type == 20 ||
        (int)type == 39 || (int)type == 43) {
        /* Basic, position, allocation, EOF, valid-data-length, I/O priority. */
        status = real_set(h, io, data, len, type);
    }
    if (!SUCCESS(status)) finish(io, status);
    unlock(); bypass_leave(); return status;
}

typedef struct ENTRY { WIN32_FIND_DATAW data; } ENTRY;
typedef struct ENUM_STATE {
    HANDLE handle;
    ENTRY *entries;
    size_t count, capacity, cursor;
    wchar_t pattern[260];
    struct ENUM_STATE *next;
} ENUM_STATE;
static ENUM_STATE *enums;
static BOOL append_entry(ENUM_STATE *state, const WIN32_FIND_DATAW *entry) {
    for (size_t i = 0; i < state->count; ++i)
        if (!_wcsicmp(state->entries[i].data.cFileName, entry->cFileName)) return TRUE;
    if (state->count == state->capacity) {
        size_t cap = state->capacity ? state->capacity * 2 : 32;
        ENTRY *p = (ENTRY *)realloc(state->entries, cap * sizeof(*p));
        if (!p) return FALSE; state->entries = p; state->capacity = cap;
    }
    state->entries[state->count++].data = *entry; return TRUE;
}
static BOOL collect(ENUM_STATE *state, const wchar_t *dirrel, const wchar_t *root) {
    wchar_t path[AGENTGLIDE_PATH], search[AGENTGLIDE_PATH], child[AGENTGLIDE_PATH]; WIN32_FIND_DATAW data;
    if (!join(path, root, dirrel) || !join(search, path, L"*")) return FALSE;
    HANDLE find = FindFirstFileW(search, &data);
    if (find == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    BOOL ok = TRUE;
    do {
        if (!wcscmp(data.cFileName, L".") || !wcscmp(data.cFileName, L"..")) continue;
        if (*dirrel) { if (!join(child, dirrel, data.cFileName)) { ok = FALSE; break; } }
        else wcscpy_s(child, AGENTGLIDE_PATH, data.cFileName);
        if (hidden(child)) continue;
        UNICODE_STRING pattern, name;
        pattern.Buffer = state->pattern; pattern.Length = (USHORT)(wcslen(state->pattern) * 2); pattern.MaximumLength = pattern.Length;
        name.Buffer = data.cFileName; name.Length = (USHORT)(wcslen(data.cFileName) * 2); name.MaximumLength = name.Length;
        if (match_name(&pattern, &name, TRUE, NULL) && !append_entry(state, &data)) { ok = FALSE; break; }
    } while (FindNextFileW(find, &data));
    FindClose(find); return ok;
}
static NTSTATUS enumerate(HANDLE h, HANDLE event, PIO_APC_ROUTINE apc, PIO_STATUS_BLOCK io,
    void *buffer, ULONG length, FILE_INFORMATION_CLASS type, BOOL single, PUNICODE_STRING pattern, BOOL restart) {
    wchar_t physical[AGENTGLIDE_PATH], rel[AGENTGLIDE_PATH];
    if (!handle_path(h, physical)) return (NTSTATUS)0x12345678;
    if (under(physical, files)) {
        const wchar_t *p = physical + wcslen(files); if (*p == L'\\') p++;
        wcscpy_s(rel, AGENTGLIDE_PATH, p);
    } else if (relative_path(physical, rel) != 1) return (NTSTATUS)0x12345678;
    if (event || apc) return finish(io, S_UNSUPPORTED);
    ULONG offset;
    switch ((int)type) {
        case 1: offset = 64; break;
        case 2: offset = 68; break;
        case 3: offset = 94; break;
        case 12: offset = 12; break;
        case 37: offset = 104; break;
        case 38: offset = 80; break;
        default: return finish(io, S_UNSUPPORTED);
    }
    if (length < offset) return finish(io, S_SMALL);
    ENUM_STATE *state;
    for (state = enums; state && state->handle != h; state = state->next) {}
    if (!state) {
        state = (ENUM_STATE *)calloc(1, sizeof(*state)); if (!state) return finish(io, S_MEMORY);
        state->handle = h; wcscpy_s(state->pattern, 260, L"*");
        if (pattern && pattern->Length) {
            if (pattern->Length >= sizeof(state->pattern)) { free(state); return finish(io, S_INVALID); }
            memcpy(state->pattern, pattern->Buffer, pattern->Length);
            state->pattern[pattern->Length / 2] = 0;
            for (wchar_t *p = state->pattern; *p; ++p) *p = towupper(*p);
        }
        state->next = enums; enums = state; restart = TRUE;
    }
    if (restart) {
        state->count = state->cursor = 0;
        if (!collect(state, rel, files) || !collect(state, rel, cfg.base)) return finish(io, S_DENIED);
    }
    if (state->cursor >= state->count) return finish(io, S_NO_MORE);
    ULONG used = 0, previous = 0; BOOL any = FALSE;
    while (state->cursor < state->count) {
        WIN32_FIND_DATAW *d = &state->entries[state->cursor].data;
        ULONG namebytes = (ULONG)(wcslen(d->cFileName) * 2), record = offset + namebytes;
        ULONG start = (used + 7) & ~7U;
        if (start + record > length && any) break;
        BYTE *out = (BYTE *)buffer + start;
        ULONG available = length - start;
        memset(out, 0, available < record ? available : record);
        *(ULONG *)(out + 4) = (ULONG)state->cursor;
        if ((int)type == 12) *(ULONG *)(out + 8) = namebytes;
        else {
            memcpy(out + 8, &d->ftCreationTime, 8);
            memcpy(out + 16, &d->ftLastAccessTime, 8);
            memcpy(out + 24, &d->ftLastWriteTime, 8);
            memcpy(out + 32, &d->ftLastWriteTime, 8);
            ULONGLONG size = ((ULONGLONG)d->nFileSizeHigh << 32) | d->nFileSizeLow;
            *(ULONGLONG *)(out + 40) = size; *(ULONGLONG *)(out + 48) = (size + 4095) & ~4095ULL;
            *(ULONG *)(out + 56) = d->dwFileAttributes; *(ULONG *)(out + 60) = namebytes;
            if ((int)type == 37) *(ULONGLONG *)(out + 96) = state->cursor + 1;
            if ((int)type == 38) *(ULONGLONG *)(out + 72) = state->cursor + 1;
        }
        ULONG copy = available - offset; if (copy > namebytes) copy = namebytes;
        memcpy(out + offset, d->cFileName, copy);
        if (any) *(ULONG *)((BYTE *)buffer + previous) = start - previous;
        previous = start; any = TRUE; state->cursor++;
        used = start + offset + copy;
        if (copy != namebytes) { io->Status = S_OVERFLOW; io->Information = used; return S_OVERFLOW; }
        if (single) break;
    }
    io->Status = S_OK; io->Information = used; return S_OK;
}
static NTSTATUS NTAPI hook_query(HANDLE h,HANDLE event,PIO_APC_ROUTINE apc,PVOID context,PIO_STATUS_BLOCK io,
    PVOID buffer,ULONG len,FILE_INFORMATION_CLASS type,BOOLEAN single,PUNICODE_STRING pattern,BOOLEAN restart) {
    if (bypass_get()) return real_query(h,event,apc,context,io,buffer,len,type,single,pattern,restart);
    bypass_enter(); lock(); NTSTATUS status = enumerate(h,event,apc,io,buffer,len,type,single,pattern,restart); unlock(); bypass_leave();
    if (status == (NTSTATUS)0x12345678) return real_query(h,event,apc,context,io,buffer,len,type,single,pattern,restart);
    return status;
}
static NTSTATUS NTAPI hook_query_ex(HANDLE h,HANDLE event,PIO_APC_ROUTINE apc,PVOID context,PIO_STATUS_BLOCK io,
    PVOID buffer,ULONG len,FILE_INFORMATION_CLASS type,ULONG flags,PUNICODE_STRING pattern) {
    if (bypass_get()) return real_query_ex(h,event,apc,context,io,buffer,len,type,flags,pattern);
    bypass_enter(); lock(); NTSTATUS status = enumerate(h,event,apc,io,buffer,len,type,!!(flags & 2),pattern,!!(flags & 1)); unlock(); bypass_leave();
    if (status == (NTSTATUS)0x12345678) return real_query_ex(h,event,apc,context,io,buffer,len,type,flags,pattern);
    return status;
}
static NTSTATUS NTAPI hook_close(HANDLE h) {
    if (bypass_get()) return real_close(h);
    bypass_enter(); EnterCriticalSection(&local_lock);
    ENUM_STATE **p = &enums;
    while (*p) {
        if ((*p)->handle == h) { ENUM_STATE *old = *p; *p = old->next; free(old->entries); free(old); break; }
        p = &(*p)->next;
    }
    NTSTATUS status = real_close(h);
    if (SUCCESS(status)) untag(h);
    LeaveCriticalSection(&local_lock); bypass_leave(); return status;
}
static BOOL WINAPI hook_process(HANDLE token,LPCWSTR app,LPWSTR command,LPSECURITY_ATTRIBUTES pa,
    LPSECURITY_ATTRIBUTES ta,BOOL inherit,DWORD flags,LPVOID env,LPCWSTR cwd,LPSTARTUPINFOW si,
    LPPROCESS_INFORMATION pi,PHANDLE newtoken) {
    /* Do not keep bypass set while CreateProcess loads the executable: it must see the overlay. */
    BOOL ok = real_process(token,app,command,pa,ta,inherit,flags | CREATE_SUSPENDED,env,cwd,si,pi,newtoken);
    if (!ok) return FALSE;
    bypass_enter();
    ok = agentglide_inject(pi->hProcess, &cfg);
    if (ok) {
        wchar_t child[64]; swprintf_s(child, 64, L"child=%lu", pi->dwProcessId); log_event(L"propagate", child);
        if (!(flags & CREATE_SUSPENDED)) ResumeThread(pi->hThread);
    } else {
        log_event(L"propagate-failed", app);
        TerminateProcess(pi->hProcess, 125); CloseHandle(pi->hThread); CloseHandle(pi->hProcess);
        ZeroMemory(pi, sizeof(*pi));
    }
    bypass_leave(); if (!ok) SetLastError(ERROR_DLL_INIT_FAILED); return ok;
}

static BOOL install(HMODULE mod, const char *name, void *hook, void **original) {
    void *p = (void *)GetProcAddress(mod, name);
    return p && MH_CreateHook(p, hook, original) == MH_OK;
}
__declspec(dllexport) DWORD WINAPI UsvfsInitialize(void *argument) {
    if (InterlockedCompareExchange(&initialized, 1, 0)) return 0;
    AGENTGLIDE_CONFIG *input = (AGENTGLIDE_CONFIG *)argument;
    if (!input || (input->magic != AGENTGLIDE_MAGIC && input->magic != AGENTGLIDE_MAGIC_LAYOUT1)) return 0;
    cfg = *input; cfg.base[AGENTGLIDE_PATH-1] = cfg.instance[AGENTGLIDE_PATH-1] = cfg.dll[AGENTGLIDE_PATH-1] = 0;
    bypass_index = TlsAlloc();
    if (bypass_index == TLS_OUT_OF_INDEXES) return 0;
    bypass_enter();
    if (!join(files, cfg.instance, L"files") || !join(deleted, cfg.instance, cfg.magic == AGENTGLIDE_MAGIC_LAYOUT1 ? L".vfs\\deleted" : L"deleted") ||
        !no_reparse(cfg.base) || !mkdirs(files) || !mkdirs(deleted)) return 0;
    wchar_t drive[3] = { cfg.base[0], L':', 0 };
    QueryDosDeviceW(drive, device, AGENTGLIDE_PATH);
    ULONGLONG hash = 1469598103934665603ULL;
    wchar_t lower[AGENTGLIDE_PATH]; wcscpy_s(lower, AGENTGLIDE_PATH, cfg.instance);
    for (wchar_t *p = lower; *p; ++p) *p = towlower(*p);
    for (const wchar_t *p = lower; *p; ++p) { hash ^= (unsigned short)*p; hash *= 1099511628211ULL; }
    wchar_t mutex_name[100]; swprintf_s(mutex_name, 100, L"Local\\usvfs-c-%016llx", hash);
    mutex = CreateMutexW(NULL, FALSE, mutex_name); if (!mutex) return 0;
    InitializeCriticalSection(&local_lock);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll"), kb = GetModuleHandleW(L"kernelbase.dll");
    match_name = (MATCH_FN)GetProcAddress(nt, "RtlIsNameInExpression");
    if (!match_name || MH_Initialize() != MH_OK) return 0;
#define HOOK(module, name, hook, real) if (!install(module, name, (void *)hook, (void **)&real)) goto fail
    HOOK(nt,"NtCreateFile",hook_create,real_create);
    HOOK(nt,"NtOpenFile",hook_open,real_open);
    HOOK(nt,"NtQueryAttributesFile",hook_attr,real_attr);
    HOOK(nt,"NtQueryFullAttributesFile",hook_fullattr,real_fullattr);
    if (GetProcAddress(nt, "NtQueryInformationByName")) {
        HOOK(nt,"NtQueryInformationByName",hook_byname,real_byname);
    }
    HOOK(nt,"NtSetInformationFile",hook_set,real_set);
    HOOK(nt,"NtQueryDirectoryFile",hook_query,real_query);
    HOOK(nt,"NtQueryDirectoryFileEx",hook_query_ex,real_query_ex);
    HOOK(nt,"NtClose",hook_close,real_close);
    HOOK(nt,"NtQueryInformationFile",hook_info,real_info);
    HOOK(nt,"NtDuplicateObject",hook_dup,real_dup);
    HOOK(kb,"GetFinalPathNameByHandleW",hook_final_w,real_final_w);
    HOOK(kb,"GetFinalPathNameByHandleA",hook_final_a,real_final_a);
    HOOK(kb,"CreateProcessInternalW",hook_process,real_process);
#undef HOOK
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) goto fail;
    log_event(L"initialized", cfg.base); bypass_leave(); return AGENTGLIDE_MAGIC;
fail:
    MH_Uninitialize(); log_event(L"hook-install-failed", L""); bypass_leave(); return 0;
}
BOOL WINAPI DllMain(HINSTANCE dll, DWORD reason, LPVOID reserved) {
    (void)dll; (void)reason; (void)reserved;
    /* All work is done by the explicit remote initializer, outside loader lock. */
    return TRUE;
}
