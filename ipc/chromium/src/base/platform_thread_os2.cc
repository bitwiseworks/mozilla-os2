// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// exceptq trap file generator
#include <string.h>
#define INCL_BASE
#include <os2.h>
#define INCL_LIBLOADEXCEPTQ
#include <exceptq.h>

#include "base/platform_thread.h"

#include "base/logging.h"

namespace {

void ThreadFunc(void* closure) {
  EXCEPTIONREGISTRATIONRECORD exceptqreg;
  LibLoadExceptq(&exceptqreg);

  PlatformThread::Delegate* delegate =
      static_cast<PlatformThread::Delegate*>(closure);
  delegate->ThreadMain();

  UninstallExceptq(&exceptqreg);
}

}  // namespace

// static
PlatformThreadId PlatformThread::CurrentId() {
  PTIB ptib;
  ::DosGetInfoBlocks(&ptib, NULL);
  return ptib->tib_ptib2->tib2_ultid;
}

// static
void PlatformThread::YieldCurrentThread() {
  ::DosSleep(0);
}

// static
void PlatformThread::Sleep(int duration_ms) {
  ::DosSleep(duration_ms);
}

// static
void PlatformThread::SetName(const char* name) {
  // no way to set thread name on OS/2
  return;
}

// static
bool PlatformThread::Create(size_t stack_size, Delegate* delegate,
                            PlatformThreadHandle* thread_handle) {
  int result = _beginthread(ThreadFunc, NULL, stack_size, delegate);
  if (result != -1) {
    *thread_handle = result;
    return true;
  }
  return false;
}

// static
bool PlatformThread::CreateNonJoinable(size_t stack_size, Delegate* delegate) {
  PlatformThreadHandle thread_handle;
  bool result = Create(stack_size, delegate, &thread_handle);
  return result;
}

// static
void PlatformThread::Join(PlatformThreadHandle thread_handle) {
  DCHECK(thread_handle);

  // Wait for the thread to exit.  It should already have terminated but make
  // sure this assumption is valid.
  TID tid = thread_handle;
  APIRET result = ::DosWaitThread(&tid, DCWW_WAIT);
  DCHECK_EQ(NO_ERROR, result);
}
