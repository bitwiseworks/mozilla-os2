//
// Copyright (c) 2002-2010 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

#include "compiler/osinclude.h"
//
// This file contains contains the OS/2 specific functions
//

#if !defined(ANGLE_OS_OS2)
#error Trying to build a windows specific file in a non windows build.
#endif


//
// Thread Local Storage Operations
//
OS_TLSIndex OS_AllocTLSIndex()
{
  PULONG nIndex = 0;
  APIRET rc = DosAllocThreadLocalMemory(1, &nIndex);
  if (rc) {
    assert(0 && "OS_AllocTLSIndex(): Unable to allocate Thread Local Storage");
    return OS_INVALID_TLS_INDEX;
  }

  return nIndex;
}


bool OS_SetTLSValue(OS_TLSIndex nIndex, void *lpvValue)
{
	if (nIndex == OS_INVALID_TLS_INDEX) {
		assert(0 && "OS_SetTLSValue(): Invalid TLS Index");
		return false;
	}

  *nIndex = (ULONG)lpvValue;
  return true;
}


bool OS_FreeTLSIndex(OS_TLSIndex nIndex)
{
	if (nIndex == OS_INVALID_TLS_INDEX) {
		assert(0 && "OS_SetTLSValue(): Invalid TLS Index");
		return false;
	}

  DosFreeThreadLocalMemory(nIndex);
  return true;
}
