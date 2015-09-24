// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define INCL_BASE
#define INCL_PM
#include <os2.h>

#include <limits>

#include "base/logging.h"
#include "base/sys_info.h"
#include "chrome/common/transport_dib.h"

TransportDIB::TransportDIB() {
}

TransportDIB::~TransportDIB() {
}

TransportDIB::TransportDIB(base::SharedMemoryHandle handle)
    : shared_memory_(handle, false /* read write */) {
}

// static
TransportDIB* TransportDIB::Create(size_t size, uint32_t sequence_num) {
  size_t allocation_granularity = base::SysInfo::VMAllocationGranularity();
  size = size / allocation_granularity + 1;
  size = size * allocation_granularity;

  TransportDIB* dib = new TransportDIB;

  if (!dib->shared_memory_.Create("", false /* read write */,
                                  true /* open existing */, size)) {
    delete dib;
    return NULL;
  }

  dib->size_ = size;
  dib->sequence_num_ = sequence_num;

  return dib;
}

// static
TransportDIB* TransportDIB::Map(TransportDIB::Handle handle) {
  TransportDIB* dib = new TransportDIB(handle);
  if (!dib->shared_memory_.Map(0 /* map whole shared memory segment */)) {
    CHROMIUM_LOG(ERROR) << "Failed to map transport DIB"
               << " handle:" << handle;
    delete dib;
    return NULL;
  }

  dib->size_ = dib->shared_memory_.max_size();

  return dib;
}

void* TransportDIB::memory() const {
  return shared_memory_.memory();
}

TransportDIB::Handle TransportDIB::handle() const {
  return shared_memory_.handle();
}

TransportDIB::Id TransportDIB::id() const {
  return shared_memory_.handle();
}
