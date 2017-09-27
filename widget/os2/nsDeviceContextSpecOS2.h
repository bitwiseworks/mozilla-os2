/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//---------------------------------------------------------------------------

#ifndef nsDeviceContextSpecOS2_h___
#define nsDeviceContextSpecOS2_h___

#define INCL_BASE
#define INCL_PM
// too pity the above two don't include this one (looks like a bug):
#define INCL_SPLDOSPRINT
#define INCL_DEVDJP
#include <os2.h>

#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsIDeviceContextSpec.h"
#include "nsIPrintOptions.h"
#include "nsIPrintSettings.h"

class nsIOutputStream;
class os2SpoolerStream;
class os2PrintQ;
class os2Printers;

//---------------------------------------------------------------------------

class nsDeviceContextSpecOS2 : public nsIDeviceContextSpec
{
public:
  nsDeviceContextSpecOS2();

  NS_DECL_ISUPPORTS

  // inherited methods
  NS_IMETHOD Init(nsIWidget *aWidget, nsIPrintSettings* aPrintSettings,
                  bool aIsPrintPreview);

  NS_IMETHOD GetSurfaceForPrinter(gfxASurface **nativeSurface);
  NS_IMETHOD BeginDocument(const nsAString& aTitle, char16_t* aPrintToFileName,
                           int32_t aStartPage, int32_t aEndPage);
  NS_IMETHOD EndDocument();
  NS_IMETHOD BeginPage();
  NS_IMETHOD EndPage();

  // new methods
  static nsresult SetPrintSettingsFromDevMode(nsIPrintSettings* aPrintSettings,
                                              ULONG printer);
protected:

  int16_t    AdjustDestinationForFormat(int16_t aFormat, long driverType);
  nsresult   CreateStreamForFormat(int16_t aFormat, nsIOutputStream **aStream);

  virtual ~nsDeviceContextSpecOS2();

  os2PrintQ*    mQueue;
  HDC           mPrintDC;
  PRPackedBool  mPrintingStarted;
  int32_t       mDestination;
  int32_t       mCopies;
  uint32_t      mPages;
  int32_t       mXPixels;
  int32_t       mYPixels;
  int32_t       mXDpi;
  int32_t       mYDpi;
  nsCString     mDefaultName;
  nsCOMPtr<nsIPrintSettings> mPrintSettings;
  RefPtr<os2SpoolerStream> mSpoolerStream;
};

//---------------------------------------------------------------------------

class nsPrinterEnumeratorOS2 : public nsIPrinterEnumerator
{
public:
  nsPrinterEnumeratorOS2();
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPRINTERENUMERATOR

protected:
  virtual ~nsPrinterEnumeratorOS2();
};

//---------------------------------------------------------------------------

#endif
