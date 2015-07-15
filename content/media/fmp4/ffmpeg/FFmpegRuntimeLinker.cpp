/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if XP_OS2
#define INCL_DOSERRORS
#define INCL_DOSMODULEMGR
#include <os2.h>
#else
#include <dlfcn.h>
#endif

#include "nsDebug.h"

#include "FFmpegRuntimeLinker.h"

// For FFMPEG_LOG
#include "FFmpegDecoderModule.h"

#define NUM_ELEMENTS(X) (sizeof(X) / sizeof((X)[0]))

#define LIBAVCODEC 0
#define LIBAVFORMAT 1
#define LIBAVUTIL 2

namespace mozilla
{

FFmpegRuntimeLinker::LinkStatus FFmpegRuntimeLinker::sLinkStatus =
  LinkStatus_INIT;

#ifdef XP_OS2
static const char * const sLibNames[] = {
  "avcode53.dll", "avform53.dll", "avutil51.dll",
};
#else
static const char * const sLibNames[] = {
  "libavcodec.so.53", "libavformat.so.53", "libavutil.so.51",
};
#endif

void* FFmpegRuntimeLinker::sLinkedLibs[NUM_ELEMENTS(sLibNames)] = {
  nullptr, nullptr, nullptr
};

#define AV_FUNC(lib, func) typeof(func) func;
#include "FFmpegFunctionList.h"
#undef AV_FUNC

/* static */ bool
FFmpegRuntimeLinker::Link()
{
  if (sLinkStatus) {
    return sLinkStatus == LinkStatus_SUCCEEDED;
  }

#ifdef XP_OS2
  APIRET arc = 0;
  char buf[CCHMAXPATH];
  for (uint32_t i = 0; i < NUM_ELEMENTS(sLinkedLibs); i++) {
    *buf = '\0';
    if ((arc = DosLoadModule(buf, sizeof(buf), sLibNames[i],
                             (PHMODULE)&sLinkedLibs[i])) != NO_ERROR) {
      NS_WARNING("Couldn't dynamically load ffmpeg libraries. Reason:");
      NS_WARNING((*buf ? buf | "unknown"));
      goto fail;
    }
  }

#define AV_FUNC(lib, func)                                                     \
  arc = DosQueryProcAddr((HMODULE)sLinkedLibs[lib], 0, "_" #func, (PPFN)&func);\
  if (arc != NO_ERROR) {                                                       \
    NS_WARNING("Couldn't load FFmpeg function _" #func ".");                   \
    goto fail;                                                                 \
  }
#include "FFmpegFunctionList.h"
#undef AV_FUNC
#else // XP_OS2
  for (uint32_t i = 0; i < NUM_ELEMENTS(sLinkedLibs); i++) {
    if (!(sLinkedLibs[i] = dlopen(sLibNames[i], RTLD_NOW | RTLD_LOCAL))) {
      NS_WARNING("Couldn't link ffmpeg libraries.");
      goto fail;
    }
  }

#define AV_FUNC(lib, func)                                                     \
  func = (typeof(func))dlsym(sLinkedLibs[lib], #func);                         \
  if (!func) {                                                                 \
    NS_WARNING("Couldn't load FFmpeg function " #func ".");                    \
    goto fail;                                                                 \
  }
#include "FFmpegFunctionList.h"
#undef AV_FUNC
#endif // XP_OS2

  sLinkStatus = LinkStatus_SUCCEEDED;
  return true;

fail:
  Unlink();

  sLinkStatus = LinkStatus_FAILED;
  return false;
}

/* static */ void
FFmpegRuntimeLinker::Unlink()
{
  FFMPEG_LOG("Unlinking ffmpeg libraries.");
  for (uint32_t i = 0; i < NUM_ELEMENTS(sLinkedLibs); i++) {
    if (sLinkedLibs[i]) {
#ifdef XP_OS2
      DosFreeModule((HMODULE)sLinkedLibs[i]);
#else
      dlclose(sLinkedLibs[i]);
#endif
      sLinkedLibs[i] = nullptr;
    }
  }
}

} // namespace mozilla
