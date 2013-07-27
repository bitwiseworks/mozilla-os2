/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedDIBOS2.h"
#include "gfxAlphaRecovery.h"
#include "nsMathUtils.h"
#include "nsDebug.h"

namespace mozilla {
namespace gfx {

static const uint32_t kBytesPerPixel = 4;
static const uint32_t kHeaderBytes = sizeof(BITMAPINFOHEADER2);

SharedDIBOS2::SharedDIBOS2() :
    mSharedHps(NULL)
  , mSharedBmp(NULL)
  , mOldBmp(NULL)
{
}

SharedDIBOS2::~SharedDIBOS2()
{
  Close();
}

nsresult
SharedDIBOS2::Close()
{
  if (mSharedHps && mOldBmp)
    ::GpiSetBitmap(mSharedHps, mOldBmp);

  if (mSharedHps) {
    HDC hdc = ::GpiQueryDevice(mSharedHps);
    ::GpiAssociate(mSharedHps, NULLHANDLE);
    ::GpiDestroyPS(mSharedHps);
    ::DevCloseDC(hdc);
  }

  if (mSharedBmp)
    ::GpiDeleteBitmap(mSharedBmp);

  mSharedHps = NULL;
  mOldBmp = mSharedBmp = NULL;

  SharedDIB::Close();

  return NS_OK;
}

nsresult
SharedDIBOS2::Create(HPS aHps, uint32_t aWidth, uint32_t aHeight,
                     bool aTransparent)
{
  Close();

  // create the offscreen shared dib
  BITMAPINFOHEADER2 bmih;
  uint32_t size = SetupBitmapHeader(aWidth, aHeight, aTransparent, &bmih);

  nsresult rv = SharedDIB::Create(size);
  if (NS_FAILED(rv))
    return rv;

  if (NS_FAILED(SetupSurface(aHps, &bmih))) {
    Close();
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult
SharedDIBOS2::Attach(Handle aHandle, uint32_t aWidth, uint32_t aHeight,
                     bool aTransparent)
{
  Close();

  BITMAPINFOHEADER2 bmih;
  SetupBitmapHeader(aWidth, aHeight, aTransparent, &bmih);

  nsresult rv = SharedDIB::Attach(aHandle, 0);
  if (NS_FAILED(rv))
    return rv;

  if (NS_FAILED(SetupSurface(NULL, &bmih))) {
    Close();
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

void
SharedDIBOS2::FlushBits()
{
  if (!mSharedBmp)
    return;

  ::GpiSetBitmapBits(mSharedHps, 0, mBitmapHdr->cy, mBitmapBits, mBitmapHdr);
}

uint32_t
SharedDIBOS2::SetupBitmapHeader(uint32_t aWidth, uint32_t aHeight,
                                bool /*aTransparent*/, BITMAPINFOHEADER2 *aHeader)
{
  memset((void*)aHeader, 0, sizeof(BITMAPINFOHEADER2));
  aHeader->cbFix = sizeof(BITMAPINFOHEADER2);
  aHeader->cx = aWidth;
  aHeader->cy = aHeight;
  aHeader->cPlanes = 1;
  aHeader->cBitCount = 32;

  return (kHeaderBytes + (aHeader->cx * aHeader->cy * kBytesPerPixel));
}

nsresult
SharedDIBOS2::SetupSurface(HPS aHps, BITMAPINFOHEADER2 *aHdr)
{
  HDC hdcCompat = NULLHANDLE;
  if (aHps)
    hdcCompat = ::GpiQueryDevice(aHps);

  static PCSZ hdcData[4] = { "Display", NULL, NULL, NULL };
  HDC hdc = ::DevOpenDC(0, OD_MEMORY, "*", 4, (PDEVOPENDATA) hdcData, hdcCompat);
  if (!hdc)
    return NS_ERROR_FAILURE;

  SIZEL size = { aHdr->cx, aHdr->cy };
  mSharedHps = ::GpiCreatePS(0, hdc, &size, PU_PELS | GPIA_ASSOC | GPIT_MICRO);

  if (!mSharedHps)
    return NS_ERROR_FAILURE;

  void *data = mShMem->memory();
  if (!data)
    return NS_ERROR_FAILURE;

  mBitmapHdr = (BITMAPINFO2*)data;
  mBitmapBits = (BYTE*)data + kHeaderBytes;

  if (aHps) {
    // we are creating a new dib instead of attaching, initialize its header
    memcpy(data, aHdr, kHeaderBytes);
  }

  mSharedBmp = ::GpiCreateBitmap(mSharedHps, aHdr, CBM_INIT, mBitmapBits, mBitmapHdr);
  if (!mSharedBmp)
      return NS_ERROR_FAILURE;

  mOldBmp = ::GpiSetBitmap(mSharedHps, mSharedBmp);

  return NS_OK;
}

} // gfx
} // mozilla
