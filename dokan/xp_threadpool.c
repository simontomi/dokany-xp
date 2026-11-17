/*
  Dokan : user-mode file system library for Windows

  XP compatibility: a thread pool with the part of the Vista thread pool API
  that dokan2.dll uses (see xp_threadpool.h).

  dokan2.dll keeps its pulling threads in the pool for the whole life of a
  mount (they block in DeviceIoControl), so the pool must never run out of
  threads for queued work, as the Vista pool does not: a submitted work item
  that finds no idle thread starts a new one (up to a large limit), and a
  thread that has been idle for a while ends.

  Differences to the Vista pool that dokan2.dll does not notice:
  - A work object runs once and is freed after its callback: dokan2.dll
    submits every work object exactly once and never closes it (the Vista pool
    keeps it until the cleanup group's members are closed).
  - Callbacks get NULL as their callback instance (dokan2.dll ignores it).
  - CloseThreadpoolCleanupGroupMembers always waits for the queued and running
    callbacks (dokan2.dll asks for that: CancelPendingCallbacks is FALSE).

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU Lesser General Public License as published by the Free
  Software Foundation; either version 3 of the License, or (at your option) any
  later version.
*/

#include "xp_threadpool.h"

#include <process.h>
#include <stdlib.h>

// The Vista pool's default maximum is 500 threads.
#define DOKAN_XP_TP_MAX_THREADS 512
// Threads the pool keeps ready (started with the pool, never ended for being
// idle), as the Vista pool has: a submitted work item then runs at once
// instead of waiting for a new thread.
#define DOKAN_XP_TP_MIN_THREADS 4
// A thread above the minimum that has had no work for this long ends.
#define DOKAN_XP_TP_IDLE_TIMEOUT_MS 20000
// How long CloseThreadpool waits for the idle threads to end.
#define DOKAN_XP_TP_CLOSE_WAIT_MS 5000

struct _DOKAN_XP_TP_POOL {
  CRITICAL_SECTION Lock;
  // Submitted work not taken by a thread yet; one semaphore count each.
  LIST_ENTRY Queue;
  HANDLE Semaphore;
  LONG Pending;
  // Threads waiting for work, and all live threads.
  LONG Idle;
  LONG Threads;
  BOOL Closing;
  // Set while no thread lives.
  HANDLE NoThreads;
};

struct _DOKAN_XP_TP_CLEANUP_GROUP {
  CRITICAL_SECTION Lock;
  // Work objects of the group that have not finished their callback.
  LONG Outstanding;
  // Set while Outstanding is 0.
  HANDLE Finished;
};

struct _DOKAN_XP_TP_WORK {
  LIST_ENTRY Entry;
  PDOKAN_XP_TP_WORK_CALLBACK Callback;
  PVOID Context;
  PDOKAN_XP_TP_POOL Pool;
  PDOKAN_XP_TP_CLEANUP_GROUP Group;
};

static VOID XpListInit(PLIST_ENTRY Head) { Head->Flink = Head->Blink = Head; }

static VOID XpListInsertTail(PLIST_ENTRY Head, PLIST_ENTRY Entry) {
  Entry->Flink = Head;
  Entry->Blink = Head->Blink;
  Head->Blink->Flink = Entry;
  Head->Blink = Entry;
}

static PLIST_ENTRY XpListRemoveHead(PLIST_ENTRY Head) {
  PLIST_ENTRY entry = Head->Flink;
  Head->Flink = entry->Flink;
  entry->Flink->Blink = Head;
  return entry;
}

static VOID XpGroupFinishOne(PDOKAN_XP_TP_CLEANUP_GROUP Group) {
  EnterCriticalSection(&Group->Lock);
  if (--Group->Outstanding == 0) {
    SetEvent(Group->Finished);
  }
  LeaveCriticalSection(&Group->Lock);
}

static unsigned __stdcall XpWorkerThread(void *Parameter) {
  PDOKAN_XP_TP_POOL pool = (PDOKAN_XP_TP_POOL)Parameter;
  PDOKAN_XP_TP_WORK work;
  PDOKAN_XP_TP_CLEANUP_GROUP group;
  DWORD wait;

  EnterCriticalSection(&pool->Lock);
  pool->Idle++;
  LeaveCriticalSection(&pool->Lock);
  for (;;) {
    wait = WaitForSingleObject(pool->Semaphore, DOKAN_XP_TP_IDLE_TIMEOUT_MS);
    EnterCriticalSection(&pool->Lock);
    if (wait == WAIT_OBJECT_0 && pool->Pending > 0) {
      work = CONTAINING_RECORD(XpListRemoveHead(&pool->Queue),
                               DOKAN_XP_TP_WORK, Entry);
      pool->Pending--;
      pool->Idle--;
      LeaveCriticalSection(&pool->Lock);

      group = work->Group;
      work->Callback(NULL, work->Context, work);
      free(work);
      if (group) {
        XpGroupFinishOne(group);
      }

      EnterCriticalSection(&pool->Lock);
      pool->Idle++;
      LeaveCriticalSection(&pool->Lock);
      continue;
    }
    if (wait == WAIT_TIMEOUT && !pool->Closing) {
      // Idle for a while: end, unless work came in meanwhile or the pool is
      // down to the threads it keeps ready.
      if (pool->Pending > 0 || pool->Threads <= DOKAN_XP_TP_MIN_THREADS) {
        LeaveCriticalSection(&pool->Lock);
        continue;
      }
    } else if (wait == WAIT_OBJECT_0 && !pool->Closing) {
      // A count without work cannot happen; keep waiting.
      LeaveCriticalSection(&pool->Lock);
      continue;
    }
    // Idle timeout, or the pool closes: this thread ends.
    pool->Idle--;
    pool->Threads--;
    if (pool->Threads == 0) {
      SetEvent(pool->NoThreads);
    }
    LeaveCriticalSection(&pool->Lock);
    return 0;
  }
}

PDOKAN_XP_TP_POOL WINAPI DokanXpCreateThreadpool(PVOID Reserved) {
  PDOKAN_XP_TP_POOL pool;
  int i;
  UNREFERENCED_PARAMETER(Reserved);

  pool = (PDOKAN_XP_TP_POOL)calloc(1, sizeof(DOKAN_XP_TP_POOL));
  if (!pool) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  pool->Semaphore = CreateSemaphoreW(NULL, 0, MAXLONG, NULL);
  pool->NoThreads = CreateEventW(NULL, TRUE, TRUE, NULL);
  if (!pool->Semaphore || !pool->NoThreads) {
    DWORD error = GetLastError();
    if (pool->Semaphore) {
      CloseHandle(pool->Semaphore);
    }
    if (pool->NoThreads) {
      CloseHandle(pool->NoThreads);
    }
    free(pool);
    SetLastError(error);
    return NULL;
  }
  InitializeCriticalSection(&pool->Lock);
  XpListInit(&pool->Queue);
  for (i = 0; i < DOKAN_XP_TP_MIN_THREADS; i++) {
    uintptr_t thread;
    EnterCriticalSection(&pool->Lock);
    pool->Threads++;
    ResetEvent(pool->NoThreads);
    LeaveCriticalSection(&pool->Lock);
    thread = _beginthreadex(NULL, 0, XpWorkerThread, pool, 0, NULL);
    if (thread) {
      CloseHandle((HANDLE)thread);
    } else {
      EnterCriticalSection(&pool->Lock);
      pool->Threads--;
      if (pool->Threads == 0) {
        SetEvent(pool->NoThreads);
      }
      LeaveCriticalSection(&pool->Lock);
    }
  }
  return pool;
}

// Waits until every submitted work item has been taken by a thread (or the
// time is up). TRUE when the queue is empty.
BOOL WINAPI DokanXpThreadpoolWaitQueueTaken(PDOKAN_XP_TP_POOL Pool,
                                            DWORD Milliseconds) {
  DWORD start = GetTickCount();
  for (;;) {
    LONG pending;
    EnterCriticalSection(&Pool->Lock);
    pending = Pool->Pending;
    LeaveCriticalSection(&Pool->Lock);
    if (pending == 0) {
      return TRUE;
    }
    if (GetTickCount() - start >= Milliseconds) {
      return FALSE;
    }
    Sleep(1);
  }
}

VOID WINAPI DokanXpCloseThreadpool(PDOKAN_XP_TP_POOL Pool) {
  LONG threads;
  if (!Pool) {
    return;
  }
  EnterCriticalSection(&Pool->Lock);
  Pool->Closing = TRUE;
  threads = Pool->Threads;
  LeaveCriticalSection(&Pool->Lock);
  if (threads > 0) {
    ReleaseSemaphore(Pool->Semaphore, threads, NULL);
  }
  // dokan2.dll closes the pool in DokanShutdown, after every mount ended.
  // Free it only when no thread uses it any more.
  if (WaitForSingleObject(Pool->NoThreads, DOKAN_XP_TP_CLOSE_WAIT_MS) !=
      WAIT_OBJECT_0) {
    return;
  }
  DeleteCriticalSection(&Pool->Lock);
  CloseHandle(Pool->Semaphore);
  CloseHandle(Pool->NoThreads);
  free(Pool);
}

PDOKAN_XP_TP_CLEANUP_GROUP WINAPI DokanXpCreateThreadpoolCleanupGroup(VOID) {
  PDOKAN_XP_TP_CLEANUP_GROUP group = (PDOKAN_XP_TP_CLEANUP_GROUP)calloc(
      1, sizeof(DOKAN_XP_TP_CLEANUP_GROUP));
  if (!group) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  group->Finished = CreateEventW(NULL, TRUE, TRUE, NULL);
  if (!group->Finished) {
    DWORD error = GetLastError();
    free(group);
    SetLastError(error);
    return NULL;
  }
  InitializeCriticalSection(&group->Lock);
  return group;
}

VOID WINAPI DokanXpCloseThreadpoolCleanupGroupMembers(
    PDOKAN_XP_TP_CLEANUP_GROUP Group, BOOL CancelPendingCallbacks,
    PVOID CleanupContext) {
  UNREFERENCED_PARAMETER(CancelPendingCallbacks);
  UNREFERENCED_PARAMETER(CleanupContext);
  if (Group) {
    WaitForSingleObject(Group->Finished, INFINITE);
  }
}

VOID WINAPI DokanXpCloseThreadpoolCleanupGroup(
    PDOKAN_XP_TP_CLEANUP_GROUP Group) {
  if (!Group) {
    return;
  }
  DeleteCriticalSection(&Group->Lock);
  CloseHandle(Group->Finished);
  free(Group);
}

PDOKAN_XP_TP_WORK WINAPI DokanXpCreateThreadpoolWork(
    PDOKAN_XP_TP_WORK_CALLBACK Callback, PVOID Context,
    PDOKAN_XP_TP_CALLBACK_ENVIRON Environment) {
  PDOKAN_XP_TP_WORK work;
  if (!Callback || !Environment || !Environment->Pool) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return NULL;
  }
  work = (PDOKAN_XP_TP_WORK)calloc(1, sizeof(DOKAN_XP_TP_WORK));
  if (!work) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  work->Callback = Callback;
  work->Context = Context;
  work->Pool = Environment->Pool;
  work->Group = Environment->CleanupGroup;
  if (work->Group) {
    EnterCriticalSection(&work->Group->Lock);
    if (work->Group->Outstanding++ == 0) {
      ResetEvent(work->Group->Finished);
    }
    LeaveCriticalSection(&work->Group->Lock);
  }
  return work;
}

VOID WINAPI DokanXpSubmitThreadpoolWork(PDOKAN_XP_TP_WORK Work) {
  PDOKAN_XP_TP_POOL pool = Work->Pool;
  BOOL startThread = FALSE;
  uintptr_t thread;

  EnterCriticalSection(&pool->Lock);
  XpListInsertTail(&pool->Queue, &Work->Entry);
  pool->Pending++;
  if (pool->Pending > pool->Idle &&
      pool->Threads < DOKAN_XP_TP_MAX_THREADS) {
    pool->Threads++;
    if (pool->Threads == 1) {
      ResetEvent(pool->NoThreads);
    }
    startThread = TRUE;
  }
  LeaveCriticalSection(&pool->Lock);

  if (startThread) {
    thread = _beginthreadex(NULL, 0, XpWorkerThread, pool, 0, NULL);
    if (thread) {
      CloseHandle((HANDLE)thread);
    } else {
      // The work stays queued for the threads there are.
      EnterCriticalSection(&pool->Lock);
      pool->Threads--;
      if (pool->Threads == 0) {
        SetEvent(pool->NoThreads);
      }
      LeaveCriticalSection(&pool->Lock);
    }
  }
  ReleaseSemaphore(pool->Semaphore, 1, NULL);
}
