/*
  Dokan : user-mode file system library for Windows

  XP port: the requests whose completion would wait for a busy FCB lock (or
  for the paging I/O resource) are completed by a system worker thread
  instead of the thread that delivered them: a pull thread of the file
  system, or the timeout thread.

  Why: a thread can hold the FCB resource while it waits for user mode.
  Section creation does: AcquireFileForNtCreateSection holds it exclusively
  while the memory manager asks for the file size (an IRP_MJ_QUERY_INFORMATION)
  or reads an image header (a paging read). The completions of creates and
  cleanups (and FileAllInformation queries) take the same resource; with
  every pull thread waiting there, the request that would release it is never
  pulled. The timeout thread ends that after 15 s by failing the waiting
  request, unless it waits as well: it cancels a timed-out create through
  DokanCompleteCreate, and then the volume stalls for good. Seen on XP (T43p,
  2026-11-18): fc /b maps the file it compares; 6 parallel readers of one
  file (with the kernel read cache they loop fast) stalled in the first
  minute, and before that an fc failed now and then with "cannot open" (its
  mapping timed out).

  The purges that end a file's caching wait for the paging I/O resource,
  which a cached read holds while its page faults go to user mode: they run in
  a worker thread as well, so no pull thread ever waits for that resource.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation; either version 3 of the License, or (at your option) any
  later version.
*/

#include "dokan.h"
#include "util/fcb.h"

typedef enum _DOKAN_XP_DEFERRED_KIND {
  // DokanDispatchCompletion of a reply, then the IRP entry is freed.
  DokanXpDeferredCompletion,
  // DokanCancelCreateIrp of a timed-out create, then the IRP entry is freed.
  DokanXpDeferredCancelCreate,
  // DokanExecuteCleanup of a timed-out cleanup, the IRP fails, the IRP entry
  // is freed.
  DokanXpDeferredTimedOutCleanup,
  // DokanXpPurgeEndedCache, then the create IRP completes.
  DokanXpDeferredPurgeThenComplete,
  // DokanXpPurgeForgotten, then the FCB reference is given back.
  DokanXpDeferredForgottenPurge
} DOKAN_XP_DEFERRED_KIND;

typedef struct _DOKAN_XP_DEFERRED {
  PIO_WORKITEM WorkItem;
  DOKAN_XP_DEFERRED_KIND Kind;
  PDEVICE_OBJECT DeviceObject;
  PIRP_ENTRY IrpEntry;
  PIRP Irp;
  PDokanFCB Fcb;
  PDokanVCB Vcb;
  NTSTATUS Status;
  // The reply of a deferred completion (EventInfoLength bytes).
  ULONG EventInfoLength;
  EVENT_INFORMATION EventInfo;
} DOKAN_XP_DEFERRED, *PDOKAN_XP_DEFERRED;

static VOID DokanXpDeferredWorker(__in PDEVICE_OBJECT DeviceObject,
                                  __in_opt PVOID Context) {
  PDOKAN_XP_DEFERRED deferred = (PDOKAN_XP_DEFERRED)Context;
  PIO_WORKITEM workItem;
  PIRP irp;
  BOOLEAN setTopLevel = FALSE;
  UNREFERENCED_PARAMETER(DeviceObject);

  ASSERT(deferred != NULL);
  workItem = deferred->WorkItem;
  FsRtlEnterFileSystem();
  if (IoGetTopLevelIrp() == NULL) {
    // A file system worker, as the FSP threads of FAT and NTFS.
    IoSetTopLevelIrp((PIRP)FSRTL_FSP_TOP_LEVEL_IRP);
    setTopLevel = TRUE;
  }
  switch (deferred->Kind) {
    case DokanXpDeferredCompletion:
      DokanDispatchCompletion(deferred->DeviceObject, deferred->IrpEntry,
                              &deferred->EventInfo);
      DokanFreeIrpEntry(deferred->IrpEntry);
      break;
    case DokanXpDeferredCancelCreate:
      DokanCancelCreateIrp(&deferred->IrpEntry->RequestContext,
                           deferred->Status);
      DokanFreeIrpEntry(deferred->IrpEntry);
      break;
    case DokanXpDeferredTimedOutCleanup:
      irp = deferred->IrpEntry->RequestContext.Irp;
      DokanExecuteCleanup(&deferred->IrpEntry->RequestContext);
      irp->IoStatus.Information = 0;
      DokanCompleteIrpRequest(irp, STATUS_INSUFFICIENT_RESOURCES);
      DokanFreeIrpEntry(deferred->IrpEntry);
      break;
    case DokanXpDeferredPurgeThenComplete:
      DokanXpPurgeEndedCache(deferred->Fcb);
      DokanCompleteIrpRequest(deferred->Irp, deferred->Irp->IoStatus.Status);
      break;
    case DokanXpDeferredForgottenPurge:
      DokanXpPurgeForgotten(deferred->Fcb);
      DokanFreeFCB(deferred->Vcb, deferred->Fcb);
      break;
  }
  if (setTopLevel) {
    IoSetTopLevelIrp(NULL);
  }
  FsRtlExitFileSystem();
  ExFreePool(deferred);
  IoFreeWorkItem(workItem);
}

// A work item for DeviceObject (one of this driver's devices: the queued work
// keeps it), with room for a reply of EventInfoLength bytes. NULL when there
// is no memory: the caller then does the work itself, as before.
static PDOKAN_XP_DEFERRED DokanXpAllocateDeferred(
    __in PDEVICE_OBJECT DeviceObject, __in DOKAN_XP_DEFERRED_KIND Kind,
    __in ULONG EventInfoLength) {
  PDOKAN_XP_DEFERRED deferred;
  ULONG size = FIELD_OFFSET(DOKAN_XP_DEFERRED, EventInfo) +
               max(EventInfoLength, (ULONG)sizeof(EVENT_INFORMATION));

  deferred = ExAllocatePoolWithTag(NonPagedPool, size, TAG);
  if (deferred == NULL) {
    return NULL;
  }
  RtlZeroMemory(deferred, size);
  deferred->WorkItem = IoAllocateWorkItem(DeviceObject);
  if (deferred->WorkItem == NULL) {
    ExFreePool(deferred);
    return NULL;
  }
  deferred->Kind = Kind;
  deferred->DeviceObject = DeviceObject;
  deferred->EventInfoLength = EventInfoLength;
  return deferred;
}

static VOID DokanXpQueueDeferred(__in PDOKAN_XP_DEFERRED Deferred) {
  IoQueueWorkItem(Deferred->WorkItem, DokanXpDeferredWorker, DelayedWorkQueue,
                  Deferred);
}

// The FCB whose resource the completion of this request takes, or NULL when
// it takes none (or none that a waiting thread can hold).
static PDokanFCB DokanXpCompletionFcb(__in PIRP_ENTRY IrpEntry) {
  PIO_STACK_LOCATION irpSp = IrpEntry->RequestContext.IrpSp;
  PFILE_OBJECT fileObject;
  PDokanCCB ccb;

  switch (irpSp->MajorFunction) {
    case IRP_MJ_CREATE:
    case IRP_MJ_CLEANUP:
    case IRP_MJ_SET_INFORMATION:
      break;
    case IRP_MJ_QUERY_INFORMATION:
      // Only this class locks the FCB (the name); a size query, as the
      // memory manager's, completes without a lock.
      if (irpSp->Parameters.QueryFile.FileInformationClass !=
          FileAllInformation) {
        return NULL;
      }
      break;
    default:
      return NULL;
  }
  fileObject = irpSp->FileObject;
  if (fileObject == NULL) {
    return NULL;
  }
  ccb = fileObject->FsContext2;
  if (ccb == NULL || GetIdentifierType(ccb) != CCB) {
    return NULL;
  }
  return ccb->Fcb;
}

// TRUE when another thread owns the FCB resource now. (It can be taken right
// after this answered FALSE; then the completion waits as before, for the
// short time an ordinary lock holder keeps it.)
static BOOLEAN DokanXpFcbBusy(__in PDokanFCB Fcb) {
  BOOLEAN acquired;
  KeEnterCriticalRegion();
  acquired =
      ExAcquireResourceExclusiveLite(Fcb->AdvancedFCBHeader.Resource, FALSE);
  if (acquired) {
    ExReleaseResourceLite(Fcb->AdvancedFCBHeader.Resource);
  }
  KeLeaveCriticalRegion();
  return !acquired;
}

BOOLEAN DokanXpDeferCompletionIfBusy(__in PIRP_ENTRY IrpEntry,
                                     __in PEVENT_INFORMATION EventInfo,
                                     __in ULONG EventInfoLength) {
  PDokanFCB fcb;
  PDOKAN_XP_DEFERRED deferred;

  // A buffer overflow reply may be read past its length; it stays here.
  if (!DOKAN_XP_DEFER || EventInfo->Status == STATUS_BUFFER_OVERFLOW ||
      EventInfoLength < sizeof(EVENT_INFORMATION)) {
    return FALSE;
  }
  fcb = DokanXpCompletionFcb(IrpEntry);
  if (fcb == NULL || !DokanXpFcbBusy(fcb)) {
    return FALSE;
  }
  deferred = DokanXpAllocateDeferred(IrpEntry->RequestContext.DeviceObject,
                                     DokanXpDeferredCompletion,
                                     EventInfoLength);
  if (deferred == NULL) {
    return FALSE;
  }
  deferred->IrpEntry = IrpEntry;
  RtlCopyMemory(&deferred->EventInfo, EventInfo, EventInfoLength);
  DOKAN_LOG_FINE_IRP((&IrpEntry->RequestContext),
                     "XP: FCB=%p busy, completion goes to a worker", fcb);
  DokanXpQueueDeferred(deferred);
  return TRUE;
}

BOOLEAN DokanXpDeferTimedOutIfBusy(__in PIRP_ENTRY IrpEntry,
                                   __in NTSTATUS CreateStatus) {
  UCHAR major = IrpEntry->RequestContext.IrpSp->MajorFunction;
  PDokanFCB fcb;
  PDOKAN_XP_DEFERRED deferred;

  if (!DOKAN_XP_DEFER ||
      (major != IRP_MJ_CREATE && major != IRP_MJ_CLEANUP)) {
    return FALSE;
  }
  fcb = DokanXpCompletionFcb(IrpEntry);
  if (fcb == NULL || !DokanXpFcbBusy(fcb)) {
    return FALSE;
  }
  deferred = DokanXpAllocateDeferred(
      IrpEntry->RequestContext.DeviceObject,
      major == IRP_MJ_CREATE ? DokanXpDeferredCancelCreate
                             : DokanXpDeferredTimedOutCleanup,
      0);
  if (deferred == NULL) {
    return FALSE;
  }
  deferred->IrpEntry = IrpEntry;
  deferred->Status = CreateStatus;
  DokanXpQueueDeferred(deferred);
  return TRUE;
}

VOID DokanXpPurgeEndedCacheThenComplete(
    __in PREQUEST_CONTEXT RequestContext, __in PDokanFCB Fcb) {
  PDOKAN_XP_DEFERRED deferred = NULL;
  if (DOKAN_XP_DEFER) {
    deferred = DokanXpAllocateDeferred(RequestContext->DeviceObject,
                                       DokanXpDeferredPurgeThenComplete, 0);
  }
  if (deferred == NULL) {
    DokanXpPurgeEndedCache(Fcb);
    return;
  }
  // The open holds the FCB (its CCB) and cannot close before its create
  // completes, which the worker does after the purge.
  deferred->Irp = RequestContext->Irp;
  deferred->Fcb = Fcb;
  RequestContext->DoNotComplete = TRUE;
  DokanXpQueueDeferred(deferred);
}

VOID DokanXpPurgeForgottenLater(__in PDokanVCB Vcb, __in PDokanFCB Fcb) {
  PDOKAN_XP_DEFERRED deferred = NULL;
  if (DOKAN_XP_DEFER) {
    deferred = DokanXpAllocateDeferred(Vcb->DeviceObject,
                                       DokanXpDeferredForgottenPurge, 0);
  }
  if (deferred == NULL) {
    DokanXpPurgeForgotten(Fcb);
    DokanFreeFCB(Vcb, Fcb);
    return;
  }
  deferred->Vcb = Vcb;
  deferred->Fcb = Fcb;
  DokanXpQueueDeferred(deferred);
}
