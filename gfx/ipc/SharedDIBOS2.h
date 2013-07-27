/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gfx_SharedDIBOS2_h__
#define gfx_SharedDIBOS2_h__

#define INCL_BASE
#define INCL_PM
#include <os2.h>

#include "SharedDIB.h"

namespace mozilla {
namespace gfx {

class SharedDIBOS2 : public SharedDIB
{
public:
  SharedDIBOS2();
  ~SharedDIBOS2();

  // Allocate a new OS/2 dib section compatible with an hps. The dib will
  // be selected into the hps on return.
  nsresult Create(HPS aHps, uint32_t aWidth, uint32_t aHeight,
                  bool aTransparent);

  // Wrap a dib section around an existing shared memory object. aHandle should
  // point to a section large enough for the dib's memory, otherwise this call
  // will fail.
  nsresult Attach(Handle aHandle, uint32_t aWidth, uint32_t aHeight,
                  bool aTransparent);

  // Destroy or release resources associated with this dib.
  nsresult Close();

  // Return the HPS of the shared dib.
  HPS GetHPS() { return mSharedHps; }

  // Return the bitmap bits.
  void* GetBits() { return mBitmapBits; }

  void FlushBits();

private:
  HPS                 mSharedHps;
  HBITMAP             mSharedBmp;
  HBITMAP             mOldBmp;
  BITMAPINFO2*        mBitmapHdr;
  BYTE*               mBitmapBits;

  uint32_t SetupBitmapHeader(uint32_t aWidth, uint32_t aHeight,
                             bool aTransparent, BITMAPINFOHEADER2 *aHeader);
  nsresult SetupSurface(HPS aHps, BITMAPINFOHEADER2 *aHdr);
};

} // gfx
} // mozilla

#endif
