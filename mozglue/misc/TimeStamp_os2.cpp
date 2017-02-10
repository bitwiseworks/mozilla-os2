/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Implement TimeStamp::Now() with the OS/2 high-resolution timer. When the timer is not
// available, use the dummy TimeStamp implementation using PR_IntervalNow() as a fallback
// (its possible resolution is limited to 100000 ticks per second max (as set by
// PR_INTERVAL_MAX).

#define INCL_BASE
#define INCL_PM
#include <os2.h>

#include "mozilla/TimeStamp.h"
#include "prlock.h"
#include "prinrval.h"

namespace mozilla {

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

MFBT_API double
BaseTimeDurationPlatformUtils::ToSeconds(int64_t aTicks)
{
  return double(aTicks)/gTicksPerSecDbl;
}

MFBT_API double
BaseTimeDurationPlatformUtils::ToSecondsSigDigits(int64_t aTicks)
{
  return ToSeconds(aTicks);
}

MFBT_API int64_t
BaseTimeDurationPlatformUtils::TicksFromMilliseconds(double aMilliseconds)
{
  double result = aMilliseconds * gTicksPerMsDbl;
  if (result > INT64_MAX) {
    return INT64_MAX;
  } else if (result < INT64_MIN) {
    return INT64_MIN;
  }

  return result;
}

MFBT_API int64_t
BaseTimeDurationPlatformUtils::ResolutionInTicks()
{
  return static_cast<int64_t>(gTicksPerSec);
}

MFBT_API void
TimeStamp::Startup()
{
  if (gInitialized)
    return;

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
      return;
    gRolloverCount = 1;
    gLastNow = 0;
    gTicksPerSec = PR_TicksPerSecond();
  }

  gTicksPerSecDbl = gTicksPerSec;
  gTicksPerMsDbl = gTicksPerSecDbl / 1000.0;

  gInitialized = true;
}

MFBT_API void
TimeStamp::Shutdown()
{
  if (gInitialized) {
    if (!gUseHighResTimer) {
      PR_DestroyLock(gTimeStampLock);
      gTimeStampLock = 0;
    }
  }
}

MFBT_API TimeStamp
TimeStamp::Now(bool aHighResolution)
{
  // Note: we don't use aHighResolution; this was introduced to solve Windows bugs (see
  // TimeStamp.h). We solve ours globally with the NSPR_OS2_NO_HIRES_TIMER check.

  if (gUseHighResTimer) {
    QWORD timestamp;
    APIRET rc = DosTmrQueryTime(&timestamp);
    if (rc == NO_ERROR)
      return TimeStamp((uint64_t(timestamp.ulHi) << 32) + timestamp.ulLo);
  }

  // XXX this could be considerably simpler and faster if we had
  // 64-bit atomic operations
  PR_Lock(gTimeStampLock);

  PRIntervalTime now = PR_IntervalNow();
  if (now < gLastNow)
    ++gRolloverCount;

  gLastNow = now;
  TimeStamp result((uint64_t(gRolloverCount) << 32) + now);

  PR_Unlock(gTimeStampLock);
  return result;
}

// @todo ComputeProcessUptime is a noop for now. See what can be done with DosPerSysCall
// and DosQProcStat here http://www.edm2.com/0607/cpu.html

MFBT_API uint64_t
TimeStamp::ComputeProcessUptime()
{
  return 0;
}

} // namespace mozilla
