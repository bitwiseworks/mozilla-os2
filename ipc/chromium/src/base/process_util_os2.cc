// Copyright (c) 2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define INCL_DOSERRORS
#define INCL_DOSMODULEMGR

#include "base/process_util.h"

#include "base/string_util.h"

namespace base {

void SetCurrentProcessPrivileges(ChildPrivileges privs) {

}

NamedProcessIterator::NamedProcessIterator(const std::wstring& executable_name,
                                           const ProcessFilter* filter)
    : executable_name_(executable_name), proc_rec(NULL), filter_(filter) {
  sys_state = new char[SysStateSize];
  APIRET arc = DosQuerySysState(QS_PROCESS, 0, 0, 0, sys_state, SysStateSize);
  if (arc == NO_ERROR) {
    QSPTRREC *ptr_rec = (QSPTRREC *)sys_state;
    proc_rec = ptr_rec->pProcRec;

  }
}

NamedProcessIterator::~NamedProcessIterator() {
  delete[] sys_state;
}

const ProcessEntry* NamedProcessIterator::NextProcessEntry() {
  bool result = false;
  do {
    result = CheckForNextProcess();
  } while (result && !IncludeEntry());

  if (result)
    return &entry_;

  return NULL;
}

bool NamedProcessIterator::CheckForNextProcess() {
  if (proc_rec && proc_rec->RecType == QS_PROCESS) {
    entry_.pid = proc_rec->pid;
    entry_.ppid = proc_rec->ppid;
    char path[CCHMAXPATH];
    if (DosQueryModuleName(proc_rec->hMte, sizeof(path), path) != NO_ERROR)
      return false;
    memcpy(entry_.szExeFile, path, NAME_MAX);
    entry_.szExeFile[NAME_MAX] = '\0';
    // advance to the next record
    proc_rec = (QSPREC *)(((char *)proc_rec->pThrdRec) + proc_rec->cTCB * sizeof(QSTREC));
    return true;
  }
  return false;
}

bool NamedProcessIterator::IncludeEntry() {
  if (WideToASCII(executable_name_) != entry_.szExeFile)
    return false;
  if (!filter_)
    return true;
  return filter_->Includes(entry_.pid, entry_.ppid);
}

bool ProcessMetrics::GetIOCounters(IoCounters* io_counters) const {
  // @todo later
  NOTIMPLEMENTED();
  return false;
}

}  // namespace base
