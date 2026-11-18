# Dokany 2.3.1.1000 for Windows XP and Vista

**AI-generated ("vibe coded").** This port, its build files, its tools and
this README were written by Claude, Anthropic's AI model (Claude Opus 5.5 in
Claude Code). The person who published it asked for the port and ran the
tests below on real PCs, but did not write the code, and the Dokany
developers have not reviewed it. It is not an official Dokany release: use it
at your own risk.

This branch (`xp-port`) builds Dokany 2.3.1.1000 (driver `dokan2.sys`, library
`dokan2.dll`, `dokanctl.exe` and the `dokan_mirror.exe` sample) for:

| Build | Target | Runs on |
|---|---|---|
| `xp\build.cmd x86` | Windows XP (WDK 7.1 `WXP`) | 32-bit XP SP3, Vista, 7 |
| `xp\build.cmd x64` | Windows Vista (WDK 7.1 `WLH`) | 64-bit Vista SP2, 7 |

Upstream Dokany needs Windows 7: its driver calls 12 kernel routines that XP
does not export (3 of them are Windows 7 only, so it does not load on Vista
either), its library uses the Vista thread pool, and current Visual Studio and
WDK versions cannot build for XP at all.

## Build

Needs the Windows Driver Kit 7.1.0 (7600.16385.1, `GRMWDK_EN_7600_1.ISO`) in
`C:\WinDDK\7600.16385.1` (or set `WDK71`), with its XP and Vista libraries
(`wxplibs_x86fre`, `vistalibs_x64fre`).

```
xp\build.cmd x86 fre
xp\build.cmd x64 fre
```

The output goes to `xp\out\<arch>\` (symbols in `pdb\sys` and `pdb\dll`, both
named `dokan2.pdb`). The WDK's `build.exe` only compiles sources in its own
folder (or `..`), so `xp\sys`, `xp\dokan`, `xp\dokan_control` and
`xp\samples\dokan_mirror` hold one-line wrappers that `#include` the upstream
files; nothing of the upstream Visual Studio build changes.

The x64 driver must be signed to load: test signing (`bcdedit /set testsigning
on`) and a test certificate, or a Code Integrity patch.

Every build is stamped (2026-11-18): `r<n> (<hash>)`, where `n` counts the
commits since `v2.3.1.1000` and `hash` is the short commit hash (`-dirty` after
it when the tree had changes). `build.cmd` writes it to `xp\xp_build.h`
(generated, not in git) and `xp\out\<arch>\build.txt`. Both files carry it in
their ProductVersion (`2.3.1.1000 XP port r5 (1a2b3c4)`), and the driver
answers it for the one that is loaded: `DokanXpDriverBuild` in the library
(`FSCTL_XP_GET_BUILD` to the global device; a driver from before the stamp
fails it). After an update the file on disk can be newer than the driver that
runs until the next start of Windows; this tells the two apart.

## What changed for XP

Everything is in four files and a few `#ifdef DOKAN_XP_COMPAT` blocks:

- `sys\xp_compat.h` (forced into every driver file): SAL 2 annotations and
  constants the WDK 7.1 lacks (NTDDI values, newer info classes, IOCTL and
  NTSTATUS names, generated: `sys\xp_ntstatus.h`, `sys\xp_ioctl.h`), the
  replacements for the routines XP does not export (table below) and the
  port's own base GUID for volume names.
- `sys\xp_compat.c`: `FsRtlNotifyCleanupAll` (handle by handle) and the
  pull wait for events.

### The kernel routines XP does not export, and what replaces them

Measured: every routine below is missing from XP SP3's `ntoskrnl.exe`
(T43p and the XP install media); the Vista column says whether Vista SP2 has
it (where it does, the x64 Vista build calls the real one unless noted).

| Routine (first Windows) | What Dokany uses it for | XP build | Why that is equivalent |
|---|---|---|---|
| `ExInitializeLookasideListEx`, `ExAllocateFromLookasideListEx`, `ExFreeToLookasideListEx`, `ExDeleteLookasideListEx` (Vista) | Caches of FCBs, CCBs, ERESOURCEs and FCB table nodes | `NPAGED_LOOKASIDE_LIST` (`ExInitializeNPagedLookasideList`, ...) | All four lists are created as non-paged pool with no allocate/free callbacks and no flags, which is exactly what the XP list offers. |
| `ExEnterCriticalRegionAndAcquireResourceExclusive`, `ExReleaseResourceAndLeaveCriticalRegion` (Server 2003) | Every exclusive FCB/VCB/global lock (`DokanResourceLockRW`) | `KeEnterCriticalRegion` + `ExAcquireResourceExclusiveLite(Wait=TRUE)`, and `ExReleaseResourceLite` + `KeLeaveCriticalRegion` | That is what the combined routines do (they are a shortcut); their return value is not used. Used on Vista too. |
| `KeRemoveQueueEx` (Vista) | The pull IOCTL waits on the event queue for work | `KeRemoveQueue` in 500 ms slices, stopping when the volume unmounts (`DokanXpRemoveQueueUntilUnmount`) | Dokany removes one entry at a time (count 1). The slices are needed because XP's `KeRundownQueue` does not wake waiters (see below). Vista uses the real routine. |
| `FsRtlNotifyCleanupAll` (Vista) | At unmount: complete every pending directory change notification | Walk the VCB's FCB table and call `FsRtlNotifyCleanup` for every CCB | The CCB is the `FsContext` each notification was registered with, so every pending notify IRP completes (`STATUS_NOTIFY_CLEANUP`), as with the Vista routine. |
| `FsRtlGetEcpListFromIrp`, `FsRtlFindExtraCreateParameter`, `FsRtlIsEcpFromUserMode` (Vista) | Undo a file name rewrite when a folder mount point is reached through a reparse point | "No ECP list", so the function returns early | XP has no extra create parameters at all, and the case needs a folder mount point (a drive letter needs neither). Vista uses the real routines. |
| `MmDoesFileHaveUserWritableReferences` (Vista) | Refuse an open without write sharing while a writable user mapping exists but no handle is open | Check skipped (`FALSE`) | XP cannot answer the question; the open follows the share access of the open handles, as XP's own NTFS does. Vista uses the real routine. |
| `RtlIsNtDdiVersionAvailable` (Vista) | Switch on Windows 8/10 features (no-execute pool and MDLs, oplock lock checks, a reparse name fix) | The WDK 7.1 `rtlver.lib`, made for XP drivers (based on `RtlGetVersion`) | Answers "no" for 8 and 10 on XP, Vista and 7, as upstream does on 7. |
| `FsRtlCheckOplockEx` (Vista; the flags used are Windows 7) | (1) store/check the caller's oplock key on create, (2) back out an atomic oplock request | Those two flags: `STATUS_SUCCESS`; anything else: legacy `FsRtlCheckOplock` | Oplock keys and atomic oplocks (`FILE_OPEN_REQUIRING_OPLOCK`) are Windows 7 features the XP/Vista I/O manager never uses. Used on Vista too. |
| `FsRtlOplockBreakH` (Windows 7) | Break handle-caching oplocks before failing an open with a sharing violation | `STATUS_SUCCESS` ("nothing to break") | XP and Vista have no handle (RH) oplocks; the sharing violation is returned as before. |
| `FsRtlOplockIsSharedRequest` (Windows 7) | Is an oplock request for a shared oplock | A level 2 request, or a Windows 7 request without write caching | The same answer the Windows 7 routine gives for the requests XP/Vista can send. |
| `FsRtlAreThereCurrentOrInProgressFileLocks` (Windows 7) | Refuse a level 1/batch oplock while byte-range locks exist | `FsRtlGetNextFileLock(Restart=TRUE) != NULL` | Sees every granted lock; a lock still being granted cannot be seen on XP (the oplock is then granted and broken by the lock, as on XP's own NTFS). |
| `FsRtlCheckLockForOplockRequest`, `FsRtlAreThereWaitingFileLocks` (Windows 8) | Same check on 8 and later | Unchanged | Upstream already looks them up at run time, on Windows 8 and later only. |

Not routines, but also needed: `ExInitializeDriverRuntime` (Windows 8
no-execute pool) is a no-op; `DEVOBJ_EXTENSION.ExtensionFlags`, which the
WDK 7.1 headers hide, is read through a structure with the same layout.
- `dokan\xp_compat.h` (forced into the library, dokanctl and the sample) and
  `dokan\xp_threadpool.c`: the Vista thread pool calls dokan2.dll uses, on a
  pool that grows on demand (the pulling threads block for the whole mount).
- C89: the WDK 7.1 compiler wants declarations at the start of a block;
  `xp\tools\c89hoist.py` moved about 200 of them (declaration up, assignment
  where it was).

## Additions that upstream does not have (2026-11-18)

Both keep the upstream behaviour unless the library and the driver of this
port meet: the library asks for them with new `EVENT_START` flags, the driver
answers with new `EVENT_DRIVER_INFO` flags (high bits, `sys\public.h`), and
neither side changes anything for a partner that does not know them.

- **Short pulls.** Without IPC batching the library pulls one request at a
  time, but upstream always asks `FSCTL_EVENT_PROCESS_N_PULL` (a
  `METHOD_BUFFERED` ioctl) for 128 KiB, so the I/O manager allocates and frees
  a 128 KiB system buffer per request. The library now asks for 32 KiB
  (`EVENT_CONTEXT_MAX_SIZE`: the driver refuses larger requests, large writes
  fetch their data with `FSCTL_EVENT_WRITE`), and with this port's driver for
  3.5 KiB (or the size of the reply it sends): when the next request does not
  fit, the driver answers `STATUS_BUFFER_OVERFLOW` with the length it needs
  and keeps the request; the library pulls again with the whole buffer. On a
  Core 2 Duo with Vista the buffered ioctl costs 6.8 us with 3.5 KiB, 11.2 us
  with 32 KiB and 19.3 us with 128 KiB.
- **Kernel read cache** (`DOKAN_OPTION_XP_READ_CACHE`, `sys\xp_cache.c`).
  Upstream never sets up the cache manager (every read goes to user mode) and
  purges every section of a file at each cleanup. With the option, a file
  system marks an open cacheable in its `ZwCreateFile` with
  `DokanXpSetFileCache(DokanFileInfo, FileSize, ContentId)`; the create reply
  carries the mark. A file whose opens are all marked (no write or delete
  access) with the same size and content id is read through the Windows cache:
  fast I/O and read IRPs copy from the system cache, and only the cache
  manager's paging reads (misses, read-ahead) go to user mode. Its cached data
  and image section stay when a handle closes. Caching of a file starts only
  when it has no handle and no section (nothing of an earlier version can be
  in memory), and ends at an open that is not marked, asks for write or delete
  access or reports another size or content id (its pages, cache maps and
  unmapped image section are dropped before that open completes),
  at `DokanXpPurgeCache` (a new FSCTL: one path, everything below a path, or
  everything) and at the unmount. A cached file deleted with its last handle
  leaves the FCB table, so its name can be created again while the cache
  manager still holds the old file object (as NTFS frees the name at the last
  cleanup). Cached reads hold the FCB's paging I/O resource shared, not its
  main resource (`FsRtlCopyRead` would, and the create completion of a pull
  thread waits for that one); MDL fast reads are not offered.
  `DokanXpReadCacheActive` tells whether the driver took the option.
  `dokan_mirror /z` marks every read-only open (content id: last write time)
  and purges everything when `\__xp_purge` is opened; it prints its ReadFile
  callbacks at unmount.
- **No pull thread waits for a busy FCB lock** (`sys\xp_defer.c`, always on,
  with or without the read cache; `DOKAN_XP_DEFER` 0 in `sys\dokan.h` builds
  the upstream behaviour for comparisons). A thread may hold an FCB's main
  resource while it waits for user mode: section creation does
  (`AcquireFileForNtCreateSection` holds it while the memory manager asks for
  the file size with an `IRP_MJ_QUERY_INFORMATION`, or reads an image header).
  Upstream completes every reply in the pull thread that delivers it, and the
  create and cleanup completions take that resource. With every pull thread
  waiting there, the request that would release it is never pulled. The
  timeout thread would fail it after 15 s, but it cancels timed-out creates
  through `DokanCompleteCreate` and then waits too: the volume stalls for
  good. On XP (T43p, one CPU, 2026-11-18) `fc /b`, which maps the files it
  compares, and five more readers of the same file stalled a mirror drive
  within a minute, every time; before that an `fc` failed now and then with
  "cannot open" (its mapping timed out). Now a reply whose completion takes
  an FCB resource that another thread owns at that moment (create, cleanup,
  set information, `FileAllInformation`), and a timed-out create or cleanup
  in the same case, is completed by a system worker thread; the purges that
  end a file's caching run there as well (they wait for the paging I/O
  resource, which a cached read holds while its page fault is in user mode).
  A size query of a cacheable file is answered from the FCB (fast I/O), so
  mapping it needs no round trip to user mode while the resource is held.
- **Lock debugging on 32-bit Windows.** `dokanctl /d 2` (`DOKAN_DEBUG_LOCK`)
  printed pointers with `%I64x` and crashed 32-bit Windows (bugcheck 0x8E) at
  its first "Stuck trying to lock" message; it prints them with `%p` now.

**Status (2026-11-18): experimental; the stall of the stress test is found and
fixed on XP.** Passed on the XP, Vista and 7 VMs (mount, reads, writes, a
change behind the drive, delete and re-create, programs, purge, unmount,
8 x 10 stress) and with the test battery of a .NET 3.5 file system on T61p-2 and
T43p. The stress: 6
parallel readers of one file, each round `fc /b` against a copy, copy out and
compare, `dir`, `type` (started outside the
WinRM shell, whose memory cap makes `fc` fail with "Not enough quota"):
- Vista x64 (T61p-2, two CPUs, the build before the fix): 6 runs of 6 x 16
  rounds on a mirror drive passed. Vista's `fc` reads into buffers; it never
  maps the file.
- XP (T43p, one CPU): before the fix every first run stalled within a minute,
  and now and then an `fc` failed with "cannot open" first, as on the XP VM.
  The cause is the cycle described under "No pull thread waits for a busy FCB
  lock": both pull threads of the mirror and the timeout thread were waiting
  for the FCB resource, and a fresh `fc` that held it waited in the kernel
  for its size query (thread states; with lock debugging the waiters poll,
  and the first lock-debug run crashed in `DokanCompleteCreate`'s lock wait,
  see "Lock debugging on 32-bit Windows"). With the fix: 8 runs of 6 x 16 on a
  mirror drive and 3 on the drive of a .NET 3.5 file system passed, and that
  file system's test battery (78 checks).
- Vista x64 (T61p-2) with the fix: 3 runs of 6 x 16 on a mirror drive and
  the same battery (79 checks) passed.
- The stalls of the VMs (Vista: a 64-bit thread calling `NtCancelIoFile` in a
  loop) were not looked at again; the VMs broke for other reasons.

## The kernel read cache in a nutshell, and what it gains (2026-11-18)

**Without it** (upstream Dokany) every read of a program goes the whole
way: the I/O manager hands it to the Dokany driver, the driver queues it, a
thread of the file system process pulls it, the file system answers from
wherever it keeps the data, and the reply goes back the same way. Windows
caches nothing of a Dokany volume, so reading the same 512 bytes a second
time costs the same round trip again: about 66 µs on a Core 2 Duo, against
about 7 µs for a file on a local disk or on an SMB share, which Windows does
cache. A program that does hundreds of thousands of small reads waits seconds
for nothing but round trips.

**With it** the driver sets up the Windows cache manager for the files the
file system marks cacheable when it opens them (`DokanXpSetFileCache` with
the file's size and a content id, for example its last write time). Reads of
such a file are copied from the system cache in the kernel (fast I/O, or the
read IRP), the way NTFS serves them; only what is not in the cache yet goes
to user mode, as paging reads of 64 KiB (cache misses and read-ahead). The
cached pages stay after the handles close, so the next start of a program
finds them in memory. A marked open with another size or content id, an open
that may write, or `DokanXpPurgeCache` drops what is cached of the file, so a
file that changed is never served stale. It is the file system's choice which
files to mark: files that only change through the file system itself or not
at all suit it best.

**Benchmarks.** A user-mode file system that serves a game from an SMB
share, with its own copy of the file data on the local disk, so the file
system answers every read from local disk either way. The game (iw4x) does
about 200,000 reads of 512 bytes before its window appears; the time is from
the start of the game to its main menu, windowed; hot rows are the median of 3 to 5 runs, cold rows the mean of 2. "Cold": the file system's own disk copy and the Windows cache were
emptied before the run; "hot": everything was cached by an earlier run.

| PC | Driver | Cold | Hot | Game window |
|---|---|---|---|---|
| Core 2 Duo T7500, Vista x64 | this port without the cache | 52.1 s | 46.3 s | ~23 s |
| Core 2 Duo T7500, Vista x64 | this port with the cache | 37.4 s | 31.5 s | 4-10 s |
| Core 2 Duo T7500, Vista x64 | (game on a local disk) | | 30.0 s | |
| Core 2 Duo T7500, Vista x64 | (game read straight from the share) | | 40.2 s | |
| Core i5 M520, Windows 7 x64 | official Dokany 2.3.1 | 42.9 s | 35.0 s | 13-14 s |
| Core i5 M520, Windows 7 x64 | this port with the cache | 30.5 s | 23.1 s | 3-4 s |

With the cache a hot start is as fast as from a local disk, and faster than
from the share directly. A start with the Windows cache empty but the file
system's disk copy warm (the first start after a boot) took 24.5 s on the
Windows 7 PC, close to a hot start: the 64 KiB paging reads cost little. A
single hot 512-byte read went from 66 µs to 6 µs (Vista x64). On Windows 7
and newer the port's x64 driver is only test-signed: it loads in test signing
mode or with a Code Integrity patch.

## XP behaviour differences found by testing

- **Unmount hang.** At unmount the driver runs its event queue down
  (`KeRundownQueue`). From Vista on that wakes the threads waiting on the
  queue; on XP they keep waiting for ever (the difference Microsoft documents
  for `GetQueuedCompletionStatus` on a closed completion port), so the file
  system process never ended. The XP build waits in 500 ms slices and stops
  once the volume unmounts (`DokanXpRemoveQueueUntilUnmount`).
- **FindFirstFile and delete.** XP's `FindFirstFile` opens a folder without
  `FILE_SHARE_DELETE`, which collides with the delete request's own handle:
  the mirror sample's "is the folder empty" check failed with a sharing
  violation. The sample now lists through a handle that shares delete on XP.
  A file system that lists a folder inside its DeleteDirectory callback must
  do the same on XP.
- **First mount next to Dokan 0.6.0.** Dokan 0.6.0 (which may stay installed next to
  the port on XP and Vista) and Dokany name their volume devices from the same base GUID
  `{D6CC17C5-1734-4085-BCE7-964F1E9F5DE9}` XOR'd with a mount counter that
  starts at 0 in each driver. With a 0.6.0 drive mounted, the port's first
  volume after its driver loaded got the same device name:
  `IoCreateDeviceSecure` failed with `STATUS_OBJECT_NAME_COLLISION` and the
  file system got `DOKAN_START_ERROR` ("Driver something wrong"); the second
  mount worked because the counter had moved on. Seen on T43p and T61p-2,
  reproduced on the XP VM with 0.6.0 installed. The XP build has its own base
  GUID `{6B588B54-627F-4272-8841-D2C36946B13A}` (upstream lets
  `DOKAN_BASE_GUID` be overridden).
- `sc stop Dokan2` leaves the driver in STOP_PENDING (it can then no longer
  mount until a restart). **This is upstream behaviour:** the official
  2.3.1.1000 driver does the same on Windows 7, even when nothing was ever
  mounted (tested with the binary from the official MSI, test-signed because
  the unpatched VM lacks SHA-2 code signing support).

## Tests (2026-11-17, VirtualBox VMs)

`xp\tests\` and the scripts the VMs ran (mirror round, stress, name query):

- XP SP3 x86, Vista SP2 x64, 7 SP1 x64: mount, read/write, mkdir/rmdir
  (non-empty refused), rename, move, an 8 MB copy compared byte for byte,
  xcopy trees in and out, an exe run from the drive, tree delete, unmount (the
  file system process ends), remount, unmount.
- XP: 8 parallel workers x 15 rounds of copy/compare/rename/list/delete:
  240 of 240 compares equal, clean unmount.
- Vista: `FileNameInformation` with a small buffer answers
  `STATUS_BUFFER_OVERFLOW` with the length (as NTFS; Dokan 0.6.0 did not),
  and the firewall's `FwpmGetAppIdFromFileName0` succeeds for an exe on the
  drive (it failed with 0x5AA on Dokan 0.6.0).
- `xp\tools\check_imports.py`: every import of the x86 build resolves in
  XP SP3's system files, every import of the x64 build in Vista SP2 x64's
  (and the x86 dokan2.dll in Vista's WOW64).

## Tests on real PCs (2026-11-17)

The same round (file operations, name and firewall query, 4 workers x 10
rounds of stress, unmount), next to the drive G: of another file system (left mounted):

- T43p (XP SP3, single core, Dokan 0.6.0 for G:): port driver x86. Before
  the base GUID fix the first mount after the driver's first load failed (the
  collision above), the rest passed in 5 runs. After the fix and a restart:
  fresh driver load, first mount at once, full round: passed.
- T61p-2 (Vista SP2 x64, Dokan 0.6.0 for G:, Code Integrity patch): port
  driver x64: the same first-mount failure before the fix; after the fix and
  a restart: fresh load, full round, firewall API included: passed.
- T410 (7 SP1 x64, official Dokany 2.3.1 driver for G:): the port's x64
  dokan2.dll and its x86 one (with the XP thread pool, in WOW64) against the
  official driver: both passed.

### The test battery of a .NET 3.5 file system

With the port's x86 dokan2.dll next to the battery program (a 32-bit
.NET 3.5 process) and the port's driver:

| PC | Result |
|---|---|
| T43p, XP SP3 | `REMOTE2 OK`, 4 runs (54 checks; robocopy is skipped on XP) |
| T61p-2, Vista SP2 x64 | `REMOTE2 OK`, 2 runs (55 checks) |
| T410, 7 SP1 x64, official driver | upstream dll: `REMOTE2 OK` (55); the port's dll: `REMOTE2 OK` (55) |

The first run on T43p found a startup race: the file system lists its drive right after
DokanCreateFileSystem returns (from the serving process itself). The driver
serves a volume only once a pull thread has asked it for work (HasEventWait);
an open before that gets a handle without context, and the first query on it
fails with STATUS_INVALID_PARAMETER. The Vista pool runs the pull threads at
once on ready threads; the XP pool started new ones, which lost the race on the
single core T43p. The XP pool now keeps 4 threads ready, and the library waits
until the pull threads are taken (and gives them 50 ms to reach the driver)
before it reports the mount.

### Diagnostics

`DOKAN_DEBUG` (any value) in the environment of the file system process makes
the XP build's dokan2.dll write its debug output to stderr and ask the driver
to forward its log of the mount (`DOKAN_OPTION_DISPATCH_DRIVER_LOGS`), for any
host program, without changing it.

## Installing it on XP and Vista

There is no installer. The files go where the official MSI lays out Dokany 2
on Windows 7: `System32\drivers\dokan2.sys`, `System32\dokan2.dll` (plus the
32-bit one in `SysWOW64` on x64), service `dokan2` as a file system driver,
automatic start, errors ignored. A program that uses the official Dokany
2.3.1 works with it unchanged.

The version resources of this build say `Dokan Library (XP port)` and
`Dokan Driver (XP port)` (`dokan\dokan.rc`, `sys\dokan.rc`, under
`DOKAN_XP_COMPAT`); the version numbers stay 2.3.1.1000, so a program can tell
the port from the official build by the loaded `dokan2.dll`'s FileDescription.

The file of a running driver can be overwritten on XP and Vista; the new
driver takes over at the next start of Windows, since a Dokany 2 driver
cannot be stopped.
