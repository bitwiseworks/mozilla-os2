/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8 -*- */
/* vim: set sw=4 ts=8 et tw=80 ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_plugins_NPEventOS2_h
#define mozilla_dom_plugins_NPEventOS2_h 1


#include "npapi.h"
namespace mozilla {

namespace plugins {

// We use an NPRemoteEvent struct so that we can store the extra data on
// the stack so that we don't need to worry about managing the memory.
struct NPRemoteEvent
{
    NPEvent event;
    union {
        SWP swp;
    } paramData;
};

}

}

namespace IPC {

template <>
struct ParamTraits<mozilla::plugins::NPRemoteEvent>
{
    typedef mozilla::plugins::NPRemoteEvent paramType;

    static void Write(Message* aMsg, const paramType& aParam)
    {
        // Make a non-const copy of aParam so that we can muck with
        // its insides for tranport
        paramType paramCopy;

        paramCopy.event = aParam.event;

        // We can't blindly ipc events because they may sometimes contain
        // pointers to memory in the sending process.
        switch (paramCopy.event.event) {
            case WM_WINDOWPOSCHANGED:
                // The wParam parameter of WM_WINDOWPOSCHANGED holds a pointer to
                // a SWP structure that contains information about the
                // window's new size and position
                paramCopy.paramData.swp = *(reinterpret_cast<SWP*>(paramCopy.event.wParam));
                break;

            // the white list of events that we will ipc to the client
            case WM_PAINT:

            case WM_CHAR:

            case WM_CONTEXTMENU:

            case 0x041E: // WM_MOUSEENTER:
            case 0x041F: // WM_MOUSELEAVE:
            case WM_MOUSEMOVE:
            case WM_BUTTON1DOWN:
            case WM_BUTTON2DOWN:
            case WM_BUTTON3DOWN:
            case WM_BUTTON1UP:
            case WM_BUTTON2UP:
            case WM_BUTTON3UP:
            case WM_BUTTON1DBLCLK:
            case WM_BUTTON2DBLCLK:
            case WM_BUTTON3DBLCLK:

            case WM_SETFOCUS:
            case 0x000E: // WM_FOCUSCHANGED:
                break;

            default:
                // RegisterWindowMessage events should be passed.
                if (paramCopy.event.event >= 0xC000 && paramCopy.event.event <= 0xFFFF)
                    break;

                // FIXME/bug 567465: temporarily work around unhandled
                // events by forwarding a "dummy event".  The eventual
                // fix will be to stop trying to send these events
                // entirely.
                paramCopy.event.event = WM_NULL;
                break;
        }

        aMsg->WriteBytes(&paramCopy, sizeof(paramType));
    }

    static bool Read(const Message* aMsg, void** aIter, paramType* aResult)
    {
        const char* bytes = 0;

        if (!aMsg->ReadBytes(aIter, &bytes, sizeof(paramType))) {
            return false;
        }
        memcpy(aResult, bytes, sizeof(paramType));

        if (aResult->event.event == WM_WINDOWPOSCHANGED) {
            // restore the wParam to point at the SWP
            aResult->event.wParam = reinterpret_cast<uint32_t>(&aResult->paramData.swp);
        }

        return true;
    }

    static void Log(const paramType& aParam, std::wstring* aLog)
    {
        aLog->append(L"(PMEvent)");
    }

};

} // namespace IPC

#endif // ifndef mozilla_dom_plugins_NPEventOS2_h
