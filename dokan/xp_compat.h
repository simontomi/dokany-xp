/*
  Dokan : user-mode file system library for Windows

  XP compatibility for dokan2.dll and dokanctl.exe: what the WDK 7.1 headers
  (Windows XP x86 and Windows Vista x64 targets) do not have, and replacements
  for the Windows API calls that Windows XP does not have.

  The XP build (xp\build.cmd) force-includes this header in every source file
  of the library and of dokanctl (/FI). The Visual Studio build never includes
  it.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation; either version 3 of the License, or (at your option) any
  later version.
*/

#ifndef DOKAN_XP_COMPAT_UM_H_
#define DOKAN_XP_COMPAT_UM_H_

#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
// The WDK build defines WIN32_LEAN_AND_MEAN, which leaves winioctl.h out of
// windows.h; the Windows 10 SDK build of the library gets it from windows.h.
#include <winioctl.h>

// The WDK raises C4242 (possible loss of data) to level 3 in its forced
// warning.h; the upstream build does not report it.
#pragma warning(disable : 4242)

// NTSTATUS: the Windows 10 SDK headers the library uses declare it; the
// WDK 7.1 ones only in winternl.h, which conflicts with fileinfo.h.
#ifndef _NTDEF_
typedef LONG NTSTATUS;
typedef NTSTATUS *PNTSTATUS;
#endif

// ---------------------------------------------------------------------------
// C language: the WDK 7.1 C compiler (cl 15.00) has no C99 'inline'.
// ---------------------------------------------------------------------------
#ifndef __cplusplus
#define inline __inline
#endif

// ---------------------------------------------------------------------------
// SAL 2 annotations: the WDK 7.1 only knows SAL 1 (__in, __out, ...).
// ---------------------------------------------------------------------------
#ifndef _In_
#define _In_
#endif
#ifndef _In_opt_
#define _In_opt_
#endif
#ifndef _Out_
#define _Out_
#endif
#ifndef _Out_opt_
#define _Out_opt_
#endif
#ifndef _Inout_
#define _Inout_
#endif
#ifndef _Inout_opt_
#define _Inout_opt_
#endif
#ifndef _Outptr_
#define _Outptr_
#endif
#ifndef _Ret_maybenull_
#define _Ret_maybenull_
#endif
#ifndef _When_
#define _When_(expr, annos)
#endif
#ifndef _Success_
#define _Success_(expr)
#endif
#ifndef _Must_inspect_result_
#define _Must_inspect_result_
#endif
#ifndef _Printf_format_string_
#define _Printf_format_string_
#endif
#ifndef _Use_decl_annotations_
#define _Use_decl_annotations_
#endif
#ifndef _Acquires_lock_
#define _Acquires_lock_(lock)
#endif
#ifndef _Releases_lock_
#define _Releases_lock_(lock)
#endif
#ifndef _Requires_lock_held_
#define _Requires_lock_held_(lock)
#endif
#ifndef _Analysis_assume_
#define _Analysis_assume_(expr)
#endif

// ---------------------------------------------------------------------------
// Constants and types the WDK 7.1 headers lack (values from the Windows 10
// SDK). An undefined name in #if counts as 0, so every _WIN32_WINNT_ name the
// code compares with must exist.
// ---------------------------------------------------------------------------
#ifndef _WIN32_WINNT_WIN8
#define _WIN32_WINNT_WIN8 0x0602
#endif
#ifndef _WIN32_WINNT_WINBLUE
#define _WIN32_WINNT_WINBLUE 0x0603
#endif
#ifndef _WIN32_WINNT_WIN10
#define _WIN32_WINNT_WIN10 0x0A00
#endif

#ifndef FILE_ID_128_DEFINED
#define FILE_ID_128_DEFINED
typedef struct _FILE_ID_128 {
  BYTE Identifier[16];
} FILE_ID_128, *PFILE_ID_128;
#endif

// FILE_ID_EXTD_DIR_INFO (Windows 8 winbase.h): the answer to
// FileIdExtdDirectoryInformation, which XP never asks for.
typedef struct _FILE_ID_EXTD_DIR_INFO {
  ULONG NextEntryOffset;
  ULONG FileIndex;
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  LARGE_INTEGER EndOfFile;
  LARGE_INTEGER AllocationSize;
  ULONG FileAttributes;
  ULONG FileNameLength;
  ULONG EaSize;
  ULONG ReparsePointTag;
  FILE_ID_128 FileId;
  WCHAR FileName[1];
} FILE_ID_EXTD_DIR_INFO, *PFILE_ID_EXTD_DIR_INFO;

// rpcnterr.h (left out by WIN32_LEAN_AND_MEAN) maps it to this error.
#ifndef RPC_X_INVALID_PIPE_OPERATION
#define RPC_X_INVALID_PIPE_OPERATION RPC_X_WRONG_PIPE_ORDER
#endif

// Wow64DisableWow64FsRedirection: 32-bit XP has no WOW64 and no such
// export (64-bit Windows has both); without WOW64 there is nothing to turn off.
#if defined(_M_IX86)
static __inline BOOL DokanXpWow64DisableWow64FsRedirection(PVOID *OldValue) {
  typedef BOOL(WINAPI * PFN_WOW64_DISABLE)(PVOID *);
  PFN_WOW64_DISABLE disable = (PFN_WOW64_DISABLE)GetProcAddress(
      GetModuleHandleW(L"kernel32.dll"), "Wow64DisableWow64FsRedirection");
  if (!disable) {
    if (OldValue) {
      *OldValue = NULL;
    }
    return TRUE;
  }
  return disable(OldValue);
}
#define Wow64DisableWow64FsRedirection DokanXpWow64DisableWow64FsRedirection
#endif

// InterlockedAdd: the x86 headers of the WDK 7.1 only have it for x64.
#if defined(_M_IX86) && !defined(InterlockedAdd)
#define InterlockedAdd(Addend, Value)                                        \
  (InterlockedExchangeAdd((Addend), (Value)) + (Value))
#endif

#ifndef _Out_writes_bytes_
#define _Out_writes_bytes_(size)
#endif
#ifndef _In_reads_bytes_
#define _In_reads_bytes_(size)
#endif
#ifndef _Out_writes_
#define _Out_writes_(size)
#endif
#ifndef _In_reads_
#define _In_reads_(size)
#endif

// ---------------------------------------------------------------------------
// For the mirror sample: Vista file APIs, with their NT equivalents on XP.
// ---------------------------------------------------------------------------
#ifndef FILE_ATTRIBUTE_INTEGRITY_STREAM
#define FILE_ATTRIBUTE_INTEGRITY_STREAM 0x00008000
#endif
#ifndef FILE_ATTRIBUTE_NO_SCRUB_DATA
#define FILE_ATTRIBUTE_NO_SCRUB_DATA 0x00020000
#endif
#ifndef ATTRIBUTE_SECURITY_INFORMATION
#define ATTRIBUTE_SECURITY_INFORMATION 0x00000020L
#define SCOPE_SECURITY_INFORMATION 0x00000040L
#define PROCESS_TRUST_LABEL_SECURITY_INFORMATION 0x00000080L
#define BACKUP_SECURITY_INFORMATION 0x00010000L
#endif

#if (_WIN32_WINNT < 0x0600)
typedef enum _DOKAN_XP_FILE_INFO_BY_HANDLE_CLASS {
  FileBasicInfo,
  FileStandardInfo,
  FileNameInfo,
  FileRenameInfo,
  FileDispositionInfo
} DOKAN_XP_FILE_INFO_BY_HANDLE_CLASS;

typedef struct _FILE_RENAME_INFO {
  BOOLEAN ReplaceIfExists;
  HANDLE RootDirectory;
  DWORD FileNameLength;
  WCHAR FileName[1];
} FILE_RENAME_INFO, *PFILE_RENAME_INFO;

typedef struct _FILE_DISPOSITION_INFO {
  BOOLEAN DeleteFile;
} FILE_DISPOSITION_INFO, *PFILE_DISPOSITION_INFO;

typedef struct _DOKAN_XP_IO_STATUS_BLOCK {
  union {
    LONG Status;
    PVOID Pointer;
  } u;
  ULONG_PTR Information;
} DOKAN_XP_IO_STATUS_BLOCK;

// SetFileInformationByHandle (Vista) for a rename or a delete: on XP the
// NtSetInformationFile call that Vista's kernel32 makes (a full DOS path
// becomes an NT path; a stream name ":name" stays as it is).
static __inline BOOL DokanXpSetFileInformationByHandle(HANDLE File,
                                                       int InfoClass,
                                                       LPVOID Info,
                                                       DWORD Size) {
  typedef BOOL(WINAPI * PFN_SET)(HANDLE, int, LPVOID, DWORD);
  typedef LONG(WINAPI * PFN_NTSET)(HANDLE, DOKAN_XP_IO_STATUS_BLOCK *, PVOID,
                                   ULONG, int);
  typedef ULONG(WINAPI * PFN_TODOS)(LONG);
  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  PFN_SET set = (PFN_SET)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                        "SetFileInformationByHandle");
  PFN_NTSET ntSet =
      (PFN_NTSET)GetProcAddress(ntdll, "NtSetInformationFile");
  PFN_TODOS toDos = (PFN_TODOS)GetProcAddress(ntdll, "RtlNtStatusToDosError");
  DOKAN_XP_IO_STATUS_BLOCK iosb;
  LONG status;
  if (set) {
    return set(File, InfoClass, Info, Size);
  }
  if (!ntSet || !toDos) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
  }
  if (InfoClass == FileDispositionInfo) {
    status = ntSet(File, &iosb, Info, sizeof(BOOLEAN), 13);
  } else if (InfoClass == FileRenameInfo) {
    PFILE_RENAME_INFO in = (PFILE_RENAME_INFO)Info;
    DWORD chars = in->FileNameLength / sizeof(WCHAR);
    BOOL drive = chars >= 3 && in->FileName[1] == L':' && in->FileName[2] == L'\\';
    BOOL unc = chars >= 2 && in->FileName[0] == L'\\' && in->FileName[1] == L'\\';
    LPCWSTR prefix = drive ? L"\\??\\" : (unc ? L"\\??\\UNC" : L"");
    DWORD prefixChars = (DWORD)wcslen(prefix);
    DWORD skip = unc ? 1 : 0;
    DWORD newChars = prefixChars + chars - skip;
    DWORD size = (DWORD)(FIELD_OFFSET(FILE_RENAME_INFO, FileName) +
                         (newChars + 1) * sizeof(WCHAR));
    PFILE_RENAME_INFO out = (PFILE_RENAME_INFO)HeapAlloc(
        GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!out) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return FALSE;
    }
    out->ReplaceIfExists = in->ReplaceIfExists;
    out->RootDirectory = in->RootDirectory;
    out->FileNameLength = newChars * sizeof(WCHAR);
    CopyMemory(out->FileName, prefix, prefixChars * sizeof(WCHAR));
    CopyMemory(out->FileName + prefixChars, in->FileName + skip,
               (chars - skip) * sizeof(WCHAR));
    status = ntSet(File, &iosb, out, size, 10);
    HeapFree(GetProcessHeap(), 0, out);
  } else {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (status < 0) {
    SetLastError(toDos(status));
    return FALSE;
  }
  return TRUE;
}
#define SetFileInformationByHandle DokanXpSetFileInformationByHandle

// For the mirror sample's DeleteDirectory on XP: STATUS_SUCCESS when the
// folder has no entries but "." and "..", STATUS_DIRECTORY_NOT_EMPTY when it
// has. Lists through a handle that shares delete (see mirror.c).
static __inline NTSTATUS DokanXpCheckDirectoryEmpty(LPCWSTR DirectoryPath) {
  typedef LONG(WINAPI * PFN_QUERYDIR)(HANDLE, HANDLE, PVOID, PVOID,
                                      DOKAN_XP_IO_STATUS_BLOCK *, PVOID,
                                      ULONG, int, BOOLEAN, PVOID, BOOLEAN);
  // FILE_NAMES_INFORMATION (FileNamesInformation = 12)
  typedef struct _XP_NAMES {
    ULONG NextEntryOffset;
    ULONG FileIndex;
    ULONG FileNameLength;
    WCHAR FileName[1];
  } XP_NAMES;
  PFN_QUERYDIR query = (PFN_QUERYDIR)GetProcAddress(
      GetModuleHandleW(L"ntdll.dll"), "NtQueryDirectoryFile");
  ULONG buffer[1024];
  DOKAN_XP_IO_STATUS_BLOCK iosb;
  BOOLEAN restart = TRUE;
  NTSTATUS result = STATUS_SUCCESS;
  HANDLE dir;
  if (!query) {
    return STATUS_NOT_IMPLEMENTED;
  }
  dir = CreateFileW(DirectoryPath, FILE_LIST_DIRECTORY | SYNCHRONIZE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (dir == INVALID_HANDLE_VALUE) {
    switch (GetLastError()) {
    case ERROR_FILE_NOT_FOUND:
      return STATUS_OBJECT_NAME_NOT_FOUND;
    case ERROR_PATH_NOT_FOUND:
      return STATUS_OBJECT_PATH_NOT_FOUND;
    case ERROR_ACCESS_DENIED:
      return STATUS_ACCESS_DENIED;
    case ERROR_SHARING_VIOLATION:
      return STATUS_SHARING_VIOLATION;
    default:
      return STATUS_UNSUCCESSFUL;
    }
  }
  for (;;) {
    XP_NAMES *entry = (XP_NAMES *)buffer;
    LONG status = query(dir, NULL, NULL, NULL, &iosb, buffer, sizeof(buffer),
                        12, FALSE, NULL, restart);
    restart = FALSE;
    if (status == STATUS_NO_MORE_FILES) {
      break;
    }
    if (status < 0) {
      result = status;
      break;
    }
    for (;;) {
      ULONG chars = entry->FileNameLength / sizeof(WCHAR);
      if (!(chars == 1 && entry->FileName[0] == L'.') &&
          !(chars == 2 && entry->FileName[0] == L'.' &&
            entry->FileName[1] == L'.')) {
        CloseHandle(dir);
        return STATUS_DIRECTORY_NOT_EMPTY;
      }
      if (!entry->NextEntryOffset) {
        break;
      }
      entry = (XP_NAMES *)((PCHAR)entry + entry->NextEntryOffset);
    }
  }
  CloseHandle(dir);
  return result;
}

// FindFirstStreamW/FindNextStreamW (Vista): XP has no stream enumeration
// API; the mirror then reports that it cannot list streams.
static __inline HANDLE DokanXpFindFirstStreamW(LPCWSTR FileName, int InfoLevel,
                                               LPVOID FindStreamData,
                                               DWORD Flags) {
  typedef HANDLE(WINAPI * PFN_FIRST)(LPCWSTR, int, LPVOID, DWORD);
  PFN_FIRST first = (PFN_FIRST)GetProcAddress(
      GetModuleHandleW(L"kernel32.dll"), "FindFirstStreamW");
  if (first) {
    return first(FileName, InfoLevel, FindStreamData, Flags);
  }
  SetLastError(ERROR_INVALID_FUNCTION);
  return INVALID_HANDLE_VALUE;
}
#define FindFirstStreamW DokanXpFindFirstStreamW

static __inline BOOL DokanXpFindNextStreamW(HANDLE FindStream,
                                            LPVOID FindStreamData) {
  typedef BOOL(WINAPI * PFN_NEXT)(HANDLE, LPVOID);
  PFN_NEXT next = (PFN_NEXT)GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
                                           "FindNextStreamW");
  if (next) {
    return next(FindStream, FindStreamData);
  }
  SetLastError(ERROR_HANDLE_EOF);
  return FALSE;
}
#define FindNextStreamW DokanXpFindNextStreamW
#endif // _WIN32_WINNT < 0x0600

// ---------------------------------------------------------------------------
// The Vista thread pool: XP has none, xp_threadpool.c has what dokan2.dll
// uses. The Vista (x64) build uses the system thread pool.
// ---------------------------------------------------------------------------
#if (_WIN32_WINNT < 0x0600)
#include "xp_threadpool.h"
#define PTP_POOL PDOKAN_XP_TP_POOL
#define PTP_CLEANUP_GROUP PDOKAN_XP_TP_CLEANUP_GROUP
#define TP_CALLBACK_ENVIRON DOKAN_XP_TP_CALLBACK_ENVIRON
#define PTP_CALLBACK_ENVIRON PDOKAN_XP_TP_CALLBACK_ENVIRON
#define PTP_WORK PDOKAN_XP_TP_WORK
#define PTP_WORK_CALLBACK PDOKAN_XP_TP_WORK_CALLBACK
#define PTP_CALLBACK_INSTANCE PDOKAN_XP_TP_CALLBACK_INSTANCE
#define CreateThreadpool DokanXpCreateThreadpool
#define CloseThreadpool DokanXpCloseThreadpool
#define CreateThreadpoolCleanupGroup DokanXpCreateThreadpoolCleanupGroup
#define CloseThreadpoolCleanupGroupMembers                                   \
  DokanXpCloseThreadpoolCleanupGroupMembers
#define CloseThreadpoolCleanupGroup DokanXpCloseThreadpoolCleanupGroup
#define CreateThreadpoolWork DokanXpCreateThreadpoolWork
#define SubmitThreadpoolWork DokanXpSubmitThreadpoolWork
#define InitializeThreadpoolEnvironment DokanXpInitializeThreadpoolEnvironment
#define SetThreadpoolCallbackPool DokanXpSetThreadpoolCallbackPool
#define SetThreadpoolCallbackCleanupGroup                                    \
  DokanXpSetThreadpoolCallbackCleanupGroup
#define DestroyThreadpoolEnvironment DokanXpDestroyThreadpoolEnvironment
#endif

#endif // DOKAN_XP_COMPAT_UM_H_
