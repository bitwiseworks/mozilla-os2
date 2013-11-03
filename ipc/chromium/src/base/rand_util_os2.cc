// Copyright (c) 2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/rand_util.h"

#include <stdlib.h>

#include "base/file_util.h"
#include "base/logging.h"

namespace base {

uint64 RandUint64() {
  uint64 number;

  for (size_t i = 0; i < sizeof(uint64)/sizeof(long); i++) {
    ((long *)&number)[i] = random();
  }

  return number;
}

}  // namespace base
