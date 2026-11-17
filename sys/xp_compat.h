/*
  Dokan : user-mode file system library for Windows

  XP compatibility for the kernel driver: what the WDK 7.1 headers
  (Windows XP x86 and Windows Vista x64 targets) do not have, and replacements
  for the kernel routines that Windows XP does not export.

  The XP build (xp\build.cmd) force-includes this header in every driver source
  file (/FI). The Visual Studio build never includes it.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation; either version 3 of the License, or (at your option) any
  later version.
*/

#ifndef DOKAN_XP_COMPAT_H_
#define DOKAN_XP_COMPAT_H_

#include <ntifs.h>
// The headers whose missing names this file adds, first: #ifndef only works
// once they are in.
#include <ntdddisk.h>
#include <ntddstor.h>
#include <ntddvol.h>
#include <mountmgr.h>
#include <mountdev.h>

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
#ifndef _Dispatch_type_
#define _Dispatch_type_(type)
#endif
#ifndef _Function_class_
#define _Function_class_(name)
#endif
#ifndef _IRQL_requires_max_
#define _IRQL_requires_max_(irql)
#endif
#ifndef _IRQL_requires_
#define _IRQL_requires_(irql)
#endif
#ifndef _IRQL_requires_same_
#define _IRQL_requires_same_
#endif
#ifndef _IRQL_uses_cancel_
#define _IRQL_uses_cancel_
#endif
#ifndef _IRQL_raises_
#define _IRQL_raises_(irql)
#endif
#ifndef _IRQL_saves_
#define _IRQL_saves_
#endif
#ifndef _IRQL_restores_
#define _IRQL_restores_
#endif
#ifndef _Acquires_shared_lock_
#define _Acquires_shared_lock_(lock)
#endif
#ifndef _Acquires_exclusive_lock_
#define _Acquires_exclusive_lock_(lock)
#endif
#ifndef _Acquires_lock_
#define _Acquires_lock_(lock)
#endif
#ifndef _Releases_lock_
#define _Releases_lock_(lock)
#endif
#ifndef _Releases_shared_lock_
#define _Releases_shared_lock_(lock)
#endif
#ifndef _Releases_exclusive_lock_
#define _Releases_exclusive_lock_(lock)
#endif
#ifndef _Requires_lock_held_
#define _Requires_lock_held_(lock)
#endif
#ifndef _Use_decl_annotations_
#define _Use_decl_annotations_
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
#ifndef _Analysis_assume_
#define _Analysis_assume_(expr)
#endif

// ---------------------------------------------------------------------------
// C language: the WDK 7.1 C compiler (cl 15.00) has no C99 'inline'.
// ---------------------------------------------------------------------------
#ifndef __cplusplus
#define inline __inline
#endif

// ---------------------------------------------------------------------------
// Constants and types the WDK 7.1 headers lack (values from the Windows 10
// SDK). An undefined name in #if counts as 0, so every NTDDI_ name the code
// compares with must exist.
// ---------------------------------------------------------------------------
#ifndef NTDDI_WIN8
#define NTDDI_WIN8 0x06020000
#endif
#ifndef NTDDI_WINBLUE
#define NTDDI_WINBLUE 0x06030000
#endif
#ifndef NTDDI_WIN10
#define NTDDI_WIN10 0x0A000000
#endif
#ifndef NTDDI_WIN10_RS1
#define NTDDI_WIN10_RS1 0x0A000002
#endif
#ifndef NTDDI_WIN10_RS4
#define NTDDI_WIN10_RS4 0x0A000005
#endif

#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// The volume devices are named from a base GUID XOR'd with the driver's own
// mount counter. Dokan 0.6.0, which may stay installed on XP and Vista, uses the same
// base GUID as Dokany ({D6CC17C5-1734-4085-BCE7-964F1E9F5DE9}) and its counter
// also starts at 0, so with both drivers loaded the first volume of one
// collided with a volume of the other (IoCreateDeviceSecure failed with
// STATUS_OBJECT_NAME_COLLISION: the first mount after the load failed).
// The XP build has a base GUID of its own: {6B588B54-627F-4272-8841-D2C36946B13A}.
#define DOKAN_BASE_GUID                                                        \
  {                                                                            \
    0x6b588b54, 0x627f, 0x4272, {                                              \
      0x88, 0x41, 0xd2, 0xc3, 0x69, 0x46, 0xb1, 0x3a                           \
    }                                                                          \
  }

// public.h takes BOOL from <minwindef.h> (Windows 8 SDK), which the WDK 7.1
// does not have.
#ifndef _MINWINDEF_
typedef int BOOL;
#endif

// The WDK raises C4242 (possible loss of data) to level 3 in its forced
// warning.h; the upstream build does not report it and the code relies on
// those conversions.
#pragma warning(disable : 4242)

// NTSTATUS codes of later Windows versions (mostly for the log tables).
#include "xp_ntstatus.h"
// IOCTL and FSCTL codes of later Windows versions (for the log table).
#include "xp_ioctl.h"

// FILE_INFORMATION_CLASS and FS_INFORMATION_CLASS values of later Windows
// versions (Windows 10 WDK). The I/O manager of XP and Vista refuses them
// before they reach a file system; the driver only names them in switch
// statements.
#define FileUnusedInformation ((FILE_INFORMATION_CLASS)52)
#define FileRenameInformationBypassAccessCheck ((FILE_INFORMATION_CLASS)56)
#define FileLinkInformationBypassAccessCheck ((FILE_INFORMATION_CLASS)57)
#define FileVolumeNameInformation ((FILE_INFORMATION_CLASS)58)
#define FileIdInformation ((FILE_INFORMATION_CLASS)59)
#define FileIdExtdDirectoryInformation ((FILE_INFORMATION_CLASS)60)
#define FileReplaceCompletionInformation ((FILE_INFORMATION_CLASS)61)
#define FileHardLinkFullIdInformation ((FILE_INFORMATION_CLASS)62)
#define FileIdExtdBothDirectoryInformation ((FILE_INFORMATION_CLASS)63)
#define FileDispositionInformationEx ((FILE_INFORMATION_CLASS)64)
#define FileRenameInformationEx ((FILE_INFORMATION_CLASS)65)
#define FileRenameInformationExBypassAccessCheck ((FILE_INFORMATION_CLASS)66)
#define FileDesiredStorageClassInformation ((FILE_INFORMATION_CLASS)67)
#define FileStatInformation ((FILE_INFORMATION_CLASS)68)
#define FileMemoryPartitionInformation ((FILE_INFORMATION_CLASS)69)
#define FileStatLxInformation ((FILE_INFORMATION_CLASS)70)
#define FileCaseSensitiveInformation ((FILE_INFORMATION_CLASS)71)
#define FileLinkInformationEx ((FILE_INFORMATION_CLASS)72)
#define FileLinkInformationExBypassAccessCheck ((FILE_INFORMATION_CLASS)73)
#define FileStorageReserveIdInformation ((FILE_INFORMATION_CLASS)74)
#define FileCaseSensitiveInformationForceAccessCheck ((FILE_INFORMATION_CLASS)75)

#define FileFsSectorSizeInformation ((FS_INFORMATION_CLASS)11)
#define FileFsDataCopyInformation ((FS_INFORMATION_CLASS)12)
#define FileFsMetadataSizeInformation ((FS_INFORMATION_CLASS)13)
#define FileFsFullSizeInformationEx ((FS_INFORMATION_CLASS)14)

#ifndef IRP_MN_NOTIFY_CHANGE_DIRECTORY_EX
#define IRP_MN_NOTIFY_CHANGE_DIRECTORY_EX 0x03
#endif
#ifndef IRP_MN_DEVICE_ENUMERATED
#define IRP_MN_DEVICE_ENUMERATED 0x19
#endif
#ifndef DBG_PRINTEXCEPTION_WIDE_C
#define DBG_PRINTEXCEPTION_WIDE_C ((NTSTATUS)0x4001000AL)
#endif

// IOCTLs of later Windows versions that the driver only names in its log
// table (values from the Windows 10 SDK and the WDK 7.1 mountmgr.h).
#ifndef IOCTL_DISK_REASSIGN_BLOCKS_EX
#define IOCTL_DISK_REASSIGN_BLOCKS_EX CTL_CODE(IOCTL_DISK_BASE, 0x0029, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_DISK_GET_PARTITION_ATTRIBUTES CTL_CODE(IOCTL_DISK_BASE, 0x003a, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DISK_SET_PARTITION_ATTRIBUTES CTL_CODE(IOCTL_DISK_BASE, 0x003b, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_DISK_GET_DISK_ATTRIBUTES CTL_CODE(IOCTL_DISK_BASE, 0x003c, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DISK_SET_DISK_ATTRIBUTES CTL_CODE(IOCTL_DISK_BASE, 0x003d, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_DISK_IS_CLUSTERED CTL_CODE(IOCTL_DISK_BASE, 0x003e, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DISK_GET_SAN_SETTINGS CTL_CODE(IOCTL_DISK_BASE, 0x0080, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_DISK_SET_SAN_SETTINGS CTL_CODE(IOCTL_DISK_BASE, 0x0081, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_DISK_GET_SNAPSHOT_INFO CTL_CODE(IOCTL_DISK_BASE, 0x0082, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_DISK_SET_SNAPSHOT_INFO CTL_CODE(IOCTL_DISK_BASE, 0x0083, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_DISK_RESET_SNAPSHOT_INFO CTL_CODE(IOCTL_DISK_BASE, 0x0084, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#endif
#ifndef IOCTL_STORAGE_GET_DEVICE_NUMBER_EX
#define IOCTL_STORAGE_GET_DEVICE_NUMBER_EX CTL_CODE(IOCTL_STORAGE_BASE, 0x0421, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_STORAGE_FAILURE_PREDICTION_CONFIG CTL_CODE(IOCTL_STORAGE_BASE, 0x0441, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_STORAGE_GET_COUNTERS CTL_CODE(IOCTL_STORAGE_BASE, 0x0442, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_STORAGE_GET_DEVICE_TELEMETRY CTL_CODE(IOCTL_STORAGE_BASE, 0x0470, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_STORAGE_DEVICE_TELEMETRY_NOTIFY CTL_CODE(IOCTL_STORAGE_BASE, 0x0471, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#endif
#ifndef IOCTL_VOLUME_GET_BC_PROPERTIES
#define IOCTL_VOLUME_GET_BC_PROPERTIES CTL_CODE(IOCTL_VOLUME_BASE, 15, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_VOLUME_ALLOCATE_BC_STREAM CTL_CODE(IOCTL_VOLUME_BASE, 16, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_VOLUME_FREE_BC_STREAM CTL_CODE(IOCTL_VOLUME_BASE, 17, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_VOLUME_BC_VERSION 1
#define IOCTL_VOLUME_IS_DYNAMIC CTL_CODE(IOCTL_VOLUME_BASE, 18, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VOLUME_PREPARE_FOR_CRITICAL_IO CTL_CODE(IOCTL_VOLUME_BASE, 19, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_VOLUME_QUERY_ALLOCATION_HINT CTL_CODE(IOCTL_VOLUME_BASE, 20, METHOD_OUT_DIRECT, FILE_READ_ACCESS)
#define IOCTL_VOLUME_UPDATE_PROPERTIES CTL_CODE(IOCTL_VOLUME_BASE, 21, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VOLUME_QUERY_MINIMUM_SHRINK_SIZE CTL_CODE(IOCTL_VOLUME_BASE, 22, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_VOLUME_PREPARE_FOR_SHRINK CTL_CODE(IOCTL_VOLUME_BASE, 23, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#endif

// Mount manager requests of Windows Server 2003 and 7 (WDK 7.1 mountmgr.h).
// XP's mount manager answers the auto-mount requests with an error, which
// the driver handles.
#define MOUNTMGRCONTROLTYPE_XP 0x0000006D
#ifndef IOCTL_MOUNTMGR_SCRUB_REGISTRY
#define IOCTL_MOUNTMGR_SCRUB_REGISTRY CTL_CODE(MOUNTMGRCONTROLTYPE_XP, 14, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_MOUNTMGR_QUERY_AUTO_MOUNT CTL_CODE(MOUNTMGRCONTROLTYPE_XP, 15, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_MOUNTMGR_SET_AUTO_MOUNT CTL_CODE(MOUNTMGRCONTROLTYPE_XP, 16, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
typedef enum _MOUNTMGR_AUTO_MOUNT_STATE {
  Disabled = 0,
  Enabled
} MOUNTMGR_AUTO_MOUNT_STATE;
typedef struct _MOUNTMGR_QUERY_AUTO_MOUNT {
  MOUNTMGR_AUTO_MOUNT_STATE CurrentState;
} MOUNTMGR_QUERY_AUTO_MOUNT, *PMOUNTMGR_QUERY_AUTO_MOUNT;
typedef struct _MOUNTMGR_SET_AUTO_MOUNT {
  MOUNTMGR_AUTO_MOUNT_STATE NewState;
} MOUNTMGR_SET_AUTO_MOUNT, *PMOUNTMGR_SET_AUTO_MOUNT;
#endif
#ifndef IOCTL_MOUNTMGR_BOOT_DL_ASSIGNMENT
#define IOCTL_MOUNTMGR_BOOT_DL_ASSIGNMENT CTL_CODE(MOUNTMGRCONTROLTYPE_XP, 17, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define IOCTL_MOUNTMGR_TRACELOG_CACHE CTL_CODE(MOUNTMGRCONTROLTYPE_XP, 18, METHOD_BUFFERED, FILE_READ_ACCESS)
#endif

typedef struct _FILE_DISPOSITION_INFORMATION_EX {
  ULONG Flags;
} FILE_DISPOSITION_INFORMATION_EX, *PFILE_DISPOSITION_INFORMATION_EX;
#ifndef FILE_DISPOSITION_DELETE
#define FILE_DISPOSITION_DO_NOT_DELETE 0x00000000
#define FILE_DISPOSITION_DELETE 0x00000001
#define FILE_DISPOSITION_POSIX_SEMANTICS 0x00000002
#define FILE_DISPOSITION_FORCE_IMAGE_SECTION_CHECK 0x00000004
#define FILE_DISPOSITION_ON_CLOSE 0x00000008
#define FILE_DISPOSITION_IGNORE_READONLY_ATTRIBUTE 0x00000010
#endif

// DEVOBJ_EXTENSION: the WDK 7.1 only declares its first fields. The layout
// of the fields up to ExtensionFlags is the same from Windows XP to 10 (x86
// and x64); the driver reads ExtensionFlags (DOE_*) to see a deleted device.
typedef struct _DOKAN_XP_DEVOBJ_EXTENSION {
  CSHORT Type;
  USHORT Size;
  PDEVICE_OBJECT DeviceObject;
  ULONG PowerFlags;
  PVOID Dope;
  ULONG ExtensionFlags;
} DOKAN_XP_DEVOBJ_EXTENSION, *PDOKAN_XP_DEVOBJ_EXTENSION;
#define DOKAN_XP_EXTENSION_FLAGS(DeviceObjectExtension)                      \
  (((PDOKAN_XP_DEVOBJ_EXTENSION)(DeviceObjectExtension))->ExtensionFlags)

// No-execute pool and MDL mappings exist from Windows 8 on; the driver only
// asks for them when RtlIsNtDdiVersionAvailable(NTDDI_WIN8) says so.
#ifndef MdlMappingNoExecute
#define MdlMappingNoExecute 0x40000000
#endif
#ifndef DrvRtPoolNxOptIn
#define DrvRtPoolNxOptIn 0x00000001
#define ExInitializeDriverRuntime(RuntimeFlags) ((void)0)
#endif

// ---------------------------------------------------------------------------
// Oplocks. Windows XP and Vista have the legacy oplocks only (level 1, level
// 2, batch, filter). Oplock keys, atomic oplocks, handle (RH) oplocks and
// FSCTL_REQUEST_OPLOCK came with Windows 7; the I/O manager of XP and Vista
// never sends them, so the Windows 7 only calls have nothing to do here.
// ---------------------------------------------------------------------------
#ifndef FILE_OPEN_REQUIRING_OPLOCK
#define FILE_OPEN_REQUIRING_OPLOCK 0x00010000
#endif
#ifndef FSCTL_REQUEST_OPLOCK
#define FSCTL_REQUEST_OPLOCK \
  CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 144, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
#ifndef OPLOCK_LEVEL_CACHE_READ
#define OPLOCK_LEVEL_CACHE_READ 0x00000001
#define OPLOCK_LEVEL_CACHE_HANDLE 0x00000002
#define OPLOCK_LEVEL_CACHE_WRITE 0x00000004
#define REQUEST_OPLOCK_INPUT_FLAG_REQUEST 0x00000001
#define REQUEST_OPLOCK_INPUT_FLAG_ACK 0x00000002
#define REQUEST_OPLOCK_INPUT_FLAG_COMPLETE_ACK_ON_CLOSE 0x00000004
#define REQUEST_OPLOCK_CURRENT_VERSION 1
#define REQUEST_OPLOCK_OUTPUT_FLAG_ACK_REQUIRED 0x00000001
#define REQUEST_OPLOCK_OUTPUT_FLAG_MODES_PROVIDED 0x00000002
typedef struct _REQUEST_OPLOCK_INPUT_BUFFER {
  USHORT StructureVersion;
  USHORT StructureLength;
  ULONG RequestedOplockLevel;
  ULONG Flags;
} REQUEST_OPLOCK_INPUT_BUFFER, *PREQUEST_OPLOCK_INPUT_BUFFER;
typedef struct _REQUEST_OPLOCK_OUTPUT_BUFFER {
  USHORT StructureVersion;
  USHORT StructureLength;
  ULONG OriginalOplockLevel;
  ULONG NewOplockLevel;
  ULONG Flags;
  ACCESS_MASK AccessMode;
  USHORT ShareMode;
} REQUEST_OPLOCK_OUTPUT_BUFFER, *PREQUEST_OPLOCK_OUTPUT_BUFFER;
#endif
#ifndef OPLOCK_FLAG_COMPLETE_IF_OPLOCKED
#define OPLOCK_FLAG_COMPLETE_IF_OPLOCKED 0x00000001
#endif
#ifndef OPLOCK_FLAG_OPLOCK_KEY_CHECK_ONLY
#define OPLOCK_FLAG_OPLOCK_KEY_CHECK_ONLY 0x00000002
#endif
#ifndef OPLOCK_FLAG_BACK_OUT_ATOMIC_OPLOCK
#define OPLOCK_FLAG_BACK_OUT_ATOMIC_OPLOCK 0x00000004
#endif

// FsRtlCheckOplockEx (Vista; the flags below are Windows 7): an oplock key
// check and the back-out of an atomic oplock have nothing to do without keys
// and atomic oplocks; everything else is the legacy FsRtlCheckOplock.
static __inline NTSTATUS DokanXpCheckOplockEx(
    __in POPLOCK Oplock, __in PIRP Irp, __in ULONG Flags,
    __in_opt PVOID Context,
    __in_opt POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    __in_opt POPLOCK_FS_PREPOST_IRP PostIrpRoutine) {
  if (Flags &
      (OPLOCK_FLAG_OPLOCK_KEY_CHECK_ONLY | OPLOCK_FLAG_BACK_OUT_ATOMIC_OPLOCK)) {
    return STATUS_SUCCESS;
  }
  return FsRtlCheckOplock(Oplock, Irp, Context, CompletionRoutine,
                          PostIrpRoutine);
}
#define FsRtlCheckOplockEx DokanXpCheckOplockEx

// FsRtlOplockBreakH (Windows 7) breaks handle (RH) oplocks, which XP and Vista
// do not have: there is never one to break.
#define FsRtlOplockBreakH(Oplock, Irp, Flags, Context, CompletionRoutine,     \
                          PostIrpRoutine)                                     \
  STATUS_SUCCESS

// FsRtlOplockIsSharedRequest (Windows 7): a level 2 request, or a Windows 7
// request without the write-caching level.
static __inline BOOLEAN DokanXpOplockIsSharedRequest(__in PIRP Irp) {
  PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
  ULONG code = irpSp->Parameters.FileSystemControl.FsControlCode;
  PREQUEST_OPLOCK_INPUT_BUFFER input;
  if (code == FSCTL_REQUEST_OPLOCK_LEVEL_2) {
    return TRUE;
  }
  if (code != FSCTL_REQUEST_OPLOCK) {
    return FALSE;
  }
  input = (PREQUEST_OPLOCK_INPUT_BUFFER)Irp->AssociatedIrp.SystemBuffer;
  return (BOOLEAN)(input != NULL &&
                   irpSp->Parameters.FileSystemControl.InputBufferLength >=
                       sizeof(REQUEST_OPLOCK_INPUT_BUFFER) &&
                   !(input->RequestedOplockLevel & OPLOCK_LEVEL_CACHE_WRITE));
}
#define FsRtlOplockIsSharedRequest DokanXpOplockIsSharedRequest

// FsRtlAreThereCurrentOrInProgressFileLocks (Windows 7): any byte-range lock.
#define FsRtlAreThereCurrentOrInProgressFileLocks(FileLock)                   \
  ((BOOLEAN)(FsRtlGetNextFileLock((FileLock), TRUE) != NULL))

// ---------------------------------------------------------------------------
// Routines Windows XP does not export (Windows Vista does).
// ---------------------------------------------------------------------------
#if (NTDDI_VERSION < NTDDI_VISTA)

// ExEnterCriticalRegionAndAcquireResourceExclusive and its release partner
// (Windows Server 2003 and later).
static __inline PVOID DokanXpEnterCriticalRegionAndAcquireResourceExclusive(
    __in PERESOURCE Resource) {
  KeEnterCriticalRegion();
  ExAcquireResourceExclusiveLite(Resource, TRUE);
  return NULL;
}
#define ExEnterCriticalRegionAndAcquireResourceExclusive                      \
  DokanXpEnterCriticalRegionAndAcquireResourceExclusive

static __inline VOID DokanXpReleaseResourceAndLeaveCriticalRegion(
    __in PERESOURCE Resource) {
  ExReleaseResourceLite(Resource);
  KeLeaveCriticalRegion();
}
#define ExReleaseResourceAndLeaveCriticalRegion                               \
  DokanXpReleaseResourceAndLeaveCriticalRegion

// LOOKASIDE_LIST_EX (Vista): every list of the driver is non-paged, so the
// XP non-paged lookaside list does the same job. (The WDK 7.1 declares the
// Vista type for XP too, without its routines: the driver gets this one.)
typedef struct _DOKAN_XP_LOOKASIDE_LIST {
  NPAGED_LOOKASIDE_LIST List;
} DOKAN_XP_LOOKASIDE_LIST, *PDOKAN_XP_LOOKASIDE_LIST;
#define LOOKASIDE_LIST_EX DOKAN_XP_LOOKASIDE_LIST
#define PLOOKASIDE_LIST_EX PDOKAN_XP_LOOKASIDE_LIST

static __inline NTSTATUS DokanXpInitializeLookasideListEx(
    __out PDOKAN_XP_LOOKASIDE_LIST Lookaside, __in_opt PVOID Allocate,
    __in_opt PVOID Free, __in POOL_TYPE PoolType, __in ULONG Flags,
    __in SIZE_T Size, __in ULONG Tag, __in USHORT Depth) {
  UNREFERENCED_PARAMETER(Allocate);
  UNREFERENCED_PARAMETER(Free);
  UNREFERENCED_PARAMETER(PoolType);
  UNREFERENCED_PARAMETER(Flags);
  ExInitializeNPagedLookasideList(&Lookaside->List, NULL, NULL, 0, Size, Tag,
                                  Depth);
  return STATUS_SUCCESS;
}
#define ExInitializeLookasideListEx DokanXpInitializeLookasideListEx
#define ExAllocateFromLookasideListEx(Lookaside)                              \
  ExAllocateFromNPagedLookasideList(&(Lookaside)->List)
#define ExFreeToLookasideListEx(Lookaside, Entry)                             \
  ExFreeToNPagedLookasideList(&(Lookaside)->List, (Entry))
#define ExDeleteLookasideListEx(Lookaside)                                    \
  ExDeleteNPagedLookasideList(&(Lookaside)->List)

// KeRemoveQueueEx (Vista): the driver removes one entry at a time, which is
// KeRemoveQueue. Its result is the entry, or a status cast to a pointer
// (STATUS_TIMEOUT, STATUS_USER_APC), as KeRemoveQueueEx stores it.
#define KeRemoveQueueEx(Queue, WaitMode, Alertable, Timeout, EntryArray,      \
                        Count)                                                \
  (*(EntryArray) = KeRemoveQueue((Queue), (WaitMode), (Timeout)), 1)

// Extra create parameters (ECPs, Vista). The driver only reads one for a
// folder mount point reached through a reparse point, which needs Vista; on
// XP an IRP never has an ECP list.
typedef struct _ECP_LIST ECP_LIST, *PECP_LIST;
#define FsRtlGetEcpListFromIrp(Irp, EcpList)                                  \
  (*(EcpList) = NULL, STATUS_NOT_FOUND)
#define FsRtlFindExtraCreateParameter(EcpList, EcpType, EcpContext,           \
                                      EcpContextSize)                         \
  STATUS_NOT_FOUND
#define FsRtlIsEcpFromUserMode(EcpContext) TRUE

// MmDoesFileHaveUserWritableReferences (Vista) refuses an open without write
// sharing while a user mapping can write the file but no handle is open. XP
// has no way to ask; the open then follows the share access of the handles.
#define MmDoesFileHaveUserWritableReferences(SectionPointer) FALSE

// FsRtlNotifyCleanupAll (Vista): xp_compat.c completes the pending change
// notifications handle by handle (FsRtlNotifyCleanup for every CCB).
VOID DokanXpNotifyCleanupAll(__in PNOTIFY_SYNC NotifySync,
                             __in PLIST_ENTRY NotifyList);
#define FsRtlNotifyCleanupAll DokanXpNotifyCleanupAll

// The pull IOCTL's wait for events; see xp_compat.c (the VCB type comes later
// in dokan.h, so the parameter is untyped here).
PLIST_ENTRY DokanXpRemoveQueueUntilUnmount(__in PRKQUEUE Queue,
                                           __in_opt PLARGE_INTEGER Timeout,
                                           __in PVOID Vcb);

#endif // NTDDI_VERSION < NTDDI_VISTA

#endif // DOKAN_XP_COMPAT_H_
