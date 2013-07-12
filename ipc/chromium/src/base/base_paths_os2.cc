// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define INCL_BASE
#include <os2.h>

#include "base/base_paths_os2.h"

#include "base/file_path.h"
#include "base/path_service.h"

namespace base {

bool PathProviderOS2(int key, FilePath* result) {
  FilePath path;
  switch (key) {
    case base::FILE_EXE: {
      char buf [CCHMAXPATH];
      PPIB ppib;
      DosGetInfoBlocks(NULL, &ppib);
      DosQueryModuleName(ppib->pib_hmte, sizeof(buf), buf);
      *result = FilePath(buf);
      return true;
    }
    case base::FILE_MODULE: {
      char buf [CCHMAXPATH];
      HMODULE hmod;
      ULONG objNum, offset;
      DosQueryModFromEIP(&hmod, &objNum, sizeof(buf), buf, &offset,
                         (ULONG)PathProviderOS2);
      DosQueryModuleName(hmod, sizeof(buf), buf);
      *result = FilePath(buf);
      return true;
    }
    case base::DIR_SOURCE_ROOT:
      // On linux, unit tests execute two levels deep from the source root.
      // For example:  sconsbuild/{Debug|Release}/net_unittest
      if (!PathService::Get(base::DIR_EXE, &path))
        return false;
      path = path.Append(FilePath::kParentDirectory)
                 .Append(FilePath::kParentDirectory);
      *result = path;
      return true;
  }
  return false;
}

}  // namespace base
