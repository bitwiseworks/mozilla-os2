// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/shared_memory.h"
#include "base/sys_string_conversions.h"

#include "base/logging.h"

#include <unistd.h>

namespace base {

SharedMemory::SharedMemory()
    : base_(NULL),
      committed_(0),
      memory_(NULL),
      read_only_(false),
      max_size_(0),
      lock_(NULLHANDLE) {
}

SharedMemory::SharedMemory(SharedMemoryHandle handle, bool read_only)
    : base_(handle),
      committed_(0),
      memory_(NULL),
      read_only_(read_only),
      max_size_(0),
      lock_(NULLHANDLE) {
  if (handle != NULL) {
    ULONG size = ~0, flags;
    APIRET arc = ::DosQueryMem(handle, &size, &flags);
    if (arc == NO_ERROR)
      max_size_ = size;
  }
}

SharedMemory::SharedMemory(SharedMemoryHandle handle, bool read_only,
                           ProcessHandle process)
    : base_(NULL),
      committed_(0),
      memory_(NULL),
      read_only_(read_only),
      max_size_(0),
      lock_(NULLHANDLE) {
  APIRET arc = ::DosGetSharedMem(handle,
      read_only_ ? PAG_READ : PAG_READ | PAG_WRITE);
  CHECK(arc == NO_ERROR);
  if (arc == NO_ERROR) {
    base_ = handle;
    ULONG size = ~0, flags;
    arc = ::DosQueryMem(base_, &size, &flags);
    if (arc == NO_ERROR)
      max_size_ = size;
  }
}

SharedMemory::~SharedMemory() {
  Close();
  if (lock_ != NULLHANDLE)
    ::DosCloseMutexSem(lock_);
}

// static
bool SharedMemory::IsHandleValid(const SharedMemoryHandle& handle) {
  return handle != NULL;
}

// static
SharedMemoryHandle SharedMemory::NULLHandle() {
  return NULL;
}

bool SharedMemory::Create(const std::string &name, bool read_only,
                          bool open_existing, size_t size) {
  DCHECK(base_ == NULL);
  name_ = name;
  read_only_ = read_only;

  // name must start with "\SHAREMEM\"
  if (!name_.empty())
    name_.insert(0, "\\SHAREMEM\\");

  PVOID base;
  APIRET arc;
#if defined(MOZ_OS2_HIGH_MEMORY)
  arc = ::DosAllocSharedMem(&base,
      name_.empty() ? NULL : name_.c_str(), size,
      (name_.empty() ? OBJ_GETTABLE | OBJ_GIVEABLE : 0) |
      (read_only_ ? PAG_READ : PAG_READ | PAG_WRITE) | OBJ_ANY /*himem*/);
  if (arc != NO_ERROR && arc != ERROR_ALREADY_EXISTS)
#endif
    arc = ::DosAllocSharedMem(&base,
        name_.empty() ? NULL : name_.c_str(), size,
        (name_.empty() ? OBJ_GETTABLE | OBJ_GIVEABLE : 0) |
        (read_only_ ? PAG_READ : PAG_READ | PAG_WRITE));
  if (arc == ERROR_ALREADY_EXISTS) {
    if (!open_existing)
      return false;
    // try to open the existing one
    arc = ::DosGetNamedSharedMem(&base, name_.c_str(),
        read_only_ ? PAG_READ : PAG_READ | PAG_WRITE);
  }
  if (arc != NO_ERROR)
    return false;

  base_ = base;
  max_size_ = size;
  return true;
}

bool SharedMemory::Delete(const std::wstring& name) {
  // intentionally empty -- there is nothing for us to do on OS/2.
  return true;
}

bool SharedMemory::Open(const std::wstring &name, bool read_only) {
  DCHECK(base_ == NULL);

  name_ = base::SysWideToNativeMB(name);
  read_only_ = read_only;

  // name must start with "\SHAREMEM\"
  if (!name_.empty())
    name_.insert(0, "\\SHAREMEM\\");

  PVOID base;
  APIRET arc = ::DosGetNamedSharedMem(&base, name_.c_str(),
      read_only_ ? PAG_READ : PAG_READ | PAG_WRITE);
  if (arc != NO_ERROR)
    return false;

  base_ = base;

  ULONG size = ~0, flags;
  arc = ::DosQueryMem(base_, &size, &flags);
  if (arc == NO_ERROR)
    max_size_ = size;

  return true;
}

bool SharedMemory::Map(size_t bytes) {
  if (base_ == NULL)
    return false;

  // bytes = 0 means whole segment according to TransportDIB::Map()
  if (bytes == 0)
    bytes = max_size_;

  APIRET arc = ::DosSetMem(base_, bytes,
      PAG_COMMIT | (read_only_ ? PAG_READ : PAG_READ | PAG_WRITE));
  if (arc != NO_ERROR)
    return false;

  memory_ = base_;
  committed_ = bytes;
  return true;
}

bool SharedMemory::Unmap() {
  if (memory_ == NULL)
    return false;

  APIRET arc = ::DosSetMem(base_, committed_,
      PAG_DECOMMIT);
  if (arc != NO_ERROR)
    return false;

  memory_ = NULL;
  committed_ = 0;
  return true;
}

bool SharedMemory::ShareToProcessCommon(ProcessHandle process,
                                        SharedMemoryHandle *new_handle,
                                        bool close_self) {
  // only unnamed shared memory may be given to another process on OS/2
  DCHECK(name_.empty());

  *new_handle = NULL;

  if (close_self) {
    Unmap();
    if (process == getpid()) {
      *new_handle = base_;
      base_ = NULL;
      return true;
    }
  }

  APIRET arc = ::DosGiveSharedMem(base_, process,
      (read_only_ ? PAG_READ : PAG_READ | PAG_WRITE));
  if (arc != NO_ERROR)
    return false;

  if (close_self)
    Close();

  *new_handle = base_;
  return true;
}

void SharedMemory::Close() {
  if (memory_ != NULL)
    Unmap();

  if (base_ != NULL) {
    ::DosFreeMem(base_);
    base_ = NULL;
  }
}

void SharedMemory::Lock() {
  if (lock_ == NULLHANDLE) {
    std::string name = name_;
    name.append(".lock");
    // name must start with "\SEM32\"
    name_.insert(0, "\\SEM32\\");
    APIRET arc = ::DosCreateMutexSem(name.c_str(), &lock_,
        DC_SEM_SHARED, 0);
    if (arc == ERROR_DUPLICATE_NAME) {
      // try to open the existing one
      arc = ::DosOpenMutexSem(name.c_str(), &lock_);
    }
    DCHECK(arc == NO_ERROR);
    if (arc != NO_ERROR) {
      DLOG(ERROR) << "Could not create mutex" << arc;
      return;  // there is nothing good we can do here.
    }
  }
  APIRET arc = ::DosRequestMutexSem(lock_, SEM_INDEFINITE_WAIT);
  DCHECK(arc == NO_ERROR);
}

void SharedMemory::Unlock() {
  DCHECK(lock_ != NULLHANDLE);
  ::DosReleaseMutexSem(lock_);
}

SharedMemoryHandle SharedMemory::handle() const {
  return base_;
}

}  // namespace base
