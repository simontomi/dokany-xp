/*
  Dokan : user-mode file system library for Windows

  XP compatibility for the kernel driver: the routines of xp_compat.h that
  need the driver's own types. Only the XP build (xp\build.cmd) compiles this
  file.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation; either version 3 of the License, or (at your option) any
  later version.
*/

#include "dokan.h"

#if (NTDDI_VERSION < NTDDI_VISTA)

// FsRtlNotifyCleanupAll (Vista): completes the pending change notifications
// of every handle of the volume, which is FsRtlNotifyCleanup for every CCB
// (the CCB is the FsContext of FsRtlNotifyFullChangeDirectory, directory.c).
// The only caller, DokanCleanupAllChangeNotificationWaiters, holds the VCB
// exclusively and passes &Vcb->DirNotifyList, which leads back to the VCB.
VOID DokanXpNotifyCleanupAll(__in PNOTIFY_SYNC NotifySync,
                             __in PLIST_ENTRY NotifyList) {
  PDokanVCB vcb = CONTAINING_RECORD(NotifyList, DokanVCB, DirNotifyList);
  PDokanFCB *fcbInTable;
  PDokanFCB fcb;
  PLIST_ENTRY entry;

  ASSERT(vcb->Identifier.Type == VCB);
  for (fcbInTable = (PDokanFCB *)RtlEnumerateGenericTableAvl(&vcb->FcbTable,
                                                             /*Restart=*/TRUE);
       fcbInTable != NULL;
       fcbInTable = (PDokanFCB *)RtlEnumerateGenericTableAvl(
           &vcb->FcbTable, /*Restart=*/FALSE)) {
    fcb = *fcbInTable;
    DokanFCBLockRO(fcb);
    for (entry = fcb->NextCCB.Flink; entry != &fcb->NextCCB;
         entry = entry->Flink) {
      FsRtlNotifyCleanup(NotifySync, NotifyList,
                         CONTAINING_RECORD(entry, DokanCCB, NextCCB));
    }
    DokanFCBUnlock(fcb);
  }
}

// The pull IOCTL's wait for events (fscontrol.c, KeRemoveQueueEx on Vista).
// At unmount the driver runs the queue down (KeRundownQueue): from Vista on
// that wakes the threads waiting on it (STATUS_ABANDONED), on XP they keep
// waiting for ever and the file system process never sees the unmount (the
// same XP difference that GetQueuedCompletionStatus documents for a closed
// completion port). So wait in slices and stop once the volume unmounts.
// Timeout is absolute (system time) or NULL; the result is the queue entry or
// a status cast to a pointer, as KeRemoveQueue returns it.
#define DOKAN_XP_QUEUE_WAIT_SLICE (500 * 10000LL)  // 500 ms in 100 ns units

PLIST_ENTRY DokanXpRemoveQueueUntilUnmount(__in PRKQUEUE Queue,
                                           __in_opt PLARGE_INTEGER Timeout,
                                           __in PVOID Vcb) {
  PDokanVCB vcb = (PDokanVCB)Vcb;
  LARGE_INTEGER slice;
  LARGE_INTEGER now;
  PLIST_ENTRY entry;

  for (;;) {
    slice.QuadPart = -DOKAN_XP_QUEUE_WAIT_SLICE;
    if (Timeout) {
      KeQuerySystemTime(&now);
      if (now.QuadPart >= Timeout->QuadPart) {
        return (PLIST_ENTRY)(ULONG_PTR)STATUS_TIMEOUT;
      }
      if (Timeout->QuadPart - now.QuadPart < DOKAN_XP_QUEUE_WAIT_SLICE) {
        slice.QuadPart = -(Timeout->QuadPart - now.QuadPart);
      }
    }
    entry = KeRemoveQueue(Queue, KernelMode, &slice);
    if (entry != (PLIST_ENTRY)(ULONG_PTR)STATUS_TIMEOUT) {
      return entry;
    }
    if (IsUnmountPendingVcb(vcb)) {
      return (PLIST_ENTRY)(ULONG_PTR)STATUS_ABANDONED;
    }
  }
}

#endif // NTDDI_VERSION < NTDDI_VISTA
