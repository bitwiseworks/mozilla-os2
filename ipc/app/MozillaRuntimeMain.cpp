/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if defined(XP_OS2)
// exceptq trap file generator
#include <string.h>
#define INCL_BASE
#include <os2.h>
#define INCL_LOADEXCEPTQ
#include <exceptq.h>
#endif

#include "nsXPCOM.h"
#include "nsXULAppAPI.h"

// FIXME/cjones testing
#if !defined(OS_WIN)
#include <unistd.h>
#endif

#ifdef XP_WIN
#include <windows.h>
// we want a wmain entry point
// but we don't want its DLL load protection, because we'll handle it here
#define XRE_DONT_PROTECT_DLL_LOAD
#include "nsWindowsWMain.cpp"

#include "nsSetDllDirectory.h"
#endif

#if defined(XP_OS2)
// Stack-based exceptq handler installation wrapper whose only function is to automatically
// uninstall the handler when it leaves the scope (e.g. upon early return from a function)
class ScopedExceptqLoader
{
public:
  ScopedExceptqLoader(const char *mode = NULL, const char *appInfoString = NULL)
  {
    LoadExceptq(&xcptRegRec, mode, appInfoString);
  }
  ~ScopedExceptqLoader()
  {
    UninstallExceptq(&xcptRegRec);
  }
private:
  EXCEPTIONREGISTRATIONRECORD xcptRegRec;
};
#endif

int
main(int argc, char* argv[])
{
#if defined(XP_OS2)
    ScopedExceptqLoader exceptq;
#endif

#if defined(XP_WIN) && defined(DEBUG_bent)
    MessageBox(NULL, L"Hi", L"Hi", MB_OK);
#endif

    // Check for the absolute minimum number of args we need to move
    // forward here. We expect the last arg to be the child process type.
    if (argc < 1)
      return 1;
    GeckoProcessType proctype = XRE_StringToChildProcessType(argv[--argc]);

#ifdef XP_WIN
    // For plugins, this is done in PluginProcessChild::Init, as we need to
    // avoid it for unsupported plugins.  See PluginProcessChild::Init for
    // the details.
    if (proctype != GeckoProcessType_Plugin) {
        mozilla::SanitizeEnvironmentVariables();
        SetDllDirectory(L"");
    }
#endif

    nsresult rv = XRE_InitChildProcess(argc, argv, proctype);
    NS_ENSURE_SUCCESS(rv, 1);

    return 0;
}
