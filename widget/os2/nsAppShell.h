/* -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsAppShell_h__
#define nsAppShell_h__

#define INCL_BASE
#define INCL_PM
#include <os2.h>

#include "nsBaseAppShell.h"

/**
 * Native OS/2 Application shell wrapper
 */

class nsAppShell : public nsBaseAppShell
{
public:
  nsAppShell() : mEventWnd(NULLHANDLE) {}

  nsresult Init();

protected:
  virtual void ScheduleNativeEventCallback();
  virtual bool ProcessNextNativeEvent(bool mayWait);
  virtual ~nsAppShell();

protected:
  HWND mEventWnd;

friend MRESULT EXPENTRY EventWindowProc(HWND, ULONG, MPARAM, MPARAM);
};


#endif // nsAppShell_h__

