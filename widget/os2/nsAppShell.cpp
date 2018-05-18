/* -*- Mode: c++; tab-width: 2; indent-tabs-mode: nil; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAppShell.h"
#include "nsThreadUtils.h"

static UINT sMsgId;

//-------------------------------------------------------------------------

MRESULT EXPENTRY EventWindowProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
  if (msg == sMsgId) {
    nsAppShell *as = reinterpret_cast<nsAppShell *>(mp2);
    as->NativeEventCallback();
    NS_RELEASE(as);
    return (MRESULT)TRUE;
  }
  return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

nsAppShell::~nsAppShell()
{
  if (mEventWnd) {
    // DestroyWindow doesn't do anything when called from a non UI thread.
    // Since mEventWnd was created on the UI thread, it must be destroyed on
    // the UI thread.
    WinSendMsg(mEventWnd, WM_CLOSE, 0, 0);
  }
}

nsresult
nsAppShell::Init()
{
  // a message queue is required to create a window but
  // it is not necessarily created yet
  if (WinQueryQueueInfo(HMQ_CURRENT, nullptr, 0) == FALSE) {
    // Set our app to be a PM app before attempting Win calls
    PPIB ppib;
    PTIB ptib;
    DosGetInfoBlocks(&ptib, &ppib);
    ppib->pib_ultype = 3;

    HAB hab = WinInitialize(0);
    WinCreateMsgQueue(hab, 0);
  }

  if (!sMsgId) {
    sMsgId = WinAddAtom( WinQuerySystemAtomTable(), "nsAppShell:EventID");
    WinRegisterClass((HAB)0, "nsAppShell:EventWindowClass", EventWindowProc, 0, 0);
  }

  mEventWnd = ::WinCreateWindow(HWND_DESKTOP,
                                "nsAppShell:EventWindowClass",
                                "nsAppShell:EventWindow",
                                0,
                                0, 0,
                                10, 10,
                                HWND_DESKTOP,
                                HWND_BOTTOM,
                                0, 0, 0);
  NS_ENSURE_STATE(mEventWnd);

  return nsBaseAppShell::Init();
}

void
nsAppShell::DoProcessMoreGeckoEvents()
{
  // Called by nsBaseAppShell's NativeEventCallback() after it has finished
  // processing pending gecko events and there are still gecko events pending
  // for the thread. (This can happen if NS_ProcessPendingEvents reached it's
  // starvation timeout limit.) The default behavior in nsBaseAppShell is to
  // call ScheduleNativeEventCallback to post a follow up native event callback
  // message. This triggers an additional call to NativeEventCallback for more
  // gecko event processing.

  // There's a deadlock risk here with certain internal Windows modal loops. In
  // our dispatch code, we prioritize messages so that input is handled first.
  // However Windows modal dispatch loops often prioritize posted messages. If
  // we find ourselves in a tight gecko timer loop where NS_ProcessPendingEvents
  // takes longer than the timer duration, NS_HasPendingEvents(thread) will
  // always be true. ScheduleNativeEventCallback will be called on every
  // NativeEventCallback callback, and in a Windows modal dispatch loop, the
  // callback message will be processed first -> input gets starved, dead lock.

  // To avoid, don't post native callback messages from NativeEventCallback
  // when we're in a modal loop. This gets us back into the Windows modal
  // dispatch loop dispatching input messages. Once we drop out of the modal
  // loop, we use mNativeCallbackPending to fire off a final NativeEventCallback
  // if we need it, which insures NS_ProcessPendingEvents gets called and all
  // gecko events get processed.
  if (mEventloopNestingLevel < 2) {
    OnDispatchedEvent(nullptr);
    mNativeCallbackPending = false;
  } else {
    mNativeCallbackPending = true;
  }
}

void
nsAppShell::ScheduleNativeEventCallback()
{
  // Post a message to the hidden message window
  NS_ADDREF_THIS(); // will be released when the event is processed
  WinPostMsg(mEventWnd, sMsgId, 0, reinterpret_cast<MPARAM>(this));
}

bool
nsAppShell::ProcessNextNativeEvent(bool mayWait)
{
  bool gotMessage = false;

  do {
    QMSG qmsg;
    // Give priority to system messages (in particular keyboard, mouse, timer,
    // and paint messages).
    if (WinPeekMsg((HAB)0, &qmsg, NULLHANDLE, WM_CHAR, WM_VIOCHAR, PM_REMOVE) ||
        WinPeekMsg((HAB)0, &qmsg, NULLHANDLE, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE) || 
        WinPeekMsg((HAB)0, &qmsg, NULLHANDLE, 0, WM_USER-1, PM_REMOVE) || 
        WinPeekMsg((HAB)0, &qmsg, NULLHANDLE, 0, 0, PM_REMOVE)) {
      gotMessage = true;
      ::WinDispatchMsg((HAB)0, &qmsg);
    } else if (mayWait) {
      // Block and wait for any posted application message
      ::WinWaitMsg((HAB)0, 0, 0);
    }
  } while (!gotMessage && mayWait);

  // See DoProcessNextNativeEvent, mEventloopNestingLevel will be
  // one when a modal loop unwinds.
  if (mNativeCallbackPending && mEventloopNestingLevel == 1)
    DoProcessMoreGeckoEvents();

  return gotMessage;
}
