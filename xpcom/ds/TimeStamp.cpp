/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of the OS-independent methods of the TimeStamp class
 */

#include "mozilla/TimeStamp.h"
#include "prenv.h"
#include "prlock.h"

namespace mozilla {

TimeStamp TimeStamp::sFirstTimeStamp;
TimeStamp TimeStamp::sProcessCreation;

TimeStamp
TimeStamp::ProcessCreation(bool& aIsInconsistent)
{
  aIsInconsistent = false;

  if (sProcessCreation.IsNull()) {
    char *mozAppRestart = PR_GetEnv("MOZ_APP_RESTART");
    TimeStamp ts;

    /* When calling PR_SetEnv() with an empty value the existing variable may
     * be unset or set to the empty string depending on the underlying platform
     * thus we have to check if the variable is present and not empty. */
    if (mozAppRestart && (strcmp(mozAppRestart, "") != 0)) {
      /* Firefox was restarted, use the first time-stamp we've taken as the new
       * process startup time and unset MOZ_APP_RESTART. */
      ts = sFirstTimeStamp;
      PR_SetEnv("MOZ_APP_RESTART=");
    } else {
      TimeStamp now = Now();
      uint64_t uptime = ComputeProcessUptime();

      ts = now - TimeDuration::FromMicroseconds(uptime);

      if ((ts > sFirstTimeStamp) || (uptime == 0)) {
        /* If the process creation timestamp was inconsistent replace it with
         * the first one instead and notify that a telemetry error was
         * detected. */
        aIsInconsistent = true;
        ts = sFirstTimeStamp;
      }
    }

    sProcessCreation = ts;
  }

  return sProcessCreation;
}

void
TimeStamp::RecordProcessRestart()
{
  PR_SetEnv("MOZ_APP_RESTART=1");
  sProcessCreation = TimeStamp();
}

#if defined(XP_OS2)

// Dummy TimeStamp implementation using PR_IntervalNow(). Its possible
// resolution is limited to 100000 ticks per second max (as set by
// PR_INTERVAL_MAX).

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
  gRolloverCount = 1;
  gLastNow = 0;
  return gTimeStampLock ? NS_OK : NS_ERROR_OUT_OF_MEMORY;
}

void
TimeStamp::Shutdown()
{
  if (gTimeStampLock) {
    PR_DestroyLock(gTimeStampLock);
    gTimeStampLock = nsnull;
  }
}

TimeStamp
TimeStamp::Now()
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

#endif // defined(XP_OS2)

} // namespace mozilla
