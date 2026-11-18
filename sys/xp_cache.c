/*
  Dokan : user-mode file system library for Windows

  XP port: read caching through the Windows cache manager
  (DOKAN_EVENT_XP_READ_CACHE). Only the XP build (xp\build.cmd) compiles this
  file.

  Upstream Dokany never sets up the cache manager (the FCB's PrivateCacheMap
  stays NULL, FsRtlCopyRead can never succeed), so every read is a round trip
  to user mode, and it flushes and purges every section of a file at each
  cleanup. With this option, the reads of a file whose opens the file system
  marked cacheable (DokanXpSetFileCache in its ZwCreateFile) are served by the
  cache manager: fast I/O and the IRP path copy from the system cache, and
  only its paging reads (misses, read-ahead) go to user mode, through the
  usual paging read path. A cacheable file keeps its cached data and its
  image section when a handle closes.

  When a file is cacheable (FCB XpCacheable):
  - It is set by an open that the file system marked cacheable, does not ask
    for write or delete access, and finds the file with no other handle and
    no section (so no cached data of an earlier version can exist).
  - Later opens keep it while they are marked cacheable with the same size and
    content id. Any other open (a writer, an unmarked open, another size or
    content id) ends it: from then on the file behaves as upstream (reads go
    to user mode, cleanup flushes and purges) until it has no handle and no
    section again.
  - FSCTL_XP_PURGE_CACHE (DokanXpPurgeCache) and the unmount end it and drop
    the cached data and the image section.

  Locks: the cached read paths hold the FCB's paging I/O resource shared
  while the cache manager copies (and maybe waits for a paging read served by
  user mode), as NTFS does; FsRtlCopyRead would hold the FCB resource, which
  the create completion (a pull thread) takes exclusively, and with two pull
  threads that can starve the paging read. Read-ahead takes the same resource
  shared. Purges take it exclusively first, then the FCB resource; no pull
  thread ever waits for it (the purges of a create or cleanup completion run
  in a worker thread, xp_defer.c). A size query of a cacheable file is
  answered from the FCB (fast I/O), so mapping it (the memory manager asks for
  the size while it holds the FCB resource) needs no round trip to user mode.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation; either version 3 of the License, or (at your option) any
  later version.
*/

#include "dokan.h"
#include "util/fcb.h"

// The read-ahead unit of a cached file (the default is one page).
#define DOKAN_XP_READ_AHEAD_GRANULARITY (64 * 1024)

// Access rights that can change what the file holds: such an open is never
// cached, whatever the file system says.
#define DOKAN_XP_WRITE_ACCESS                                                  \
  (FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE | WRITE_DAC | WRITE_OWNER |     \
   GENERIC_WRITE | GENERIC_ALL | MAXIMUM_ALLOWED)

static BOOLEAN DokanXpAcquireForReadAhead(__in PVOID Context,
                                          __in BOOLEAN Wait) {
  PDokanFCB fcb = (PDokanFCB)Context;
  KeEnterCriticalRegion();
  if (!ExAcquireResourceSharedLite(&fcb->PagingIoResource, Wait)) {
    KeLeaveCriticalRegion();
    return FALSE;
  }
  if (IoGetTopLevelIrp() == NULL) {
    IoSetTopLevelIrp((PIRP)FSRTL_CACHE_TOP_LEVEL_IRP);
  }
  return TRUE;
}

static VOID DokanXpReleaseFromReadAhead(__in PVOID Context) {
  PDokanFCB fcb = (PDokanFCB)Context;
  if (IoGetTopLevelIrp() == (PIRP)FSRTL_CACHE_TOP_LEVEL_IRP) {
    IoSetTopLevelIrp(NULL);
  }
  ExReleaseResourceLite(&fcb->PagingIoResource);
  KeLeaveCriticalRegion();
}

VOID DokanXpInitCacheCallbacks(__in PDokanDCB Dcb) {
  // Nothing is ever written through the cache (cached files have no writer),
  // so the lazy writer has nothing to lock.
  Dcb->CacheManagerCallbacks.AcquireForLazyWrite = &DokanNoOpAcquire;
  Dcb->CacheManagerCallbacks.ReleaseFromLazyWrite = &DokanNoOpRelease;
  Dcb->CacheManagerCallbacks.AcquireForReadAhead = &DokanXpAcquireForReadAhead;
  Dcb->CacheManagerCallbacks.ReleaseFromReadAhead =
      &DokanXpReleaseFromReadAhead;
}

static VOID DokanXpEndCaching(__in PDokanFCB Fcb) {
  Fcb->XpCacheable = FALSE;
  Fcb->AdvancedFCBHeader.IsFastIoPossible = FastIoIsNotPossible;
}

BOOLEAN DokanXpCacheOnCreate(__in PREQUEST_CONTEXT RequestContext,
                             __in PDokanFCB Fcb,
                             __in PEVENT_INFORMATION EventInfo) {
  DOKAN_XP_CACHE_INFO info;
  BOOLEAN marked;
  ACCESS_MASK access = 0;
  PSECTION_OBJECT_POINTERS sop = &Fcb->SectionObjectPointers;
  LONGLONG allocationSize;

  if (!RequestContext->Dcb->XpReadCache) {
    return FALSE;
  }
  if (DokanFCBFlagsIsSet(Fcb, DOKAN_FILE_DIRECTORY)) {
    return FALSE;
  }
  if (RequestContext->IrpSp->Parameters.Create.SecurityContext != NULL) {
    access = RequestContext->IrpSp->Parameters.Create.SecurityContext
                 ->DesiredAccess;
  }
  marked = (EventInfo->Operation.Create.Flags & DOKAN_XP_CREATE_CACHEABLE) &&
           !(EventInfo->Operation.Create.Flags & DOKAN_FILE_DIRECTORY) &&
           EventInfo->BufferLength >= sizeof(DOKAN_XP_CACHE_INFO) &&
           !(access & DOKAN_XP_WRITE_ACCESS);
  if (marked) {
    RtlCopyMemory(&info, EventInfo->Buffer, sizeof(info));
    if (info.FileSize < 0) {
      marked = FALSE;
    }
  }

  if (!marked) {
    if (Fcb->XpCacheable) {
      DOKAN_LOG_FINE_IRP(RequestContext,
                         "XP cache: open not cacheable, caching ends FCB=%p",
                         Fcb);
      DokanXpEndCaching(Fcb);
      return TRUE;
    }
    return FALSE;
  }

  if (Fcb->XpCacheable) {
    if (Fcb->AdvancedFCBHeader.FileSize.QuadPart == info.FileSize &&
        Fcb->XpContentId == info.ContentId) {
      return FALSE;
    }
    DOKAN_LOG_FINE_IRP(RequestContext,
                       "XP cache: content changed, caching ends FCB=%p", Fcb);
    DokanXpEndCaching(Fcb);
    return TRUE;
  }

  // Not cached now: only a file without handles and sections can start, so
  // nothing of an earlier version (or of an uncached writer) is in memory and
  // the cache map is set up by a file object of this version.
  if (Fcb->UncleanCount != 0 || sop->DataSectionObject != NULL ||
      sop->SharedCacheMap != NULL || sop->ImageSectionObject != NULL) {
    return FALSE;
  }
  allocationSize =
      (info.FileSize + PAGE_SIZE - 1) & ~((LONGLONG)PAGE_SIZE - 1);
  InterlockedExchange64(&Fcb->AdvancedFCBHeader.AllocationSize.QuadPart,
                        allocationSize);
  InterlockedExchange64(&Fcb->AdvancedFCBHeader.FileSize.QuadPart,
                        info.FileSize);
  Fcb->XpContentId = info.ContentId;
  Fcb->XpEverCached = TRUE;
  Fcb->AdvancedFCBHeader.IsFastIoPossible = FastIoIsQuestionable;
  KeMemoryBarrier();
  Fcb->XpCacheable = TRUE;
  DOKAN_LOG_FINE_IRP(RequestContext, "XP cache: caching starts FCB=%p size %I64d",
                     Fcb, info.FileSize);
  return FALSE;
}

VOID DokanXpPurgeEndedCache(__in PDokanFCB Fcb) {
  PSECTION_OBJECT_POINTERS sop = &Fcb->SectionObjectPointers;
  // Nothing was written while the file was cacheable (a writer's open ends
  // it), so there is nothing to flush. Cached reads still running finish
  // first; the section pages of the old content go (a mapping made from now
  // on reads the new one), and so do the file objects' cache maps.
  if (sop->DataSectionObject != NULL || sop->SharedCacheMap != NULL) {
    DokanPagingIoLockRW(Fcb);
    CcPurgeCacheSection(sop, NULL, 0, /*UninitializeCacheMaps=*/TRUE);
    DokanPagingIoUnlock(Fcb);
  }
  if (sop->ImageSectionObject != NULL) {
    MmFlushImageSection(sop, MmFlushForWrite);
  }
}

// Called with the paging I/O resource held shared.
static VOID DokanXpInitCacheMap(__in PDokanDCB Dcb, __in PDokanFCB Fcb,
                                __in PFILE_OBJECT FileObject) {
  CC_FILE_SIZES sizes;
  if (FileObject->PrivateCacheMap != NULL) {
    return;
  }
  sizes.AllocationSize = Fcb->AdvancedFCBHeader.AllocationSize;
  sizes.FileSize = Fcb->AdvancedFCBHeader.FileSize;
  sizes.ValidDataLength = Fcb->AdvancedFCBHeader.FileSize;
  CcInitializeCacheMap(FileObject, &sizes, /*PinAccess=*/FALSE,
                       &Dcb->CacheManagerCallbacks, Fcb);
  CcSetReadAheadGranularity(FileObject, DOKAN_XP_READ_AHEAD_GRANULARITY);
}

NTSTATUS DokanXpCachedRead(__in PREQUEST_CONTEXT RequestContext,
                           __in PDokanFCB Fcb, __in LARGE_INTEGER ByteOffset,
                           __in ULONG Length) {
  PIRP irp = RequestContext->Irp;
  PFILE_OBJECT fileObject = RequestContext->IrpSp->FileObject;
  PVOID buffer = NULL;
  BOOLEAN wait = IoIsOperationSynchronous(irp);
  NTSTATUS status = STATUS_MORE_PROCESSING_REQUIRED;
  NTSTATUS code;
  LONGLONG fileSize;

  if (irp->MdlAddress != NULL) {
    buffer = MmGetSystemAddressForMdlNormalSafe(irp->MdlAddress);
    if (buffer == NULL) {
      return STATUS_MORE_PROCESSING_REQUIRED;
    }
  } else if (irp->RequestorMode == KernelMode ||
             irp->Tail.Overlay.Thread == PsGetCurrentThread()) {
    // Either a system address or the caller's own: this is its thread.
    buffer = irp->UserBuffer;
  } else {
    return STATUS_MORE_PROCESSING_REQUIRED;
  }

  // DokanBuildRequest entered the file system (APCs are off).
  if (!ExAcquireResourceSharedLite(&Fcb->PagingIoResource, wait)) {
    return STATUS_MORE_PROCESSING_REQUIRED;
  }
  __try {
    if (!Fcb->XpCacheable ||
        !FsRtlOplockIsFastIoPossible(DokanGetFcbOplock(Fcb))) {
      __leave;
    }
    if (!FsRtlCheckLockForReadAccess(&Fcb->FileLock, irp)) {
      status = STATUS_FILE_LOCK_CONFLICT;
      __leave;
    }
    fileSize = Fcb->AdvancedFCBHeader.FileSize.QuadPart;
    if (ByteOffset.QuadPart >= fileSize) {
      irp->IoStatus.Information = 0;
      status = STATUS_END_OF_FILE;
      __leave;
    }
    if ((LONGLONG)Length > fileSize - ByteOffset.QuadPart) {
      Length = (ULONG)(fileSize - ByteOffset.QuadPart);
    }
    DokanXpInitCacheMap(RequestContext->Dcb, Fcb, fileObject);
    if (!CcCopyRead(fileObject, &ByteOffset, Length, wait, buffer,
                    &irp->IoStatus)) {
      // An asynchronous read of data that is not in memory: user mode.
      irp->IoStatus.Information = 0;
      __leave;
    }
    status = irp->IoStatus.Status;
    if (NT_SUCCESS(status) && (fileObject->Flags & FO_SYNCHRONOUS_IO)) {
      fileObject->CurrentByteOffset.QuadPart =
          ByteOffset.QuadPart + irp->IoStatus.Information;
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    code = GetExceptionCode();
    irp->IoStatus.Information = 0;
    // A bad buffer of the caller fails the read; anything else (a failed
    // paging read, no memory for the cache map) goes to user mode as before,
    // so the caller gets the file system's own answer.
    status = (code == STATUS_ACCESS_VIOLATION ||
              code == STATUS_INVALID_USER_BUFFER)
                 ? STATUS_INVALID_USER_BUFFER
                 : STATUS_MORE_PROCESSING_REQUIRED;
  }
  ExReleaseResourceLite(&Fcb->PagingIoResource);
  return status;
}

BOOLEAN DokanXpFastIoRead(__in PFILE_OBJECT FileObject,
                          __in PLARGE_INTEGER FileOffset, __in ULONG Length,
                          __in BOOLEAN Wait, __in ULONG LockKey,
                          __out PVOID Buffer, __out PIO_STATUS_BLOCK IoStatus,
                          __in PDEVICE_OBJECT DeviceObject) {
  PDokanCCB ccb;
  PDokanFCB fcb;
  BOOLEAN done = FALSE;
  BOOLEAN setTopLevel = FALSE;
  LARGE_INTEGER offset;
  LARGE_INTEGER length;
  LONGLONG fileSize;
  UNREFERENCED_PARAMETER(DeviceObject);

  // Only file objects whose cache map this file built (cached files).
  if (FileObject == NULL || FileObject->PrivateCacheMap == NULL) {
    return FALSE;
  }
  ccb = FileObject->FsContext2;
  if (ccb == NULL || GetIdentifierType(ccb) != CCB) {
    return FALSE;
  }
  fcb = ccb->Fcb;
  if (fcb == NULL || !fcb->XpCacheable) {
    return FALSE;
  }
  if (Length == 0) {
    IoStatus->Status = STATUS_SUCCESS;
    IoStatus->Information = 0;
    return TRUE;
  }
  offset = *FileOffset;

  FsRtlEnterFileSystem();
  if (!ExAcquireResourceSharedLite(&fcb->PagingIoResource, Wait)) {
    FsRtlExitFileSystem();
    return FALSE;
  }
  __try {
    if (!fcb->XpCacheable || FileObject->PrivateCacheMap == NULL ||
        !FsRtlOplockIsFastIoPossible(DokanGetFcbOplock(fcb))) {
      __leave;
    }
    length.QuadPart = Length;
    if (!FsRtlFastCheckLockForRead(&fcb->FileLock, &offset, &length, LockKey,
                                   FileObject, PsGetCurrentProcess())) {
      __leave;
    }
    fileSize = fcb->AdvancedFCBHeader.FileSize.QuadPart;
    if (offset.QuadPart >= fileSize) {
      IoStatus->Status = STATUS_END_OF_FILE;
      IoStatus->Information = 0;
      done = TRUE;
      __leave;
    }
    if ((LONGLONG)Length > fileSize - offset.QuadPart) {
      Length = (ULONG)(fileSize - offset.QuadPart);
    }
    if (IoGetTopLevelIrp() == NULL) {
      IoSetTopLevelIrp((PIRP)FSRTL_FAST_IO_TOP_LEVEL_IRP);
      setTopLevel = TRUE;
    }
    __try {
      done = CcCopyRead(FileObject, &offset, Length, Wait, Buffer, IoStatus);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      // The IRP path tries again and reports the error.
      done = FALSE;
    }
    if (setTopLevel) {
      IoSetTopLevelIrp(NULL);
    }
    if (done && NT_SUCCESS(IoStatus->Status) &&
        (FileObject->Flags & FO_SYNCHRONOUS_IO)) {
      FileObject->CurrentByteOffset.QuadPart =
          offset.QuadPart + IoStatus->Information;
    }
  } __finally {
    ExReleaseResourceLite(&fcb->PagingIoResource);
    FsRtlExitFileSystem();
  }
  return done;
}

BOOLEAN DokanXpFastIoCheckIfPossible(__in PFILE_OBJECT FileObject,
                                     __in PLARGE_INTEGER FileOffset,
                                     __in ULONG Length, __in BOOLEAN Wait,
                                     __in ULONG LockKey,
                                     __in BOOLEAN CheckForReadOperation) {
  PDokanCCB ccb;
  PDokanFCB fcb;
  LARGE_INTEGER length;
  UNREFERENCED_PARAMETER(Wait);

  if (!CheckForReadOperation || FileObject == NULL ||
      FileObject->PrivateCacheMap == NULL) {
    return FALSE;
  }
  ccb = FileObject->FsContext2;
  if (ccb == NULL || GetIdentifierType(ccb) != CCB) {
    return FALSE;
  }
  fcb = ccb->Fcb;
  if (fcb == NULL || !fcb->XpCacheable ||
      !FsRtlOplockIsFastIoPossible(DokanGetFcbOplock(fcb))) {
    return FALSE;
  }
  length.QuadPart = Length;
  return FsRtlFastCheckLockForRead(&fcb->FileLock, FileOffset, &length,
                                   LockKey, FileObject, PsGetCurrentProcess());
}

BOOLEAN DokanXpFastIoQueryStandardInfo(__in PFILE_OBJECT FileObject,
                                       __in BOOLEAN Wait,
                                       __out PFILE_STANDARD_INFORMATION Buffer,
                                       __out PIO_STATUS_BLOCK IoStatus,
                                       __in PDEVICE_OBJECT DeviceObject) {
  PDokanCCB ccb;
  PDokanFCB fcb;
  UNREFERENCED_PARAMETER(Wait);
  UNREFERENCED_PARAMETER(DeviceObject);

  if (!DOKAN_XP_DEFER || FileObject == NULL) {
    return FALSE;
  }
  ccb = FileObject->FsContext2;
  if (ccb == NULL || GetIdentifierType(ccb) != CCB) {
    return FALSE;
  }
  fcb = ccb->Fcb;
  // The sizes of a cacheable file are the ones the file system vouched for
  // (DokanXpCacheOnCreate); any other file asks user mode as before.
  if (fcb == NULL || !fcb->XpCacheable ||
      DokanFCBFlagsIsSet(fcb, DOKAN_FILE_DIRECTORY)) {
    return FALSE;
  }
  Buffer->AllocationSize.QuadPart =
      fcb->AdvancedFCBHeader.AllocationSize.QuadPart;
  Buffer->EndOfFile.QuadPart = fcb->AdvancedFCBHeader.FileSize.QuadPart;
  Buffer->NumberOfLinks = 1;
  Buffer->DeletePending = DokanFCBIsPendingDeletion(fcb);
  Buffer->Directory = FALSE;
  IoStatus->Status = STATUS_SUCCESS;
  IoStatus->Information = sizeof(FILE_STANDARD_INFORMATION);
  return TRUE;
}

BOOLEAN DokanXpCleanupCache(__in PREQUEST_CONTEXT RequestContext,
                            __in PDokanFCB Fcb,
                            __in PFILE_OBJECT FileObject) {
  if (!RequestContext->Dcb->XpReadCache) {
    return FALSE;
  }
  if (FileObject->PrivateCacheMap != NULL) {
    // Wait for cached reads of this file object still running (they hold the
    // paging I/O resource shared), then end its cache map.
    DokanPagingIoLockRW(Fcb);
    DokanPagingIoUnlock(Fcb);
    CcUninitializeCacheMap(FileObject, NULL, NULL);
  }
  return Fcb->XpCacheable;
}

// Ends caching of a file and drops its cached data and sections that no
// process maps. No lock may be held (the flush can send paging writes to user
// mode). Returns TRUE when the cache manager let go of the data.
static BOOLEAN DokanXpPurgeFcb(__in PDokanFCB Fcb, __in BOOLEAN Flush) {
  PSECTION_OBJECT_POINTERS sop = &Fcb->SectionObjectPointers;
  BOOLEAN purged = TRUE;
  IO_STATUS_BLOCK iosb;

  // Written data of a writer that mapped the file after caching ended.
  if (Flush && sop->DataSectionObject != NULL) {
    CcFlushCache(sop, NULL, 0, &iosb);
  }
  DokanPagingIoLockRW(Fcb);
  DokanFCBLockRW(Fcb);
  DokanXpEndCaching(Fcb);
  DokanFCBUnlock(Fcb);
  if (sop->DataSectionObject != NULL || sop->SharedCacheMap != NULL) {
    purged = CcPurgeCacheSection(sop, NULL, 0, /*UninitializeCacheMaps=*/TRUE);
  }
  DokanPagingIoUnlock(Fcb);
  if (sop->ImageSectionObject != NULL) {
    MmFlushImageSection(sop, MmFlushForWrite);
  }
  // Sections still in use (mapped, or the cache manager's until its lazy
  // close) go away once they are not.
  MmForceSectionClosed(sop, /*DelayClose=*/TRUE);
  return purged;
}

VOID DokanXpForgetDeletedFcb(__in PREQUEST_CONTEXT RequestContext,
                             __in PDokanFCB Fcb) {
  PDokanVCB vcb = RequestContext->Vcb;
  BOOLEAN forgot = FALSE;

  if (RequestContext->Dcb == NULL || !RequestContext->Dcb->XpReadCache ||
      !Fcb->XpEverCached || Fcb->UncleanCount != 0 ||
      !DokanFCBIsPendingDeletion(Fcb) || Fcb->ReplacedByRename) {
    return;
  }
  DokanVCBLockRW(vcb);
  DokanFCBLockRW(Fcb);
  if (Fcb->UncleanCount == 0 && DokanFCBIsPendingDeletion(Fcb) &&
      !Fcb->ReplacedByRename && Fcb->NextGarbageCollectableFcb.Flink == NULL &&
      RtlDeleteElementGenericTableAvl(&vcb->FcbTable, &Fcb)) {
    // Freed without a table removal by its last DokanFreeFCB (the renamed-over
    // FCB of DokanRenameFcb takes the same way).
    Fcb->ReplacedByRename = TRUE;
    DokanXpEndCaching(Fcb);
    // Keeps the FCB for the purge; DokanXpPurgeForgottenLater gives it back.
    InterlockedIncrement(&Fcb->OpenCount);
    forgot = TRUE;
  }
  DokanFCBUnlock(Fcb);
  DokanVCBUnlock(vcb);
  if (!forgot) {
    return;
  }
  DOKAN_LOG_FINE_IRP(RequestContext, "XP cache: deleted FCB=%p left the table",
                     Fcb);
  // In a worker thread: this is a pull thread, and the purge waits for the
  // paging I/O resource.
  DokanXpPurgeForgottenLater(vcb, Fcb);
}

VOID DokanXpPurgeForgotten(__in PDokanFCB Fcb) {
  PSECTION_OBJECT_POINTERS sop = &Fcb->SectionObjectPointers;
  // Nobody can open this file again: what the cache manager and Mm keep of it
  // goes, and with it their file objects (the FCB's last references).
  if (sop->DataSectionObject != NULL || sop->SharedCacheMap != NULL) {
    DokanPagingIoLockRW(Fcb);
    CcPurgeCacheSection(sop, NULL, 0, /*UninitializeCacheMaps=*/TRUE);
    DokanPagingIoUnlock(Fcb);
  }
  MmForceSectionClosed(sop, /*DelayClose=*/TRUE);
}

static BOOLEAN DokanXpPathMatches(__in PDokanFCB Fcb,
                                  __in_opt PUNICODE_STRING Path,
                                  __in BOOLEAN IgnoreCase) {
  if (Path == NULL || Path->Length == 0) {
    return TRUE;
  }
  if (Path->Buffer[Path->Length / sizeof(WCHAR) - 1] == L'\\') {
    return RtlPrefixUnicodeString(Path, &Fcb->FileName, IgnoreCase);
  }
  return RtlEqualUnicodeString(Path, &Fcb->FileName, IgnoreCase);
}

// Purges every file of the volume that was cacheable and matches Path. The
// FCBs are referenced (OpenCount) while the VCB is locked, then purged without
// locks; the purge can close file objects of the cache manager.
static ULONG DokanXpPurgeMatching(__in PDokanVCB Vcb,
                                  __in_opt PUNICODE_STRING Path,
                                  __in BOOLEAN Flush) {
  PDokanFCB *fcbInTable;
  PDokanFCB fcb;
  PDokanFCB *list = NULL;
  ULONG count = 0;
  ULONG capacity = 0;
  ULONG purged = 0;
  ULONG i;
  BOOLEAN ignoreCase =
      !(Vcb->Dcb->MountOptions & DOKAN_EVENT_CASE_SENSITIVE);

  DokanVCBLockRW(Vcb);
  for (fcbInTable = (PDokanFCB *)RtlEnumerateGenericTableAvl(&Vcb->FcbTable,
                                                             /*Restart=*/TRUE);
       fcbInTable != NULL;
       fcbInTable = (PDokanFCB *)RtlEnumerateGenericTableAvl(
           &Vcb->FcbTable, /*Restart=*/FALSE)) {
    fcb = *fcbInTable;
    if (fcb->XpEverCached && fcb->OpenCount > 0 &&
        DokanXpPathMatches(fcb, Path, ignoreCase)) {
      ++capacity;
    }
  }
  if (capacity > 0) {
    list = ExAllocatePoolWithTag(NonPagedPool, capacity * sizeof(PDokanFCB),
                                 TAG);
  }
  if (list != NULL) {
    for (fcbInTable = (PDokanFCB *)RtlEnumerateGenericTableAvl(
             &Vcb->FcbTable, /*Restart=*/TRUE);
         fcbInTable != NULL && count < capacity;
         fcbInTable = (PDokanFCB *)RtlEnumerateGenericTableAvl(
             &Vcb->FcbTable, /*Restart=*/FALSE)) {
      fcb = *fcbInTable;
      if (fcb->XpEverCached && fcb->OpenCount > 0 &&
          DokanXpPathMatches(fcb, Path, ignoreCase)) {
        // Keeps the FCB while the purge closes file objects; DokanFreeFCB
        // below gives it back (and frees the FCB if that was the last one).
        InterlockedIncrement(&fcb->OpenCount);
        list[count++] = fcb;
      }
    }
  }
  DokanVCBUnlock(Vcb);

  for (i = 0; i < count; ++i) {
    if (DokanXpPurgeFcb(list[i], Flush)) {
      ++purged;
    }
    DokanFreeFCB(Vcb, list[i]);
  }
  if (list != NULL) {
    ExFreePool(list);
  }
  return purged;
}

NTSTATUS DokanXpPurgeCacheFsctl(__in PREQUEST_CONTEXT RequestContext) {
  PDokanVCB vcb = RequestContext->Vcb;
  ULONG inputLength =
      RequestContext->IrpSp->Parameters.FileSystemControl.InputBufferLength;
  ULONG outputLength =
      RequestContext->IrpSp->Parameters.FileSystemControl.OutputBufferLength;
  PVOID systemBuffer = RequestContext->Irp->AssociatedIrp.SystemBuffer;
  UNICODE_STRING path;
  ULONG purged;

  if (vcb == NULL || GetIdentifierType(vcb) != VCB ||
      IsUnmountPendingVcb(vcb)) {
    return STATUS_NO_SUCH_DEVICE;
  }
  if (!RequestContext->Dcb->XpReadCache) {
    return STATUS_INVALID_DEVICE_REQUEST;
  }
  if (inputLength % sizeof(WCHAR) != 0 || inputLength > MAXUSHORT - 1 ||
      (inputLength > 0 && systemBuffer == NULL)) {
    return STATUS_INVALID_PARAMETER;
  }
  path.Buffer = (PWCH)systemBuffer;
  path.Length = (USHORT)inputLength;
  path.MaximumLength = (USHORT)inputLength;
  purged = DokanXpPurgeMatching(vcb, inputLength > 0 ? &path : NULL,
                                /*Flush=*/TRUE);
  DOKAN_LOG_FINE_IRP(RequestContext, "XP cache: purged %lu files", purged);
  // METHOD_BUFFERED: the answer overwrites the path, which is no longer used.
  if (outputLength >= sizeof(ULONG) && systemBuffer != NULL) {
    *(PULONG)systemBuffer = purged;
    RequestContext->Irp->IoStatus.Information = sizeof(ULONG);
  }
  return STATUS_SUCCESS;
}

VOID DokanXpPurgeAllForUnmount(__in PDokanVCB Vcb) {
  if (!Vcb->Dcb->XpReadCache) {
    return;
  }
  // User mode no longer serves paging writes: nothing to flush.
  DokanXpPurgeMatching(Vcb, NULL, /*Flush=*/FALSE);
}
