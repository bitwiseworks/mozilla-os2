/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Implement TimeStamp::Now() with the OS/2 high-resolution timer. When the timer is not
// available, use the dummy TimeStamp implementation using PR_IntervalNow() as a fallback
// (its possible resolution is limited to 100000 ticks per second max (as set by
// PR_INTERVAL_MAX).

#define INCL_DOS
#define INCL_DOSERRORS
#include <os2.h>

#include "mozilla/TimeStamp.h"
#include "prlock.h"

namespace mozilla {

struct TimeStampInitialization
{
  TimeStampInitialization() {
    TimeStamp::Startup();
  }
  ~TimeStampInitialization() {
    TimeStamp::Shutdown();
  }
};

static TimeStampInitialization initOnce;
static bool gInitialized = false;

// Variables for the high-res timer
static bool gUseHighResTimer = false;

// Variables for the PR_IntervalNow fallback
static PRLock* gTimeStampLock = 0;
static PRUint32 gRolloverCount;
static PRIntervalTime gLastNow;

static ULONG gTicksPerSec = 0;
static double gTicksPerSecDbl = .0;
static double gTicksPerMsDbl = .0;

double
TimeDuration::ToSeconds() const
{
  return double(mValue)/gTicksPerSecDbl;
}

double
TimeDuration::ToSecondsSigDigits() const
{
  return ToSeconds();
}

TimeDuration
TimeDuration::FromMilliseconds(double aMilliseconds)
{
  return TimeDuration::FromTicks(aMilliseconds * gTicksPerMsDbl);
}

TimeDuration
TimeDuration::Resolution()
{
  return TimeDuration::FromTicks(int64_t(gTicksPerSec));
}

nsresult
TimeStamp::Startup()
{
  if (gInitialized)
    return NS_OK;

  const char *envp;
  APIRET rc;

  // Use the same variable as NSPR's os2inrval.c does to let the user disable the
  // high-resolution timer (it is known that it doesn't work well on some hardware)
  if ((envp = getenv("NSPR_OS2_NO_HIRES_TIMER")) != NULL) {
    if (atoi(envp) != 1) {
      // Attempt to use the high-res timer
      rc = DosTmrQueryFreq(&gTicksPerSec);
      if (rc == NO_ERROR)
        gUseHighResTimer = true;
    }
  }

  if (!gUseHighResTimer) {
    // TimeStamp has to use bare PRLock instead of mozilla::Mutex
    // because TimeStamp can be used very early in startup.
    gTimeStampLock = PR_NewLock();
    if (!gTimeStampLock)
      return NS_ERROR_OUT_OF_MEMORY;
    gRolloverCount = 1;
    gLastNow = 0;
    gTicksPerSec = PR_TicksPerSecond();
  }

  gTicksPerSecDbl = gTicksPerSec;
  gTicksPerMsDbl = gTicksPerSecDbl / 1000.0;

  gInitialized = true;
  sFirstTimeStamp = TimeStamp::Now();
  sProcessCreation = TimeStamp();

  return NS_OK;
}

void
TimeStamp::Shutdown()
{
  if (gInitialized) {
    if (!gUseHighResTimer) {
      PR_DestroyLock(gTimeStampLock);
      gTimeStampLock = 0;
    }
  }
}

TimeStamp
TimeStamp::Now(bool aHighResolution)
{
  // Note: we don't use aHighResolution; this was introduced to solve Windows bugs (see
  // TimeStamp.h). We solve ours globally with the NSPR_OS2_NO_HIRES_TIMER check.

  if (gUseHighResTimer) {
    QWORD timestamp;
    APIRET rc = DosTmrQueryTime(&timestamp);
    if (rc != NO_ERROR)
      NS_RUNTIMEABORT("DosTmrQueryTime failed!");
    return TimeStamp((uint64_t(timestamp.ulHi) << 32) + timestamp.ulLo);
  }

  // XXX this could be considerably simpler and faster if we had
  // 64-bit atomic operations
  PR_Lock(gTimeStampLock);

  PRIntervalTime now = PR_IntervalNow();
  if (now < gLastNow) {
    ++gRolloverCount;
    // This can't happen unless you've been running for millions of years
    NS_ASSERTION(gRolloverCount > 0, "Rollover in rollover count???");
  }

  gLastNow = now;
  TimeStamp result((uint64_t(gRolloverCount) << 32) + now);

  PR_Unlock(gTimeStampLock);
  return result;
}

// @todo ComputeProcessUptime is a noop for now. See what can be done with DosPerSysCall
// and DosQProcStat here http://www.edm2.com/0607/cpu.html

uint64_t
TimeStamp::ComputeProcessUptime()
{
  return 0;
}

} // namespace mozilla
