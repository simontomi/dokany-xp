/*
  Dokany XP port check: how a file system answers file name queries with
  small buffers (the Dokan 0.6.0 bug that breaks the Vista firewall: a buffer
  smaller than the name gave STATUS_INSUFFICIENT_RESOURCES without a length,
  where NTFS gives STATUS_BUFFER_OVERFLOW with FileNameLength filled in).
  Usage: namequery <file path>     (compare a Dokany drive with C:)
*/
#include <windows.h>
#include <stdio.h>

typedef LONG NTSTATUS;
typedef struct _IO_STATUS_BLOCK {
  union { NTSTATUS Status; PVOID Pointer; };
  ULONG_PTR Information;
} IO_STATUS_BLOCK;
typedef NTSTATUS(NTAPI *PFN_QUERY)(HANDLE, IO_STATUS_BLOCK *, PVOID, ULONG, int);
typedef DWORD(WINAPI *PFN_FINAL)(HANDLE, LPWSTR, DWORD, DWORD);

int __cdecl wmain(int argc, WCHAR *argv[]) {
  PFN_QUERY query = (PFN_QUERY)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationFile");
  PFN_FINAL final = (PFN_FINAL)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetFinalPathNameByHandleW");
  static const int classes[] = {9 /* FileNameInformation */, 48 /* FileNormalizedNameInformation */};
  int failures = 0;
  int c;
  HANDLE h;
  if (argc < 2) { wprintf(L"usage: namequery <file>\n"); return 2; }
  h = CreateFileW(argv[1], FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
  if (h == INVALID_HANDLE_VALUE) { wprintf(L"NAMEQUERY FAIL open %u\n", GetLastError()); return 1; }
  for (c = 0; c < 2; c++) {
    ULONG sizes[] = {4, 8, 12, 16, 512};
    ULONG fullLength = 0;
    int s;
    for (s = 0; s < 5; s++) {
      ULONG buffer[256];
      IO_STATUS_BLOCK iosb;
      NTSTATUS status;
      ZeroMemory(buffer, sizeof(buffer));
      buffer[0] = 0xCCCCCCCC;
      iosb.Status = 0; iosb.Information = 0;
      status = query(h, &iosb, buffer, sizes[s], classes[c]);
      wprintf(L"class %2d size %3u: status 0x%08X info %3u FileNameLength %u%s\n", classes[c], sizes[s],
              (ULONG)status, (ULONG)iosb.Information, buffer[0] == 0xCCCCCCCC ? 0 : buffer[0],
              buffer[0] == 0xCCCCCCCC ? L" (not set)" : L"");
      if (sizes[s] == 512 && status == 0) {
        fullLength = buffer[0];
        wprintf(L"  name: %.*s\n", (int)(buffer[0] / 2), (WCHAR *)&buffer[1]);
      }
      if (sizes[s] < 512 && status != 0x80000005L /* STATUS_BUFFER_OVERFLOW */ && status != 0xC0000003L /* invalid class */ &&
          status != 0xC0000004L /* STATUS_INFO_LENGTH_MISMATCH, size < 4 */) {
        wprintf(L"  NAMEQUERY BAD: small buffer gave 0x%08X, expected STATUS_BUFFER_OVERFLOW\n", (ULONG)status);
        failures++;
      }
    }
    if (classes[c] == 9 && fullLength == 0) failures++;
  }
  if (final) {
    WCHAR path[1024];
    DWORD n = final(h, path, 1024, 0 /* normalized, DOS */);
    wprintf(L"GetFinalPathNameByHandle(normalized): %u %s (error %u)\n", n, n ? path : L"", n ? 0 : GetLastError());
    n = final(h, path, 1024, 2 /* VOLUME_NAME_NT */);
    wprintf(L"GetFinalPathNameByHandle(NT): %u %s (error %u)\n", n, n ? path : L"", n ? 0 : GetLastError());
  }
  CloseHandle(h);
  {
    /* The Vista firewall's own call (Dokan 0.6.0 made it fail with 0x5AA). */
    typedef DWORD(WINAPI *PFN_APPID)(const WCHAR *, PVOID *);
    typedef void(WINAPI *PFN_FREE)(PVOID *);
    HMODULE fw = LoadLibraryW(L"fwpuclnt.dll");
    PFN_APPID appId = fw ? (PFN_APPID)GetProcAddress(fw, "FwpmGetAppIdFromFileName0") : NULL;
    PFN_FREE freeMem = fw ? (PFN_FREE)GetProcAddress(fw, "FwpmFreeMemory0") : NULL;
    if (appId) {
      PVOID blob = NULL;
      DWORD r = appId(argv[1], &blob);
      wprintf(L"FwpmGetAppIdFromFileName0: 0x%X\n", r);
      if (r == 0 && freeMem) freeMem(&blob);
      if (r != 0) failures++;
    }
  }
  wprintf(failures ? L"NAMEQUERY FAIL %d\n" : L"NAMEQUERY OK\n", failures);
  return failures ? 1 : 0;
}
