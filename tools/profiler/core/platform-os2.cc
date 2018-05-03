// Copyright (c) 2006-2011 The Chromium Authors. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the
//    distribution.
//  * Neither the name of Google, Inc. nor the names of its contributors
//    may be used to endorse or promote products derived from this
//    software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
// OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
// AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
// OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
// SUCH DAMAGE.

#include "platform.h"
#include "GeckoSampler.h"
#include "ThreadResponsiveness.h"
#include "ProfileEntry.h"

// Memory profile
#include "nsMemoryReporterManager.h"

class PlatformData {
 public:
// @todo
//  PlatformData(int aThreadId) : profiled_thread_(aThreadId) {}

//  ~PlatformData() {}

//  TID profiled_thread() { return profiled_thread_; }

// private:
//  TID profiled_thread_;
  PlatformData()
  {}
};

/* static */ PlatformData*
Sampler::AllocPlatformData(int aThreadId)
{
//  return new PlatformData(aThreadId);
  return new PlatformData;
}

/* static */ void
Sampler::FreePlatformData(PlatformData* aData)
{
  delete aData;
}

// @todo
//uintptr_t
//Sampler::GetThreadHandle(PlatformData* aData)
//{
//  return (uintptr_t) aData->profiled_thread();
//}

class SamplerThread : public Thread {
 public:
  SamplerThread(double interval, Sampler* sampler)
      : Thread("SamplerThread")
      , interval_(interval)
      , sampler_(sampler)
  {
    interval_ = floor(interval + 0.5);
    if (interval_ <= 0) {
      interval_ = 1;
    }
  }

  static void StartSampler(Sampler* sampler) {
    if (instance_ == NULL) {
      instance_ = new SamplerThread(sampler->interval(), sampler);
      instance_->Start();
    } else {
      ASSERT(instance_->interval_ == sampler->interval());
    }
  } 

  static void StopSampler() {
    instance_->Join();
    delete instance_;
    instance_ = NULL;
  }

  // Implement Thread::Run().
  virtual void Run() {

// @todo
//    // By default we'll not adjust the timer resolution which tends to be around
//    // 16ms. However, if the requested interval is sufficiently low we'll try to
//    // adjust the resolution to match.
//    if (interval_ < 10)
//        ::timeBeginPeriod(interval_);

    while (sampler_->IsActive()) {
      sampler_->DeleteExpiredMarkers();

      if (!sampler_->IsPaused()) {
        ::MutexAutoLock lock(*Sampler::sRegisteredThreadsMutex);
        std::vector<ThreadInfo*> threads =
          sampler_->GetRegisteredThreads();
        bool isFirstProfiledThread = true;
        for (uint32_t i = 0; i < threads.size(); i++) {
          ThreadInfo* info = threads[i];

          // This will be null if we're not interested in profiling this thread.
          if (!info->Profile() || info->IsPendingDelete())
            continue;

          PseudoStack::SleepState sleeping = info->Stack()->observeSleeping();
          if (sleeping == PseudoStack::SLEEPING_AGAIN) {
            info->Profile()->DuplicateLastSample();
            continue;
          }

          info->Profile()->GetThreadResponsiveness()->Update();

          ThreadProfile* thread_profile = info->Profile();

          SampleContext(sampler_, thread_profile, isFirstProfiledThread);
          isFirstProfiledThread = false;
        }
      }
      OS::Sleep(interval_);
    }

// @todo
//    // disable any timer resolution changes we've made
//    if (interval_ < 10)
//        ::timeEndPeriod(interval_);
  }

  void SampleContext(Sampler* sampler, ThreadProfile* thread_profile,
                     bool isFirstProfiledThread)
  {
// @todo
//    uintptr_t thread = Sampler::GetThreadHandle(
//                               thread_profile->GetPlatformData());
//    HANDLE profiled_thread = reinterpret_cast<HANDLE>(thread);
//    if (profiled_thread == NULL)
//      return;
    tid_t profiled_tid = thread_profile->ThreadId();

    // Context used for sampling the register state of the profiled thread.
    CONTEXTRECORD context;
    memset(&context, 0, sizeof(context));

    TickSample sample_obj;
    TickSample* sample = &sample_obj;

    // Grab the timestamp before pausing the thread, to avoid deadlocks.
    sample->timestamp = mozilla::TimeStamp::Now();
    sample->threadProfile = thread_profile;

    if (isFirstProfiledThread && Sampler::GetActiveSampler()->ProfileMemory()) {
      sample->rssMemory = nsMemoryReporterManager::ResidentFast();
    } else {
      sample->rssMemory = 0;
    }

    // Unique Set Size is not supported on OS/2.
    sample->ussMemory = 0;

    if (DosSuspendThread(profiled_tid) != NO_ERROR)
      return;

    context.ContextFlags = CONTEXT_CONTROL;
    if (DosQueryThreadContext(profiled_tid, CONTEXT_CONTROL, &context) == NO_ERROR) {
      sample->pc = reinterpret_cast<Address>(context.ctx_RegEip);
      sample->sp = reinterpret_cast<Address>(context.ctx_RegEsp);
      sample->fp = reinterpret_cast<Address>(context.ctx_RegEbp);
      sample->context = &context;
      sampler->Tick(sample);
    }
    DosResumeThread(profiled_tid);
  }

  int interval_; // units: ms
  Sampler* sampler_;

  // Protects the process wide state below.
  static SamplerThread* instance_;

  DISALLOW_COPY_AND_ASSIGN(SamplerThread);
};

SamplerThread* SamplerThread::instance_ = NULL;


Sampler::Sampler(double interval, bool profiling, int entrySize)
    : interval_(interval),
      profiling_(profiling),
      paused_(false),
      active_(false),
      entrySize_(entrySize) {
}

Sampler::~Sampler() {
  ASSERT(!IsActive());
}

void Sampler::Start() {
  ASSERT(!IsActive());
  SetActive(true);
  SamplerThread::StartSampler(this);
}

void Sampler::Stop() {
  ASSERT(IsActive());
  SetActive(false);
  SamplerThread::StopSampler();
}


static void ThreadEntry(void* arg) {
  Thread* thread = reinterpret_cast<Thread*>(arg);
  thread->Run();
}

// Initialize a Win32 thread object. The thread has an invalid thread
// handle until it is started.
Thread::Thread(const char* name)
    : stack_size_(0) {
  thread_id_ = 0;
  set_name(name);
}

void Thread::set_name(const char* name) {
  strncpy(name_, name, sizeof(name_));
  name_[sizeof(name_) - 1] = '\0';
}

Thread::~Thread() {
}

// Create a new thread. It is important to use _beginthread() instead of
// the OS/2 function DosCreateThread(), because the DosCreateThread() does not
// initialize thread specific structures in the C runtime library.
void Thread::Start() {
  thread_id_ = _beginthread(ThreadEntry,
                            NULL,
                            static_cast<unsigned>(stack_size_),
                            this);
}

// Wait for thread to terminate.
void Thread::Join() {
  if (thread_id_ != GetCurrentId()) {
    TID tid = thread_id_;
    ::DosWaitThread(&tid, DCWW_WAIT);
  }
}

/* static */ Thread::tid_t
Thread::GetCurrentId()
{
  return _gettid();
}

void OS::Startup() {
}

void OS::Sleep(int milliseconds) {
  ::DosSleep(milliseconds);
}

bool Sampler::RegisterCurrentThread(const char* aName,
                                    PseudoStack* aPseudoStack,
                                    bool aIsMainThread, void* stackTop)
{
  if (!Sampler::sRegisteredThreadsMutex)
    return false;


  ::MutexAutoLock lock(*Sampler::sRegisteredThreadsMutex);

  int id = _gettid();

  for (uint32_t i = 0; i < sRegisteredThreads->size(); i++) {
    ThreadInfo* info = sRegisteredThreads->at(i);
    if (info->ThreadId() == id && !info->IsPendingDelete()) {
      // Thread already registered. This means the first unregister will be
      // too early.
      ASSERT(false);
      return false;
    }
  }

  set_tls_stack_top(stackTop);

  ThreadInfo* info = new StackOwningThreadInfo(aName, id,
    aIsMainThread, aPseudoStack, stackTop);

  if (sActiveSampler) {
    sActiveSampler->RegisterThread(info);
  }

  sRegisteredThreads->push_back(info);

  return true;
}

void Sampler::UnregisterCurrentThread()
{
  if (!Sampler::sRegisteredThreadsMutex)
    return;

  tlsStackTop.set(nullptr);

  ::MutexAutoLock lock(*Sampler::sRegisteredThreadsMutex);

  int id = _gettid();

  for (uint32_t i = 0; i < sRegisteredThreads->size(); i++) {
    ThreadInfo* info = sRegisteredThreads->at(i);
    if (info->ThreadId() == id && !info->IsPendingDelete()) {
      if (profiler_is_active()) {
        // We still want to show the results of this thread if you
        // save the profile shortly after a thread is terminated.
        // For now we will defer the delete to profile stop.
        info->SetPendingDelete();
        break;
      } else {
        delete info;
        sRegisteredThreads->erase(sRegisteredThreads->begin() + i);
        break;
      }
    }
  }
}

void TickSample::PopulateContext(void* aContext)
{
  // There is no way to query the context of the calling thread on OS/2,
  // use assembly (borrowed from platform-macos.cc).

  // Note that this asm changes if PopulateContext's parameter list is altered

  asm (
      // Compute caller's %esp by adding to %ebp:
      // 4 bytes for aContext + 4 bytes for return address +
      // 4 bytes for previous %ebp
      "leal 0xc(%%ebp), %0\n\t"
      // Dereference %ebp to get previous %ebp
      "movl (%%ebp), %1\n\t"
      :
      "=r"(sp),
      "=r"(fp)
  );

  pc = reinterpret_cast<Address>(__builtin_extract_return_addr(
                                    __builtin_return_address(0)));

// @todo remove
//  asm(
//      "movl %%ebp"
//      :
//      "=r"(sp),
//      "=r"(sp),
//      "=r"(pc)
//  );
}

