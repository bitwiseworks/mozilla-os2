/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Dummy TimeStamp implementation using PR_IntervalNow(). Its possible
// resolution is limited to 100000 ticks per second max (as set by
// PR_INTERVAL_MAX).

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

static PRLock* gTimeStampLock = 0;
static PRUint32 gRolloverCount;
static PRIntervalTime gLastNow;

double
TimeDuration::ToSeconds() const
{
 return double(mValue)/PR_TicksPerSecond();
}

double
TimeDuration::ToSecondsSigDigits() const
{
  return ToSeconds();
}

TimeDuration
TimeDuration::FromMilliseconds(double aMilliseconds)
{
  static double kTicksPerMs = double(PR_TicksPerSecond()) / 1000.0;
  return TimeDuration::FromTicks(aMilliseconds * kTicksPerMs);
}

TimeDuration
TimeDuration::Resolution()
{
  // This is grossly nonrepresentative of actual system capabilities
  // on some platforms
  return TimeDuration::FromTicks(PRInt64(1));
}

nsresult
TimeStamp::Startup()
{
  if (gTimeStampLock)
    return NS_OK;

  // TimeStamp has to use bare PRLock instead of mozilla::Mutex
  // because TimeStamp can be used very early in startup.
  gTimeStampLock = PR_NewLock();
  if (!gTimeStampLock)
    return NS_ERROR_OUT_OF_MEMORY;

  gRolloverCount = 1;
  gLastNow = 0;

  sFirstTimeStamp = TimeStamp::Now();
  sProcessCreation = TimeStamp();

  return NS_OK;
}

void
TimeStamp::Shutdown()
{
  if (gTimeStampLock) {
    PR_DestroyLock(gTimeStampLock);
    gTimeStampLock = 0;
  }
}

TimeStamp
TimeStamp::Now(bool aHighResolution)
{
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
  TimeStamp result((PRUint64(gRolloverCount) << 32) + now);

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
