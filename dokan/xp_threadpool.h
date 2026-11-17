/*
  Dokan : user-mode file system library for Windows

  XP compatibility: the part of the Vista thread pool API that dokan2.dll uses
  (a pool, cleanup groups, callback environments and work objects), for
  Windows XP, which has no CreateThreadpool. xp_compat.h maps the Vista names
  to these functions in the XP (x86) build; the Vista (x64) build uses the
  system thread pool.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation; either version 3 of the License, or (at your option) any
  later version.
*/

#ifndef DOKAN_XP_THREADPOOL_H_
#define DOKAN_XP_THREADPOOL_H_

#include <windows.h>

typedef struct _DOKAN_XP_TP_POOL DOKAN_XP_TP_POOL, *PDOKAN_XP_TP_POOL;
typedef struct _DOKAN_XP_TP_CLEANUP_GROUP DOKAN_XP_TP_CLEANUP_GROUP,
    *PDOKAN_XP_TP_CLEANUP_GROUP;
typedef struct _DOKAN_XP_TP_WORK DOKAN_XP_TP_WORK, *PDOKAN_XP_TP_WORK;
// Callbacks get NULL as their instance: dokan2.dll never uses it.
typedef struct _DOKAN_XP_TP_CALLBACK_INSTANCE *PDOKAN_XP_TP_CALLBACK_INSTANCE;

typedef VOID(CALLBACK *PDOKAN_XP_TP_WORK_CALLBACK)(
    PDOKAN_XP_TP_CALLBACK_INSTANCE Instance, PVOID Context,
    PDOKAN_XP_TP_WORK Work);
typedef VOID(CALLBACK *PDOKAN_XP_TP_CLEANUP_GROUP_CANCEL_CALLBACK)(
    PVOID ObjectContext, PVOID CleanupContext);

typedef struct _DOKAN_XP_TP_CALLBACK_ENVIRON {
  PDOKAN_XP_TP_POOL Pool;
  PDOKAN_XP_TP_CLEANUP_GROUP CleanupGroup;
} DOKAN_XP_TP_CALLBACK_ENVIRON, *PDOKAN_XP_TP_CALLBACK_ENVIRON;

PDOKAN_XP_TP_POOL WINAPI DokanXpCreateThreadpool(PVOID Reserved);
VOID WINAPI DokanXpCloseThreadpool(PDOKAN_XP_TP_POOL Pool);
PDOKAN_XP_TP_CLEANUP_GROUP WINAPI DokanXpCreateThreadpoolCleanupGroup(VOID);
VOID WINAPI DokanXpCloseThreadpoolCleanupGroupMembers(
    PDOKAN_XP_TP_CLEANUP_GROUP Group, BOOL CancelPendingCallbacks,
    PVOID CleanupContext);
VOID WINAPI DokanXpCloseThreadpoolCleanupGroup(PDOKAN_XP_TP_CLEANUP_GROUP Group);
PDOKAN_XP_TP_WORK WINAPI DokanXpCreateThreadpoolWork(
    PDOKAN_XP_TP_WORK_CALLBACK Callback, PVOID Context,
    PDOKAN_XP_TP_CALLBACK_ENVIRON Environment);
VOID WINAPI DokanXpSubmitThreadpoolWork(PDOKAN_XP_TP_WORK Work);
// Not in the Vista API: waits until every submitted work item has been taken
// by a thread (see dokan.c, DokanCreateFileSystem).
BOOL WINAPI DokanXpThreadpoolWaitQueueTaken(PDOKAN_XP_TP_POOL Pool,
                                            DWORD Milliseconds);

#define DokanXpInitializeThreadpoolEnvironment(Environment)                  \
  ((Environment)->Pool = NULL, (Environment)->CleanupGroup = NULL)
#define DokanXpSetThreadpoolCallbackPool(Environment, ThreadPool)            \
  ((Environment)->Pool = (ThreadPool))
#define DokanXpSetThreadpoolCallbackCleanupGroup(Environment, Group, Cancel) \
  ((Environment)->CleanupGroup = (Group))
#define DokanXpDestroyThreadpoolEnvironment(Environment) ((void)0)

#endif // DOKAN_XP_THREADPOOL_H_
