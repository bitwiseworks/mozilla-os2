/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <dlfcn.h>

#include "FFmpegRuntimeLinker.h"
#include "mozilla/ArrayUtils.h"
#include "FFmpegLog.h"

namespace mozilla
{

FFmpegRuntimeLinker::LinkStatus FFmpegRuntimeLinker::sLinkStatus =
  LinkStatus_INIT;

struct AvFormatLib
{
#ifdef XP_OS2
  const char* Name[FFmpegRuntimeLinker::NumDLLs];
#else
  const char* Name;
#endif
  already_AddRefed<PlatformDecoderModule> (*Factory)();
  uint32_t Version;
};

template <int V> class FFmpegDecoderModule
{
public:
  static already_AddRefed<PlatformDecoderModule> Create();
};

static const AvFormatLib sLibs[] = {
#ifdef XP_OS2
  { { "avform56.dll", "avcode56.dll", "avutil54.dll" }, FFmpegDecoderModule<55>::Create, 55 },
  { { "avform55.dll", "avcode55.dll", "avutil53.dll" }, FFmpegDecoderModule<55>::Create, 55 },
  { { "avform53.dll", "avcode53.dll", "avutil51.dll" }, FFmpegDecoderModule<53>::Create, 53 },
#else
  { "libavformat.so.56", FFmpegDecoderModule<55>::Create, 55 },
  { "libavformat.so.55", FFmpegDecoderModule<55>::Create, 55 },
  { "libavformat.so.54", FFmpegDecoderModule<54>::Create, 54 },
  { "libavformat.so.53", FFmpegDecoderModule<53>::Create, 53 },
  { "libavformat.56.dylib", FFmpegDecoderModule<55>::Create, 55 },
  { "libavformat.55.dylib", FFmpegDecoderModule<55>::Create, 55 },
  { "libavformat.54.dylib", FFmpegDecoderModule<54>::Create, 54 },
  { "libavformat.53.dylib", FFmpegDecoderModule<53>::Create, 53 },
#endif
};

#ifdef XP_OS2
void* FFmpegRuntimeLinker::sLinkedLib[NumDLLs] = { nullptr };
#else
void* FFmpegRuntimeLinker::sLinkedLib = nullptr;
#endif
const AvFormatLib* FFmpegRuntimeLinker::sLib = nullptr;

#define AV_FUNC(func, ver) void (*func)();
#define LIBAVCODEC_ALLVERSION
#include "FFmpegFunctionList.h"
#undef LIBAVCODEC_ALLVERSION
#undef AV_FUNC

/* static */ bool
FFmpegRuntimeLinker::Link()
{
  if (sLinkStatus) {
    return sLinkStatus == LinkStatus_SUCCEEDED;
  }

  for (size_t i = 0; i < ArrayLength(sLibs); i++) {
    const AvFormatLib* lib = &sLibs[i];
#ifdef XP_OS2
    for (size_t j = 0; j < NumDLLs; j++) {
      sLinkedLib[j] = dlopen(lib->Name[j], RTLD_NOW | RTLD_LOCAL);
      if (!sLinkedLib[j])
        break;
    }
    if (sLinkedLib[NumDLLs - 1]) {
      if (Bind(lib->Name, lib->Version)) {
        sLib = lib;
        sLinkStatus = LinkStatus_SUCCEEDED;
        return true;
      }
    }
    // Shouldn't happen but if it does then we try the next lib.
    Unlink();
#else
    sLinkedLib = dlopen(lib->Name, RTLD_NOW | RTLD_LOCAL);
    if (sLinkedLib) {
      if (Bind(lib->Name, lib->Version)) {
        sLib = lib;
        sLinkStatus = LinkStatus_SUCCEEDED;
        return true;
      }
      // Shouldn't happen but if it does then we try the next lib.
      Unlink();
    }
#endif
  }

  FFMPEG_LOG("H264/AAC codecs unsupported without [");
  for (size_t i = 0; i < ArrayLength(sLibs); i++) {
#ifdef XP_OS2
    for (size_t j = 0; j < NumDLLs; j++)
      FFMPEG_LOG("%s %s", j ? "," : "", sLibs[i].Name[j]);
#else
    FFMPEG_LOG("%s %s", i ? "," : "", sLibs[i].Name);
#endif
  }
  FFMPEG_LOG(" ]\n");

  Unlink();

  sLinkStatus = LinkStatus_FAILED;
  return false;
}

/* static */ bool
#ifdef XP_OS2
FFmpegRuntimeLinker::Bind(const char* const aLibName[NumDLLs], uint32_t Version)
{
#define LIBAVCODEC_ALLVERSION
#define AV_FUNC(func, ver)                                                     \
  if (ver == 0 || ver == Version) {                                            \
    for (size_t j = 0; j < NumDLLs; j++) {                                     \
      if ((func = (typeof(func))dlsym(sLinkedLib[j], "_" #func)))              \
        break;                                                                 \
    }                                                                          \
    if (!func) {                                                               \
      FFMPEG_LOG("Couldn't load function _" #func " from:");                   \
      for (size_t j = 0; j < NumDLLs; j++)                                     \
        FFMPEG_LOG("%s %s", j ? "," : "", aLibName[j]);                        \
      return false;                                                            \
    }                                                                          \
  }
#else
FFmpegRuntimeLinker::Bind(const char* aLibName, uint32_t Version)
{
#define LIBAVCODEC_ALLVERSION
#define AV_FUNC(func, ver)                                                     \
  if (ver == 0 || ver == Version) {                                            \
    if (!(func = (typeof(func))dlsym(sLinkedLib, #func))) {                    \
      FFMPEG_LOG("Couldn't load function " #func " from %s.", aLibName);       \
      return false;                                                            \
    }                                                                          \
  }
#endif
#include "FFmpegFunctionList.h"
#undef AV_FUNC
#undef LIBAVCODEC_ALLVERSION
  return true;
}

/* static */ already_AddRefed<PlatformDecoderModule>
FFmpegRuntimeLinker::CreateDecoderModule()
{
  if (!Link()) {
    return nullptr;
  }
  nsRefPtr<PlatformDecoderModule> module = sLib->Factory();
  return module.forget();
}

/* static */ void
FFmpegRuntimeLinker::Unlink()
{
#ifdef XP_OS2
  for (size_t j = 0; j < NumDLLs; j++) {
    if (sLinkedLib[j]) {
      dlclose(sLinkedLib[j]);
      sLinkedLib[j] = nullptr;
    }
  }
  sLib = nullptr;
  sLinkStatus = LinkStatus_INIT;
#else
  if (sLinkedLib) {
    dlclose(sLinkedLib);
    sLinkedLib = nullptr;
    sLib = nullptr;
    sLinkStatus = LinkStatus_INIT;
  }
#endif
}

} // namespace mozilla
