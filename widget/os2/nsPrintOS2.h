/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//---------------------------------------------------------------------------

#ifndef nsPrintOS2_h___
#define nsPrintOS2_h___

#define INCL_BASE
#define INCL_PM
// too pity the above two don't include this one (looks like a bug):
#define INCL_SPLDOSPRINT
#include <os2.h>

#include "nsStringGlue.h"

//---------------------------------------------------------------------------

typedef enum
{
  printToFile = 0,
  printToPrinter,
  printPreview
} printDest;

#define MAX_PRINT_QUEUES  (128)

//---------------------------------------------------------------------------

class os2PrintQ
{
public:
  os2PrintQ(const PRQINFO3* pInfo);
  os2PrintQ(const os2PrintQ& PQInfo);
  ~os2PrintQ(void) {
    if (mpDriverData)
      free(mpDriverData);
    free(mpPQI3);
  }

  const char* DriverName()  const { return mDriverName; }
  const char* DeviceName()  const { return mDeviceName; }
  const char* PrinterName() const { return mPrinterName; }
  const char* QueueName()   const { return mpPQI3->pszName; }
  const char* QueueTitle()  const { return mpPQI3->pszComment; }
  const char* QueueProc()   const { return mpPQI3->pszPrProc; }
  const char* FullName()    const { return mpPQI3->pszDriverName; }
   PDRIVDATA  DriverData()        { return mpDriverData ? mpDriverData :
                                           mpPQI3->pDriverData; }
        void  SetDriverData(PDRIVDATA aDriverData);
         HDC  OpenHDC();
   nsAString* PrinterTitle();

private:
  os2PrintQ& operator=(const os2PrintQ& z); // prevent copying
  void      InitWithPQI3(const PRQINFO3* pInfo);

  PRQINFO3* mpPQI3;
  unsigned  mPQI3BufSize;
  PDRIVDATA mpDriverData;
  nsString  mPrinterTitle;                         // cleaned-up UCS queue title
  char      mDriverName[DRIV_NAME_SIZE + 1];       // Driver name
  char      mDeviceName[DRIV_DEVICENAME_SIZE + 1]; // Device name
  char      mPrinterName[PRINTERNAME_SIZE + 1];    // Printer name
};

//---------------------------------------------------------------------------

class os2Printers
{
public:
  os2Printers();
  ~os2Printers();
  void       RefreshPrintQueue();
  ULONG      GetNumPrinters();
  os2PrintQ* ClonePrintQ(ULONG printerNdx);
  LONG       GetDriverDataSize(ULONG printerNdx);
  PDRIVDATA  GetDriverData(ULONG printerNdx);
  HDC        OpenHDC(ULONG printerNdx);
  char*      GetDriverName(ULONG printerNdx);
  BOOL       ShowProperties(ULONG printerNdx);
  nsAString* GetPrinterTitle(ULONG printerNdx);
  int32_t    GetPrinterIndex(const char16_t* aPrinterName);

private:
  ULONG      mQueueCount;
  os2PrintQ* mPQBuf[MAX_PRINT_QUEUES];
};

//---------------------------------------------------------------------------

// Get a DC for the selected printer.  Must supply the application name.
HDC   PrnOpenDC(os2PrintQ *pPrintQueue, PCSZ pszApplicationName,
                int copies, int destination, char *file);

// Get the hardcopy caps for the selected form
BOOL  PrnQueryHardcopyCaps(HDC hdc, PHCINFO pHCInfo);

//---------------------------------------------------------------------------

#endif
