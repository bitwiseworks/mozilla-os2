/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifdef MOZ_WIDGET_GTK
#include <glib.h>
#elif XP_MACOSX
#include "PluginInterposeOSX.h"
#include "PluginUtilsOSX.h"
#endif
#ifdef MOZ_WIDGET_QT
#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include "NestedLoopTimer.h"
#endif

#if defined(XP_OS2)
#define INCL_DOS
#define INCL_PM
#include <os2.h>
#endif

#include "base/process_util.h"

#include "mozilla/Preferences.h"
#include "mozilla/unused.h"
#include "mozilla/ipc/SyncChannel.h"
#include "mozilla/plugins/PluginModuleParent.h"
#include "mozilla/plugins/BrowserStreamParent.h"
#include "mozilla/dom/PCrashReporterParent.h"
#include "PluginIdentifierParent.h"

#include "nsAutoPtr.h"
#include "nsCRT.h"
#include "nsNPAPIPlugin.h"
#include "nsIFile.h"
#include "nsPrintfCString.h"

#include "prsystem.h"

#ifdef XP_WIN
#include "mozilla/widget/AudioSession.h"
#endif
#include "sampler.h"

using base::KillProcess;

using mozilla::PluginLibrary;
using mozilla::ipc::SyncChannel;
using mozilla::dom::PCrashReporterParent;
using mozilla::dom::CrashReporterParent;

using namespace mozilla;
using namespace mozilla::plugins;
using namespace mozilla::plugins::parent;

#ifdef MOZ_CRASHREPORTER
#include "mozilla/dom/CrashReporterParent.h"

using namespace CrashReporter;
#endif

static const char kChildTimeoutPref[] = "dom.ipc.plugins.timeoutSecs";
static const char kParentTimeoutPref[] = "dom.ipc.plugins.parentTimeoutSecs";
static const char kLaunchTimeoutPref[] = "dom.ipc.plugins.processLaunchTimeoutSecs";

template<>
struct RunnableMethodTraits<mozilla::plugins::PluginModuleParent>
{
    typedef mozilla::plugins::PluginModuleParent Class;
    static void RetainCallee(Class* obj) { }
    static void ReleaseCallee(Class* obj) { }
};

// static
PluginLibrary*
PluginModuleParent::LoadModule(const char* aFilePath)
{
    PLUGIN_LOG_DEBUG_FUNCTION;

    int32_t prefSecs = Preferences::GetInt(kLaunchTimeoutPref, 0);

    // Block on the child process being launched and initialized.
    nsAutoPtr<PluginModuleParent> parent(new PluginModuleParent(aFilePath));
    bool launched = parent->mSubprocess->Launch(prefSecs * 1000);
    if (!launched) {
        // Need to set this so the destructor doesn't complain.
        parent->mShutdown = true;
        return nullptr;
    }
    parent->Open(parent->mSubprocess->GetChannel(),
                 parent->mSubprocess->GetChildProcessHandle());

    TimeoutChanged(kChildTimeoutPref, parent);

#ifdef MOZ_CRASHREPORTER
    // If this fails, we're having IPC troubles, and we're doomed anyways.
    if (!CrashReporterParent::CreateCrashReporter(parent.get())) {
        parent->mShutdown = true;
        return nullptr;
    }
#endif

    return parent.forget();
}


PluginModuleParent::PluginModuleParent(const char* aFilePath)
    : mSubprocess(new PluginProcessParent(aFilePath))
    , mShutdown(false)
    , mClearSiteDataSupported(false)
    , mGetSitesWithDataSupported(false)
    , mNPNIface(NULL)
    , mPlugin(NULL)
    , mTaskFactory(this)
#ifdef XP_WIN
    , mPluginCpuUsageOnHang()
#endif
#ifdef MOZ_CRASHREPORTER_INJECTOR
    , mFlashProcess1(0)
    , mFlashProcess2(0)
#endif
{
    NS_ASSERTION(mSubprocess, "Out of memory!");

    mIdentifiers.Init();

    Preferences::RegisterCallback(TimeoutChanged, kChildTimeoutPref, this);
    Preferences::RegisterCallback(TimeoutChanged, kParentTimeoutPref, this);
}

PluginModuleParent::~PluginModuleParent()
{
    NS_ASSERTION(OkToCleanup(), "unsafe destruction");

    if (!mShutdown) {
        NS_WARNING("Plugin host deleted the module without shutting down.");
        NPError err;
        NP_Shutdown(&err);
    }
    NS_ASSERTION(mShutdown, "NP_Shutdown didn't");

    if (mSubprocess) {
        mSubprocess->Delete();
        mSubprocess = nullptr;
    }

#ifdef MOZ_CRASHREPORTER_INJECTOR
    if (mFlashProcess1)
        UnregisterInjectorCallback(mFlashProcess1);
    if (mFlashProcess2)
        UnregisterInjectorCallback(mFlashProcess2);
#endif

    Preferences::UnregisterCallback(TimeoutChanged, kChildTimeoutPref, this);
    Preferences::UnregisterCallback(TimeoutChanged, kParentTimeoutPref, this);
}

#ifdef MOZ_CRASHREPORTER
void
PluginModuleParent::WriteExtraDataForMinidump(AnnotationTable& notes)
{
    typedef nsDependentCString CS;

    // Get the plugin filename, try to get just the file leafname
    const std::string& pluginFile = mSubprocess->GetPluginFilePath();
    size_t filePos = pluginFile.rfind(FILE_PATH_SEPARATOR);
    if (filePos == std::string::npos)
        filePos = 0;
    else
        filePos++;
    notes.Put(CS("PluginFilename"), CS(pluginFile.substr(filePos).c_str()));

    //TODO: add plugin name and version: bug 539841
    // (as PluginName, PluginVersion)
    notes.Put(CS("PluginName"), CS(""));
    notes.Put(CS("PluginVersion"), CS(""));

    CrashReporterParent* crashReporter = CrashReporter();
    if (crashReporter) {
        const nsString& hangID = crashReporter->HangID();
        if (!hangID.IsEmpty()) {
            notes.Put(CS("HangID"), NS_ConvertUTF16toUTF8(hangID));
#ifdef XP_WIN
            if (mPluginCpuUsageOnHang.Length() > 0) {
              notes.Put(CS("NumberOfProcessors"),
                        nsPrintfCString("%d", PR_GetNumberOfProcessors()));

              nsCString cpuUsageStr;
              cpuUsageStr.AppendFloat(std::ceil(mPluginCpuUsageOnHang[0] * 100) / 100);
              notes.Put(CS("PluginCpuUsage"), cpuUsageStr);

#ifdef MOZ_CRASHREPORTER_INJECTOR
              for (uint32_t i=1; i<mPluginCpuUsageOnHang.Length(); ++i) {
                nsCString tempStr;
                tempStr.AppendFloat(std::ceil(mPluginCpuUsageOnHang[i] * 100) / 100);
                notes.Put(nsPrintfCString("CpuUsageFlashProcess%d", i), tempStr);
              }
#endif
            }
#endif
        }
    }
}
#endif  // MOZ_CRASHREPORTER

int
PluginModuleParent::TimeoutChanged(const char* aPref, void* aModule)
{
    NS_ASSERTION(NS_IsMainThread(), "Wrong thead!");
    if (!strcmp(aPref, kChildTimeoutPref)) {
      // The timeout value used by the parent for children
      int32_t timeoutSecs = Preferences::GetInt(kChildTimeoutPref, 0);
      int32 timeoutMs = (timeoutSecs > 0) ? (1000 * timeoutSecs) :
                        SyncChannel::kNoTimeout;
      static_cast<PluginModuleParent*>(aModule)->SetReplyTimeoutMs(timeoutMs);
    } else if (!strcmp(aPref, kParentTimeoutPref)) {
      // The timeout value used by the child for its parent
      int32_t timeoutSecs = Preferences::GetInt(kParentTimeoutPref, 0);
      unused << static_cast<PluginModuleParent*>(aModule)->SendSetParentHangTimeout(timeoutSecs);
    }
    return 0;
}

void
PluginModuleParent::CleanupFromTimeout()
{
    if (!mShutdown && OkToCleanup())
        Close();
}

#ifdef XP_WIN
namespace {

uint64_t
FileTimeToUTC(const FILETIME& ftime) 
{
  ULARGE_INTEGER li;
  li.LowPart = ftime.dwLowDateTime;
  li.HighPart = ftime.dwHighDateTime;
  return li.QuadPart;
}

struct CpuUsageSamples
{
  uint64_t sampleTimes[2];
  uint64_t cpuTimes[2];
};

bool 
GetProcessCpuUsage(const InfallibleTArray<base::ProcessHandle>& processHandles, InfallibleTArray<float>& cpuUsage)
{
  InfallibleTArray<CpuUsageSamples> samples(processHandles.Length());
  FILETIME creationTime, exitTime, kernelTime, userTime, currentTime;
  BOOL res;

  for (uint32_t i = 0; i < processHandles.Length(); ++i) {
    ::GetSystemTimeAsFileTime(&currentTime);
    res = ::GetProcessTimes(processHandles[i], &creationTime, &exitTime, &kernelTime, &userTime);
    if (!res) {
      NS_WARNING("failed to get process times");
      return false;
    }
  
    CpuUsageSamples s;
    s.sampleTimes[0] = FileTimeToUTC(currentTime);
    s.cpuTimes[0]    = FileTimeToUTC(kernelTime) + FileTimeToUTC(userTime);
    samples.AppendElement(s);
  }

  // we already hung for a while, a little bit longer won't matter
  ::Sleep(50);

  const int32_t numberOfProcessors = PR_GetNumberOfProcessors();

  for (uint32_t i = 0; i < processHandles.Length(); ++i) {
    ::GetSystemTimeAsFileTime(&currentTime);
    res = ::GetProcessTimes(processHandles[i], &creationTime, &exitTime, &kernelTime, &userTime);
    if (!res) {
      NS_WARNING("failed to get process times");
      return false;
    }

    samples[i].sampleTimes[1] = FileTimeToUTC(currentTime);
    samples[i].cpuTimes[1]    = FileTimeToUTC(kernelTime) + FileTimeToUTC(userTime);    

    const uint64_t deltaSampleTime = samples[i].sampleTimes[1] - samples[i].sampleTimes[0];
    const uint64_t deltaCpuTime    = samples[i].cpuTimes[1]    - samples[i].cpuTimes[0];
    const float usage = 100.f * (float(deltaCpuTime) / deltaSampleTime) / numberOfProcessors;
    cpuUsage.AppendElement(usage);
  }

  return true;
}

} // anonymous namespace
#endif // #ifdef XP_WIN

bool
PluginModuleParent::ShouldContinueFromReplyTimeout()
{
#ifdef MOZ_CRASHREPORTER
    CrashReporterParent* crashReporter = CrashReporter();
    if (crashReporter->GeneratePairedMinidump(this)) {
        mBrowserDumpID = crashReporter->ParentDumpID();
        mPluginDumpID = crashReporter->ChildDumpID();
        PLUGIN_LOG_DEBUG(
                ("generated paired browser/plugin minidumps: %s/%s (ID=%s)",
                 NS_ConvertUTF16toUTF8(mBrowserDumpID).get(),
                 NS_ConvertUTF16toUTF8(mPluginDumpID).get(),
                 NS_ConvertUTF16toUTF8(crashReporter->HangID()).get()));
    } else {
        NS_WARNING("failed to capture paired minidumps from hang");
    }
#endif

#ifdef XP_WIN
    // collect cpu usage for plugin processes

    InfallibleTArray<base::ProcessHandle> processHandles;
    base::ProcessHandle handle;

    processHandles.AppendElement(OtherProcess());
#ifdef MOZ_CRASHREPORTER_INJECTOR
    if (mFlashProcess1 && base::OpenProcessHandle(mFlashProcess1, &handle)) {
      processHandles.AppendElement(handle);
    }
    if (mFlashProcess2 && base::OpenProcessHandle(mFlashProcess2, &handle)) {
      processHandles.AppendElement(handle);
    }
#endif

    if (!GetProcessCpuUsage(processHandles, mPluginCpuUsageOnHang)) {
      mPluginCpuUsageOnHang.Clear();
    }
#endif

    // this must run before the error notification from the channel,
    // or not at all
    MessageLoop::current()->PostTask(
        FROM_HERE,
        mTaskFactory.NewRunnableMethod(
            &PluginModuleParent::CleanupFromTimeout));

    if (!KillProcess(OtherProcess(), 1, false))
        NS_WARNING("failed to kill subprocess!");

    return false;
}

#ifdef MOZ_CRASHREPORTER
CrashReporterParent*
PluginModuleParent::CrashReporter()
{
    return static_cast<CrashReporterParent*>(ManagedPCrashReporterParent()[0]);
}

#ifdef MOZ_CRASHREPORTER_INJECTOR
static void
RemoveMinidump(nsIFile* minidump)
{
    if (!minidump)
        return;

    minidump->Remove(false);
    nsCOMPtr<nsIFile> extraFile;
    if (GetExtraFileForMinidump(minidump,
                                getter_AddRefs(extraFile))) {
        extraFile->Remove(true);
    }
}
#endif // MOZ_CRASHREPORTER_INJECTOR

void
PluginModuleParent::ProcessFirstMinidump()
{
    CrashReporterParent* crashReporter = CrashReporter();
    if (!crashReporter)
        return;

    AnnotationTable notes;
    notes.Init(4);
    WriteExtraDataForMinidump(notes);
        
    if (!mPluginDumpID.IsEmpty() && !mBrowserDumpID.IsEmpty()) {
        crashReporter->GenerateHangCrashReport(&notes);
        return;
    }

    uint32_t sequence = PR_UINT32_MAX;
    nsCOMPtr<nsIFile> dumpFile;
    nsCAutoString flashProcessType;
    TakeMinidump(getter_AddRefs(dumpFile), &sequence);

#ifdef MOZ_CRASHREPORTER_INJECTOR
    nsCOMPtr<nsIFile> childDumpFile;
    uint32_t childSequence;

    if (mFlashProcess1 &&
        TakeMinidumpForChild(mFlashProcess1,
                             getter_AddRefs(childDumpFile),
                             &childSequence)) {
        if (childSequence < sequence) {
            RemoveMinidump(dumpFile);
            dumpFile = childDumpFile;
            sequence = childSequence;
            flashProcessType.AssignLiteral("Broker");
        }
        else {
            RemoveMinidump(childDumpFile);
        }
    }
    if (mFlashProcess2 &&
        TakeMinidumpForChild(mFlashProcess2,
                             getter_AddRefs(childDumpFile),
                             &childSequence)) {
        if (childSequence < sequence) {
            RemoveMinidump(dumpFile);
            dumpFile = childDumpFile;
            sequence = childSequence;
            flashProcessType.AssignLiteral("Sandbox");
        }
        else {
            RemoveMinidump(childDumpFile);
        }
    }
#endif

    if (!dumpFile) {
        NS_WARNING("[PluginModuleParent::ActorDestroy] abnormal shutdown without minidump!");
        return;
    }

    PLUGIN_LOG_DEBUG(("got child minidump: %s",
                      NS_ConvertUTF16toUTF8(mPluginDumpID).get()));

    GetIDFromMinidump(dumpFile, mPluginDumpID);
    if (!flashProcessType.IsEmpty()) {
        notes.Put(NS_LITERAL_CSTRING("FlashProcessDump"), flashProcessType);
    }
    crashReporter->GenerateCrashReportForMinidump(dumpFile, &notes);
}
#endif

void
PluginModuleParent::ActorDestroy(ActorDestroyReason why)
{
    switch (why) {
    case AbnormalShutdown: {
#ifdef MOZ_CRASHREPORTER
        ProcessFirstMinidump();
#endif

        mShutdown = true;
        // Defer the PluginCrashed method so that we don't re-enter
        // and potentially modify the actor child list while enumerating it.
        if (mPlugin)
            MessageLoop::current()->PostTask(
                FROM_HERE,
                mTaskFactory.NewRunnableMethod(
                    &PluginModuleParent::NotifyPluginCrashed));
        break;
    }
    case NormalShutdown:
        mShutdown = true;
        break;

    default:
        NS_ERROR("Unexpected shutdown reason for toplevel actor.");
    }
}

void
PluginModuleParent::NotifyPluginCrashed()
{
    if (!OkToCleanup()) {
        // there's still plugin code on the C++ stack.  try again
        MessageLoop::current()->PostDelayedTask(
            FROM_HERE,
            mTaskFactory.NewRunnableMethod(
                &PluginModuleParent::NotifyPluginCrashed), 10);
        return;
    }

    if (mPlugin)
        mPlugin->PluginCrashed(mPluginDumpID, mBrowserDumpID);
}

PPluginIdentifierParent*
PluginModuleParent::AllocPPluginIdentifier(const nsCString& aString,
                                           const int32_t& aInt,
                                           const bool& aTemporary)
{
    if (aTemporary) {
        NS_ERROR("Plugins don't create temporary identifiers.");
        return NULL; // should abort the plugin
    }

    NPIdentifier npident = aString.IsVoid() ?
        mozilla::plugins::parent::NPN_getintidentifier(aInt) :
        mozilla::plugins::parent::NPN_getstringidentifier(aString.get());

    if (!npident) {
        NS_WARNING("Failed to get identifier!");
        return nullptr;
    }

    PluginIdentifierParent* ident = new PluginIdentifierParent(npident, false);
    mIdentifiers.Put(npident, ident);
    return ident;
}

bool
PluginModuleParent::DeallocPPluginIdentifier(PPluginIdentifierParent* aActor)
{
    delete aActor;
    return true;
}

PPluginInstanceParent*
PluginModuleParent::AllocPPluginInstance(const nsCString& aMimeType,
                                         const uint16_t& aMode,
                                         const InfallibleTArray<nsCString>& aNames,
                                         const InfallibleTArray<nsCString>& aValues,
                                         NPError* rv)
{
    NS_ERROR("Not reachable!");
    return NULL;
}

bool
PluginModuleParent::DeallocPPluginInstance(PPluginInstanceParent* aActor)
{
    PLUGIN_LOG_DEBUG_METHOD;
    delete aActor;
    return true;
}

void
PluginModuleParent::SetPluginFuncs(NPPluginFuncs* aFuncs)
{
    aFuncs->version = (NP_VERSION_MAJOR << 8) | NP_VERSION_MINOR;
    aFuncs->javaClass = nullptr;

    // Gecko should always call these functions through a PluginLibrary object.
    aFuncs->newp = NULL;
    aFuncs->clearsitedata = NULL;
    aFuncs->getsiteswithdata = NULL;

    aFuncs->destroy = NPP_Destroy;
    aFuncs->setwindow = NPP_SetWindow;
    aFuncs->newstream = NPP_NewStream;
    aFuncs->destroystream = NPP_DestroyStream;
    aFuncs->asfile = NPP_StreamAsFile;
    aFuncs->writeready = NPP_WriteReady;
    aFuncs->write = NPP_Write;
    aFuncs->print = NPP_Print;
    aFuncs->event = NPP_HandleEvent;
    aFuncs->urlnotify = NPP_URLNotify;
    aFuncs->getvalue = NPP_GetValue;
    aFuncs->setvalue = NPP_SetValue;
    aFuncs->gotfocus = NULL;
    aFuncs->lostfocus = NULL;
    aFuncs->urlredirectnotify = NULL;

    // Provide 'NPP_URLRedirectNotify', 'NPP_ClearSiteData', and
    // 'NPP_GetSitesWithData' functionality if it is supported by the plugin.
    bool urlRedirectSupported = false;
    unused << CallOptionalFunctionsSupported(&urlRedirectSupported,
                                             &mClearSiteDataSupported,
                                             &mGetSitesWithDataSupported);
    if (urlRedirectSupported) {
      aFuncs->urlredirectnotify = NPP_URLRedirectNotify;
    }
}

NPError NP_CALLBACK
PluginModuleParent::NPP_Destroy(NPP instance,
                                NPSavedData** /*saved*/)
{
    // FIXME/cjones:
    //  (1) send a "destroy" message to the child
    //  (2) the child shuts down its instance
    //  (3) remove both parent and child IDs from map
    //  (4) free parent
    PLUGIN_LOG_DEBUG_FUNCTION;

    PluginInstanceParent* parentInstance =
        static_cast<PluginInstanceParent*>(instance->pdata);

    if (!parentInstance)
        return NPERR_NO_ERROR;

    NPError retval = parentInstance->Destroy();
    instance->pdata = nullptr;

    unused << PluginInstanceParent::Call__delete__(parentInstance);
    return retval;
}

NPError NP_CALLBACK
PluginModuleParent::NPP_NewStream(NPP instance, NPMIMEType type,
                                  NPStream* stream, NPBool seekable,
                                  uint16_t* stype)
{
    SAMPLE_LABEL("PluginModuleParent", "NPP_NewStream");
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return NPERR_GENERIC_ERROR;

    return i->NPP_NewStream(type, stream, seekable,
                            stype);
}

NPError NP_CALLBACK
PluginModuleParent::NPP_SetWindow(NPP instance, NPWindow* window)
{
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return NPERR_GENERIC_ERROR;

    return i->NPP_SetWindow(window);
}

NPError NP_CALLBACK
PluginModuleParent::NPP_DestroyStream(NPP instance,
                                      NPStream* stream,
                                      NPReason reason)
{
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return NPERR_GENERIC_ERROR;

    return i->NPP_DestroyStream(stream, reason);
}

int32_t NP_CALLBACK
PluginModuleParent::NPP_WriteReady(NPP instance,
                                   NPStream* stream)
{
    BrowserStreamParent* s = StreamCast(instance, stream);
    if (!s)
        return -1;

    return s->WriteReady();
}

int32_t NP_CALLBACK
PluginModuleParent::NPP_Write(NPP instance,
                              NPStream* stream,
                              int32_t offset,
                              int32_t len,
                              void* buffer)
{
    BrowserStreamParent* s = StreamCast(instance, stream);
    if (!s)
        return -1;

    return s->Write(offset, len, buffer);
}

void NP_CALLBACK
PluginModuleParent::NPP_StreamAsFile(NPP instance,
                                     NPStream* stream,
                                     const char* fname)
{
    BrowserStreamParent* s = StreamCast(instance, stream);
    if (!s)
        return;

    s->StreamAsFile(fname);
}

void NP_CALLBACK
PluginModuleParent::NPP_Print(NPP instance, NPPrint* platformPrint)
{
    PluginInstanceParent* i = InstCast(instance);
    if (i)
        i->NPP_Print(platformPrint);
}

int16_t NP_CALLBACK
PluginModuleParent::NPP_HandleEvent(NPP instance, void* event)
{
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return false;

    return i->NPP_HandleEvent(event);
}

void NP_CALLBACK
PluginModuleParent::NPP_URLNotify(NPP instance, const char* url,
                                  NPReason reason, void* notifyData)
{
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return;

    i->NPP_URLNotify(url, reason, notifyData);
}

NPError NP_CALLBACK
PluginModuleParent::NPP_GetValue(NPP instance,
                                 NPPVariable variable, void *ret_value)
{
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return NPERR_GENERIC_ERROR;

    return i->NPP_GetValue(variable, ret_value);
}

NPError NP_CALLBACK
PluginModuleParent::NPP_SetValue(NPP instance, NPNVariable variable,
                                 void *value)
{
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return NPERR_GENERIC_ERROR;

    return i->NPP_SetValue(variable, value);
}

bool
PluginModuleParent::RecvBackUpXResources(const FileDescriptor& aXSocketFd)
{
#ifndef MOZ_X11
    NS_RUNTIMEABORT("This message only makes sense on X11 platforms");
#else
    NS_ABORT_IF_FALSE(0 > mPluginXSocketFdDup.get(),
                      "Already backed up X resources??");
    mPluginXSocketFdDup.forget();
    mPluginXSocketFdDup.reset(aXSocketFd.PlatformHandle());
#endif
    return true;
}

void NP_CALLBACK
PluginModuleParent::NPP_URLRedirectNotify(NPP instance, const char* url,
                                          int32_t status, void* notifyData)
{
  PluginInstanceParent* i = InstCast(instance);
  if (!i)
    return;

  i->NPP_URLRedirectNotify(url, status, notifyData);
}

bool
PluginModuleParent::AnswerNPN_UserAgent(nsCString* userAgent)
{
    *userAgent = NullableString(mNPNIface->uagent(nullptr));
    return true;
}

PluginIdentifierParent*
PluginModuleParent::GetIdentifierForNPIdentifier(NPP npp, NPIdentifier aIdentifier)
{
    PluginIdentifierParent* ident;
    if (mIdentifiers.Get(aIdentifier, &ident)) {
        if (ident->IsTemporary()) {
            ident->AddTemporaryRef();
        }
        return ident;
    }

    nsCString string;
    int32_t intval = -1;
    bool temporary = false;
    if (mozilla::plugins::parent::NPN_identifierisstring(aIdentifier)) {
        NPUTF8* chars =
            mozilla::plugins::parent::NPN_utf8fromidentifier(aIdentifier);
        if (!chars) {
            return nullptr;
        }
        string.Adopt(chars);
        temporary = !NPStringIdentifierIsPermanent(npp, aIdentifier);
    }
    else {
        intval = mozilla::plugins::parent::NPN_intfromidentifier(aIdentifier);
        string.SetIsVoid(true);
    }

    ident = new PluginIdentifierParent(aIdentifier, temporary);
    if (!SendPPluginIdentifierConstructor(ident, string, intval, temporary))
        return nullptr;

    if (!temporary) {
        mIdentifiers.Put(aIdentifier, ident);
    }
    return ident;
}

PluginInstanceParent*
PluginModuleParent::InstCast(NPP instance)
{
    PluginInstanceParent* ip =
        static_cast<PluginInstanceParent*>(instance->pdata);

    // If the plugin crashed and the PluginInstanceParent was deleted,
    // instance->pdata will be NULL.
    if (!ip)
        return NULL;

    if (instance != ip->mNPP) {
        NS_RUNTIMEABORT("Corrupted plugin data.");
    }
    return ip;
}

BrowserStreamParent*
PluginModuleParent::StreamCast(NPP instance,
                               NPStream* s)
{
    PluginInstanceParent* ip = InstCast(instance);
    if (!ip)
        return NULL;

    BrowserStreamParent* sp =
        static_cast<BrowserStreamParent*>(static_cast<AStream*>(s->pdata));
    if (sp->mNPP != ip || s != sp->mStream) {
        NS_RUNTIMEABORT("Corrupted plugin stream data.");
    }
    return sp;
}

bool
PluginModuleParent::HasRequiredFunctions()
{
    return true;
}

nsresult
PluginModuleParent::AsyncSetWindow(NPP instance, NPWindow* window)
{
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return NS_ERROR_FAILURE;

    return i->AsyncSetWindow(window);
}

#if defined(MOZ_WIDGET_QT) && (MOZ_PLATFORM_MAEMO == 6)
nsresult
PluginModuleParent::HandleGUIEvent(NPP instance,
                                   const nsGUIEvent& anEvent,
                                   bool* handled)
{
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return NS_ERROR_FAILURE;

    return i->HandleGUIEvent(anEvent, handled);
}
#endif

nsresult
PluginModuleParent::GetImageContainer(NPP instance,
                             mozilla::layers::ImageContainer** aContainer)
{
    PluginInstanceParent* i = InstCast(instance);
    return !i ? NS_ERROR_FAILURE : i->GetImageContainer(aContainer);
}

nsresult
PluginModuleParent::GetImageSize(NPP instance,
                                 nsIntSize* aSize)
{
    PluginInstanceParent* i = InstCast(instance);
    return !i ? NS_ERROR_FAILURE : i->GetImageSize(aSize);
}

nsresult
PluginModuleParent::SetBackgroundUnknown(NPP instance)
{
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return NS_ERROR_FAILURE;

    return i->SetBackgroundUnknown();
}

nsresult
PluginModuleParent::BeginUpdateBackground(NPP instance,
                                          const nsIntRect& aRect,
                                          gfxContext** aCtx)
{
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return NS_ERROR_FAILURE;

    return i->BeginUpdateBackground(aRect, aCtx);
}

nsresult
PluginModuleParent::EndUpdateBackground(NPP instance,
                                        gfxContext* aCtx,
                                        const nsIntRect& aRect)
{
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return NS_ERROR_FAILURE;

    return i->EndUpdateBackground(aCtx, aRect);
}

#if defined(XP_UNIX) && !defined(XP_MACOSX) && !defined(MOZ_WIDGET_GONK)
nsresult
PluginModuleParent::NP_Initialize(NPNetscapeFuncs* bFuncs, NPPluginFuncs* pFuncs, NPError* error)
{
    PLUGIN_LOG_DEBUG_METHOD;

    mNPNIface = bFuncs;

    if (mShutdown) {
        *error = NPERR_GENERIC_ERROR;
        return NS_ERROR_FAILURE;
    }

    uint32_t flags = 0;

    if (!CallNP_Initialize(flags, error)) {
        return NS_ERROR_FAILURE;
    }
    else if (*error != NPERR_NO_ERROR) {
        return NS_OK;
    }

    SetPluginFuncs(pFuncs);

    return NS_OK;
}
#else
nsresult
PluginModuleParent::NP_Initialize(NPNetscapeFuncs* bFuncs, NPError* error)
{
    PLUGIN_LOG_DEBUG_METHOD;

    mNPNIface = bFuncs;

    if (mShutdown) {
        *error = NPERR_GENERIC_ERROR;
        return NS_ERROR_FAILURE;
    }

    uint32_t flags = 0;
#ifdef XP_WIN
    flags |= kAllowAsyncDrawing;
#endif

    if (!CallNP_Initialize(flags, error))
        return NS_ERROR_FAILURE;

#if defined XP_WIN
    // Send the info needed to join the chrome process's audio session to the
    // plugin process
    nsID id;
    nsString sessionName;
    nsString iconPath;

    if (NS_SUCCEEDED(mozilla::widget::GetAudioSessionData(id, sessionName,
                                                          iconPath)))
        unused << SendSetAudioSessionData(id, sessionName, iconPath);
#endif

#ifdef MOZ_CRASHREPORTER_INJECTOR
    InitializeInjector();
#endif

    return NS_OK;
}
#endif

nsresult
PluginModuleParent::NP_Shutdown(NPError* error)
{
    PLUGIN_LOG_DEBUG_METHOD;

    if (mShutdown) {
        *error = NPERR_GENERIC_ERROR;
        return NS_ERROR_FAILURE;
    }

    bool ok = CallNP_Shutdown(error);

    // if NP_Shutdown() is nested within another RPC call, this will
    // break things.  but lord help us if we're doing that anyway; the
    // plugin dso will have been unloaded on the other side by the
    // CallNP_Shutdown() message
    Close();

    return ok ? NS_OK : NS_ERROR_FAILURE;
}

nsresult
PluginModuleParent::NP_GetMIMEDescription(const char** mimeDesc)
{
    PLUGIN_LOG_DEBUG_METHOD;

    *mimeDesc = "application/x-foobar";
    return NS_OK;
}

nsresult
PluginModuleParent::NP_GetValue(void *future, NPPVariable aVariable,
                                   void *aValue, NPError* error)
{
    PR_LOG(gPluginLog, PR_LOG_WARNING, ("%s Not implemented, requested variable %i", __FUNCTION__,
                                        (int) aVariable));

    //TODO: implement this correctly
    *error = NPERR_GENERIC_ERROR;
    return NS_OK;
}

#if defined(XP_WIN) || defined(XP_MACOSX) || defined(XP_OS2)
nsresult
PluginModuleParent::NP_GetEntryPoints(NPPluginFuncs* pFuncs, NPError* error)
{
    NS_ASSERTION(pFuncs, "Null pointer!");

    // We need to have the child process update its function table
    // here by actually calling NP_GetEntryPoints since the parent's
    // function table can reflect NULL entries in the child's table.
    if (!CallNP_GetEntryPoints(error)) {
        return NS_ERROR_FAILURE;
    }
    else if (*error != NPERR_NO_ERROR) {
        return NS_OK;
    }

    SetPluginFuncs(pFuncs);

    return NS_OK;
}
#endif

nsresult
PluginModuleParent::NPP_New(NPMIMEType pluginType, NPP instance,
                            uint16_t mode, int16_t argc, char* argn[],
                            char* argv[], NPSavedData* saved,
                            NPError* error)
{
    PLUGIN_LOG_DEBUG_METHOD;

    if (mShutdown) {
        *error = NPERR_GENERIC_ERROR;
        return NS_ERROR_FAILURE;
    }

    // create the instance on the other side
    InfallibleTArray<nsCString> names;
    InfallibleTArray<nsCString> values;

    for (int i = 0; i < argc; ++i) {
        names.AppendElement(NullableString(argn[i]));
        values.AppendElement(NullableString(argv[i]));
    }

    PluginInstanceParent* parentInstance =
        new PluginInstanceParent(this, instance,
                                 nsDependentCString(pluginType), mNPNIface);

    if (!parentInstance->Init()) {
        delete parentInstance;
        return NS_ERROR_FAILURE;
    }

    instance->pdata = parentInstance;

    if (!CallPPluginInstanceConstructor(parentInstance,
                                        nsDependentCString(pluginType), mode,
                                        names, values, error)) {
        // |parentInstance| is automatically deleted.
        instance->pdata = nullptr;
        // if IPC is down, we'll get an immediate "failed" return, but
        // without *error being set.  So make sure that the error
        // condition is signaled to nsNPAPIPluginInstance
        if (NPERR_NO_ERROR == *error)
            *error = NPERR_GENERIC_ERROR;
        return NS_ERROR_FAILURE;
    }

    if (*error != NPERR_NO_ERROR) {
        NPP_Destroy(instance, 0);
        return NS_ERROR_FAILURE;
    }

    TimeoutChanged(kParentTimeoutPref, this);
    
    return NS_OK;
}

nsresult
PluginModuleParent::NPP_ClearSiteData(const char* site, uint64_t flags,
                                      uint64_t maxAge)
{
    if (!mClearSiteDataSupported)
        return NS_ERROR_NOT_AVAILABLE;

    NPError result;
    if (!CallNPP_ClearSiteData(NullableString(site), flags, maxAge, &result))
        return NS_ERROR_FAILURE;

    switch (result) {
    case NPERR_NO_ERROR:
        return NS_OK;
    case NPERR_TIME_RANGE_NOT_SUPPORTED:
        return NS_ERROR_PLUGIN_TIME_RANGE_NOT_SUPPORTED;
    case NPERR_MALFORMED_SITE:
        return NS_ERROR_INVALID_ARG;
    default:
        return NS_ERROR_FAILURE;
    }
}

nsresult
PluginModuleParent::NPP_GetSitesWithData(InfallibleTArray<nsCString>& result)
{
    if (!mGetSitesWithDataSupported)
        return NS_ERROR_NOT_AVAILABLE;

    if (!CallNPP_GetSitesWithData(&result))
        return NS_ERROR_FAILURE;

    return NS_OK;
}

#if defined(XP_MACOSX)
nsresult
PluginModuleParent::IsRemoteDrawingCoreAnimation(NPP instance, bool *aDrawing)
{
    PluginInstanceParent* i = InstCast(instance);
    if (!i)
        return NS_ERROR_FAILURE;

    return i->IsRemoteDrawingCoreAnimation(aDrawing);
}
#endif

bool
PluginModuleParent::AnswerNPN_GetValue_WithBoolReturn(const NPNVariable& aVariable,
                                                      NPError* aError,
                                                      bool* aBoolVal)
{
    NPBool boolVal = false;
    *aError = mozilla::plugins::parent::NPN_getvalue(nullptr, aVariable, &boolVal);
    *aBoolVal = boolVal ? true : false;
    return true;
}

#if defined(MOZ_WIDGET_QT)
static const int kMaxtimeToProcessEvents = 30;
bool
PluginModuleParent::AnswerProcessSomeEvents()
{
    PLUGIN_LOG_DEBUG(("Spinning mini nested loop ..."));
    QCoreApplication::processEvents(QEventLoop::AllEvents, kMaxtimeToProcessEvents);

    PLUGIN_LOG_DEBUG(("... quitting mini nested loop"));

    return true;
}

#elif defined(XP_MACOSX)
bool
PluginModuleParent::AnswerProcessSomeEvents()
{
    mozilla::plugins::PluginUtilsOSX::InvokeNativeEventLoop();
    return true;
}

#elif !defined(MOZ_WIDGET_GTK)
bool
PluginModuleParent::AnswerProcessSomeEvents()
{
    NS_RUNTIMEABORT("unreached");
    return false;
}

#else
static const int kMaxChancesToProcessEvents = 20;

bool
PluginModuleParent::AnswerProcessSomeEvents()
{
    PLUGIN_LOG_DEBUG(("Spinning mini nested loop ..."));

    int i = 0;
    for (; i < kMaxChancesToProcessEvents; ++i)
        if (!g_main_context_iteration(NULL, FALSE))
            break;

    PLUGIN_LOG_DEBUG(("... quitting mini nested loop; processed %i tasks", i));

    return true;
}
#endif

bool
PluginModuleParent::RecvProcessNativeEventsInRPCCall()
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(OS_WIN)
    ProcessNativeEventsInRPCCall();
    return true;
#else
    NS_NOTREACHED(
        "PluginModuleParent::RecvProcessNativeEventsInRPCCall not implemented!");
    return false;
#endif
}

void
PluginModuleParent::ProcessRemoteNativeEventsInRPCCall()
{
#if defined(OS_WIN)
    unused << SendProcessNativeEventsInRPCCall();
    return;
#endif
    NS_NOTREACHED(
        "PluginModuleParent::ProcessRemoteNativeEventsInRPCCall not implemented!");
}

bool
PluginModuleParent::RecvPluginShowWindow(const uint32_t& aWindowId, const bool& aModal,
                                         const int32_t& aX, const int32_t& aY,
                                         const size_t& aWidth, const size_t& aHeight)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    CGRect windowBound = ::CGRectMake(aX, aY, aWidth, aHeight);
    mac_plugin_interposing::parent::OnPluginShowWindow(aWindowId, windowBound, aModal);
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvPluginShowWindow not implemented!");
    return false;
#endif
}

bool
PluginModuleParent::RecvPluginHideWindow(const uint32_t& aWindowId)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    mac_plugin_interposing::parent::OnPluginHideWindow(aWindowId, OtherSidePID());
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvPluginHideWindow not implemented!");
    return false;
#endif
}

PCrashReporterParent*
PluginModuleParent::AllocPCrashReporter(mozilla::dom::NativeThreadId* id,
                                        uint32_t* processType)
{
#ifdef MOZ_CRASHREPORTER
    return new CrashReporterParent();
#else
    return nullptr;
#endif
}

bool
PluginModuleParent::DeallocPCrashReporter(PCrashReporterParent* actor)
{
    delete actor;
    return true;
}

bool
PluginModuleParent::RecvSetCursor(const NSCursorInfo& aCursorInfo)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    mac_plugin_interposing::parent::OnSetCursor(aCursorInfo);
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvSetCursor not implemented!");
    return false;
#endif
}

bool
PluginModuleParent::RecvShowCursor(const bool& aShow)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    mac_plugin_interposing::parent::OnShowCursor(aShow);
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvShowCursor not implemented!");
    return false;
#endif
}

bool
PluginModuleParent::RecvPushCursor(const NSCursorInfo& aCursorInfo)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    mac_plugin_interposing::parent::OnPushCursor(aCursorInfo);
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvPushCursor not implemented!");
    return false;
#endif
}

bool
PluginModuleParent::RecvPopCursor()
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    mac_plugin_interposing::parent::OnPopCursor();
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvPopCursor not implemented!");
    return false;
#endif
}

bool
PluginModuleParent::RecvGetNativeCursorsSupported(bool* supported)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));
#if defined(XP_MACOSX)
    *supported =
      Preferences::GetBool("dom.ipc.plugins.nativeCursorSupport", false);
    return true;
#else
    NS_NOTREACHED(
        "PluginInstanceParent::RecvGetNativeCursorSupportLevel not implemented!");
    return false;
#endif
}

bool
PluginModuleParent::RecvNPN_SetException(PPluginScriptableObjectParent* aActor,
                                         const nsCString& aMessage)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));

    NPObject* aNPObj = NULL;
    if (aActor) {
        aNPObj = static_cast<PluginScriptableObjectParent*>(aActor)->GetObject(true);
        if (!aNPObj) {
            NS_ERROR("Failed to get object!");
            return false;
        }
    }
    mozilla::plugins::parent::NPN_setexception(aNPObj, NullableStringGet(aMessage));
    return true;
}

bool
PluginModuleParent::RecvNPN_ReloadPlugins(const bool& aReloadPages)
{
    PLUGIN_LOG_DEBUG(("%s", FULLFUNCTION));

    mozilla::plugins::parent::NPN_reloadplugins(aReloadPages);
    return true;
}

#ifdef MOZ_CRASHREPORTER_INJECTOR

// We only add the crash reporter to subprocess which have the filename
// FlashPlayerPlugin*
#define FLASH_PROCESS_PREFIX "FLASHPLAYERPLUGIN"

static DWORD
GetFlashChildOfPID(DWORD pid, HANDLE snapshot)
{
    PROCESSENTRY32 entry = {
        sizeof(entry)
    };
    for (BOOL ok = Process32First(snapshot, &entry);
         ok;
         ok = Process32Next(snapshot, &entry)) {
        if (entry.th32ParentProcessID == pid) {
            nsString name(entry.szExeFile);
            ToUpperCase(name);
            if (StringBeginsWith(name, NS_LITERAL_STRING(FLASH_PROCESS_PREFIX))) {
                return entry.th32ProcessID;
            }
        }
    }
    return 0;
}

// We only look for child processes of the Flash plugin, NPSWF*
#define FLASH_PLUGIN_PREFIX "NPSWF"

void
PluginModuleParent::InitializeInjector()
{
    if (!Preferences::GetBool("dom.ipc.plugins.flash.subprocess.crashreporter.enabled", false))
        return;

    nsCString path(Process()->GetPluginFilePath().c_str());
    ToUpperCase(path);
    int32_t lastSlash = path.RFindCharInSet("\\/");
    if (kNotFound == lastSlash)
        return;

    if (!StringBeginsWith(Substring(path, lastSlash + 1),
                          NS_LITERAL_CSTRING(FLASH_PLUGIN_PREFIX)))
        return;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (INVALID_HANDLE_VALUE == snapshot)
        return;

    DWORD pluginProcessPID = GetProcessId(Process()->GetChildProcessHandle());
    mFlashProcess1 = GetFlashChildOfPID(pluginProcessPID, snapshot);
    if (mFlashProcess1) {
        InjectCrashReporterIntoProcess(mFlashProcess1, this);

        mFlashProcess2 = GetFlashChildOfPID(mFlashProcess1, snapshot);
        if (mFlashProcess2) {
            InjectCrashReporterIntoProcess(mFlashProcess2, this);
        }
    }
}

void
PluginModuleParent::OnCrash(DWORD processID)
{
    if (!mShutdown) {
        GetIPCChannel()->CloseWithError();
        KillProcess(OtherProcess(), 1, false);
    }
}

#endif // MOZ_CRASHREPORTER_INJECTOR
