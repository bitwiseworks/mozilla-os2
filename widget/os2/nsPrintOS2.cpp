/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//---------------------------------------------------------------------------

#include <stdlib.h>

#include "nsPrintOS2.h"

#include "nsIServiceManager.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsUnicharUtils.h"
#include "nsNativeCharsetUtils.h"

//---------------------------------------------------------------------------

#ifdef DEBUG
#define debug_thebes_print
#define debug_enterexit
#endif

#ifdef debug_enterexit
  #define DBGN()    printf("enter %s\n", __FUNCTION__)
  #define DBGX()    printf("exit  %s\n", __FUNCTION__)
  #define DBGNX()   printf("en/ex %s\n", __FUNCTION__)
  #define DBGM(m)   printf("%s - %s\n", __FUNCTION__, m)
#else
  #define DBGN()
  #define DBGX()
  #define DBGNX()
  #define DBGM(m)
#endif

//---------------------------------------------------------------------------

#define SHIFT_PTR(ptr,offset) ( *((LONG*)&ptr) += offset )

os2PrintQ::os2PrintQ(const os2PrintQ& PQInfo)
  : mpDriverData(0)
{
  mPQI3BufSize = PQInfo.mPQI3BufSize;
  mpPQI3 = (PRQINFO3*)malloc(mPQI3BufSize);
  memcpy(mpPQI3, PQInfo.mpPQI3, mPQI3BufSize);    // Copy entire buffer

  long Diff = (long)mpPQI3 - (long)PQInfo.mpPQI3; // Calculate the difference between addresses
  SHIFT_PTR(mpPQI3->pszName,       Diff);         // Modify internal pointers accordingly
  SHIFT_PTR(mpPQI3->pszSepFile,    Diff);
  SHIFT_PTR(mpPQI3->pszPrProc,     Diff);
  SHIFT_PTR(mpPQI3->pszParms,      Diff);
  SHIFT_PTR(mpPQI3->pszComment,    Diff);
  SHIFT_PTR(mpPQI3->pszPrinters,   Diff);
  SHIFT_PTR(mpPQI3->pszDriverName, Diff);
  SHIFT_PTR(mpPQI3->pDriverData,   Diff);

  strcpy(mDriverName, PQInfo.mDriverName);
  strcpy(mDeviceName, PQInfo.mDeviceName);
  strcpy(mPrinterName, PQInfo.mPrinterName);
  mPrinterTitle.Assign(PQInfo.mPrinterTitle);

  if (PQInfo.mpDriverData) {
    mpDriverData = (PDRIVDATA)malloc(PQInfo.mpDriverData->cb);
    if (mpDriverData)
      memcpy(mpDriverData, PQInfo.mpDriverData, PQInfo.mpDriverData->cb);
  }
}

os2PrintQ::os2PrintQ(const PRQINFO3* pInfo)
  : mpDriverData(0)
{
  // Make local copy of PPRQINFO3 object
  ULONG SizeNeeded;
  SplQueryQueue(0, pInfo->pszName, 3, 0, 0, &SizeNeeded);
  mpPQI3 = (PRQINFO3*)malloc(SizeNeeded);
  SplQueryQueue(0, pInfo->pszName, 3, mpPQI3, SizeNeeded, &SizeNeeded);

  mPQI3BufSize = SizeNeeded;

  PCHAR sep = strchr(pInfo->pszDriverName, '.');

  if (sep) {
    *sep = '\0';
    strcpy(mDriverName, pInfo->pszDriverName);
    strcpy(mDeviceName, sep + 1);
    *sep = '.';
  } else {
    strcpy(mDriverName, pInfo->pszDriverName);
    mDeviceName [0] = '\0';
  }

  sep = strchr (pInfo->pszPrinters, ',');

  if (sep) {
    *sep = '\0';
    strcpy(mPrinterName, pInfo->pszPrinters);
    *sep = '.';
  } else {
    strcpy(mPrinterName, pInfo->pszPrinters);
  }

  mPrinterTitle.Truncate();
}

HDC os2PrintQ::OpenHDC()
{
  DEVOPENSTRUC dop;

  memset(&dop, 0, sizeof(dop));
  dop.pszDriverName = (char*)mDriverName;
  dop.pdriv         = DriverData();

  return DevOpenDC(0, OD_INFO, "*", 9, (PDEVOPENDATA)&dop, 0);
}

nsAString* os2PrintQ::PrinterTitle()
{
  if (mPrinterTitle.IsEmpty()) {
    nsAutoCString cName(mpPQI3->pszComment);
    if (cName.IsEmpty())
      cName.Assign(mpPQI3->pszName);
    cName.ReplaceChar('\r', ' ');
    cName.StripChars("\n");
    if (cName.Length() > 64)
      cName.Truncate(64);

    NS_CopyNativeToUnicode(cName, mPrinterTitle);

    // store printer description in prefs for the print dialog
    nsresult rv;
    nsCOMPtr<nsIPrefBranch> pPrefs = do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv)) {
      char  desc[DRIV_DEVICENAME_SIZE + DRIV_NAME_SIZE + 8];
      char  pref[128];

      sprintf(desc, "%s (%s)", DriverData()->szDeviceName, mDriverName);
      sprintf(pref, "print.printer_%s.printer_description", cName.get());
      pPrefs->SetCharPref(pref, desc);
    }
  }

  return &mPrinterTitle;
}

void os2PrintQ::SetDriverData(PDRIVDATA aDriverData)
{
  if (mpDriverData) {
    free(mpDriverData);
  }
  mpDriverData = aDriverData;
}

//---------------------------------------------------------------------------

os2Printers::os2Printers()
{
DBGNX();
  mQueueCount = 0;

  ULONG  TotalQueues = 0;
  ULONG  MemNeeded = 0;

   SplEnumQueue(0, 3, 0, 0, &mQueueCount, &TotalQueues,
                &MemNeeded, 0);

  PRQINFO3* pPQI3Buf = (PRQINFO3*)malloc(MemNeeded);
  SplEnumQueue(0, 3, pPQI3Buf, MemNeeded, &mQueueCount, &TotalQueues,
               &MemNeeded, 0);

  if (mQueueCount > MAX_PRINT_QUEUES)
    mQueueCount = MAX_PRINT_QUEUES;

  // Since we have native GPI printing temporarily disabled (see #171), we rely
  // on internal PostScript support in Cairo (former print.os2.postscript.use_builtin
  // pref) which, in turn, requires the native printer driver to support PS.
  // It makes no sense to show any other printers in the FF UI since printing
  // to them will always fail with a vague "An error occurred while printing"
  // message box. Thus, we hide other printers from the UI.

  ULONG defaultQueue = 0;
  ULONG idx = 0;
  for (ULONG cnt = 0; cnt < mQueueCount; cnt++) {
    os2PrintQ *pq = new os2PrintQ(&pPQI3Buf[cnt]);
    HDC hdc = pq->OpenHDC();
    if (!hdc) {
      DBGM("OpenHDC failed");
      delete pq;
      continue;
    }
    long driverType;
    if (!DevQueryCaps(hdc, CAPS_TECHNOLOGY, 1, &driverType))
      driverType = 0;
    DevCloseDC(hdc);
    if (driverType != CAPS_TECH_POSTSCRIPT) {
      delete pq;
      continue;
    }
    mPQBuf[idx] = pq;
    if (pPQI3Buf[cnt].fsType & PRQ3_TYPE_APPDEFAULT)
      defaultQueue = idx;
    ++idx;
  }

  // adjust the number of printers we accepted
  mQueueCount = idx;

  // move the entry for the default printer to index 0 (if necessary)
  if (defaultQueue > 0) {
    os2PrintQ* temp = mPQBuf[0];
    mPQBuf[0] = mPQBuf[defaultQueue];
    mPQBuf[defaultQueue] = temp;
  }

  free(pPQI3Buf);
}

os2Printers::~os2Printers()
{
DBGNX();
  for (ULONG index = 0; index < mQueueCount; index++)
    delete mPQBuf[index];
}

void os2Printers::RefreshPrintQueue()
{
DBGN();
  ULONG  newQueueCount = 0;
  ULONG  TotalQueues = 0;
  ULONG  MemNeeded = 0;

  SplEnumQueue(0, 3, 0, 0, &newQueueCount, &TotalQueues,
               &MemNeeded, 0);
  PRQINFO3* pPQI3Buf = (PRQINFO3*)malloc(MemNeeded);

  SplEnumQueue(0, 3, pPQI3Buf, MemNeeded, &newQueueCount, &TotalQueues,
               &MemNeeded, 0);
  if (newQueueCount > MAX_PRINT_QUEUES)
    newQueueCount = MAX_PRINT_QUEUES;

  os2PrintQ* tmpBuf[MAX_PRINT_QUEUES];

  // Since we have native GPI printing temporarily disabled (see #171), we rely
  // on internal PostScript support in Cairo (former print.os2.postscript.use_builtin
  // pref) which, in turn, requires the native printer driver to support PS.
  // It makes no sense to show any other printers in the FF UI since printing
  // to them will always fail with a vague "An error occurred while printing"
  // message box. Thus, we hide other printers from the UI.

  ULONG defaultQueue = 0;
  ULONG newIdx = 0;
  for (ULONG cnt = 0; cnt < newQueueCount; cnt++) {
    BOOL found = FALSE;
    for (ULONG index = 0; index < mQueueCount && !found; index++) {
       // Compare printer from requeried list with what's already in
       // Mozilla's printer list(mPQBuf).  If printer is already there,
       // use current properties; otherwise create a new printer in list.
       if (mPQBuf[index] != 0) {
         if (!strcmp(pPQI3Buf[cnt].pszPrinters, mPQBuf[index]->PrinterName()) &&
             !strcmp(pPQI3Buf[cnt].pszDriverName, mPQBuf[index]->FullName())) {
           found = TRUE;
           tmpBuf[newIdx] = mPQBuf[index];
           mPQBuf[index] = 0;
         }
       }
    }
    if (!found) {
      os2PrintQ *pq = new os2PrintQ(&pPQI3Buf[cnt]);
      HDC hdc = pq->OpenHDC();
      if (!hdc) {
        DBGM("OpenHDC failed");
        delete pq;
        continue;
      }
      long driverType;
      if (!DevQueryCaps(hdc, CAPS_TECHNOLOGY, 1, &driverType))
        driverType = 0;
      DevCloseDC(hdc);
      if (driverType != CAPS_TECH_POSTSCRIPT) {
        delete pq;
        continue;
      }
      tmpBuf[newIdx] = pq;
    }
    if (pPQI3Buf[cnt].fsType & PRQ3_TYPE_APPDEFAULT)
      defaultQueue = newIdx;
    ++newIdx;
  }

  // adjust the number of printers we accepted
  newQueueCount = newIdx;

  for (ULONG index = 0; index < newQueueCount; index++) {
    if (mPQBuf[index] != 0)
      delete(mPQBuf[index]);
    mPQBuf[index] = tmpBuf[index];
  }

  if (mQueueCount > newQueueCount)
    for (ULONG index = newQueueCount; index < mQueueCount; index++)
       if (mPQBuf[index] != 0)
         delete(mPQBuf[index]);

  mQueueCount = newQueueCount;

  // move the entry for the default printer to index 0 (if necessary)
  if (defaultQueue > 0) {
    os2PrintQ* temp = mPQBuf[0];
    mPQBuf[0] = mPQBuf[defaultQueue];
    mPQBuf[defaultQueue] = temp;
  }

  free(pPQI3Buf);
}

ULONG os2Printers::GetNumPrinters()
{
  return mQueueCount;
}

os2PrintQ* os2Printers::ClonePrintQ(ULONG printerNdx)
{
  if (printerNdx >= mQueueCount)
    return 0;

  return new os2PrintQ(*mPQBuf[printerNdx]);
}

LONG os2Printers::GetDriverDataSize(ULONG printerNdx)
{
  if (printerNdx >= mQueueCount)
    return 0;

  return mPQBuf[printerNdx]->DriverData()->cb;
}

PDRIVDATA os2Printers::GetDriverData(ULONG printerNdx)
{
  if (printerNdx >= mQueueCount)
    return 0;

  return mPQBuf[printerNdx]->DriverData();
}

HDC os2Printers::OpenHDC(ULONG printerNdx)
{
  if (printerNdx >= mQueueCount)
    return 0;

  return mPQBuf[printerNdx]->OpenHDC();
}

char* os2Printers::GetDriverName(ULONG printerNdx)
{
  if (printerNdx >= mQueueCount)
    return 0;

  return (char*)mPQBuf[printerNdx]->DriverName();
}

BOOL os2Printers::ShowProperties(ULONG printerNdx)
{
DBGNX();
  LONG          devrc = FALSE;
  PDRIVDATA     pOldDrivData;
  PDRIVDATA     pNewDrivData = 0;
  LONG          buflen;

  if (printerNdx >= mQueueCount)
    return FALSE;

  // check size of buffer required for job properties
  buflen = DevPostDeviceModes(0,
                              0,
                              mPQBuf[printerNdx]->DriverName(),
                              mPQBuf[printerNdx]->DeviceName(),
                              mPQBuf[printerNdx]->PrinterName(),
                              DPDM_POSTJOBPROP);

  // return error to caller */
  if (buflen <= 0)
    return FALSE;

  // see if the new driver data is bigger than the existing buffer;
  // if so, alloc a new buffer and copy over old data so driver can
  // use the old job properties to init the job properties dialog
  pOldDrivData = mPQBuf[printerNdx]->DriverData();

  if (buflen > pOldDrivData->cb) {
    DBGM("Allocating new memory for driver data");

    pNewDrivData = (PDRIVDATA)malloc(buflen);
    if (!pNewDrivData)
      return FALSE;
    memcpy(pNewDrivData, pOldDrivData, pOldDrivData->cb);
    mPQBuf[printerNdx]->SetDriverData(pNewDrivData);
  }

  // display job properties dialog and get updated
  // job properties from driver
  devrc = DevPostDeviceModes(0,
                             mPQBuf[printerNdx]->DriverData(),
                             mPQBuf[printerNdx]->DriverName(),
                             mPQBuf[printerNdx]->DeviceName(),
                             mPQBuf[printerNdx]->PrinterName(),
                             DPDM_POSTJOBPROP);

  return (devrc != DPDM_ERROR);
}

nsAString* os2Printers::GetPrinterTitle(ULONG printerNdx)
{
  if (printerNdx >= mQueueCount)
    return 0;

  return mPQBuf[printerNdx]->PrinterTitle();
}

int32_t os2Printers::GetPrinterIndex(const char16_t* aPrinterName)
{
  ULONG index;

  for (index = 0; index < mQueueCount; index++) {
    if (mPQBuf[index]->PrinterTitle()->Equals(aPrinterName, nsCaseInsensitiveStringComparator()))
      break;
  }
  if (index >= mQueueCount)
    return -1;

  return index;
}

//---------------------------------------------------------------------------

HDC   PrnOpenDC(os2PrintQ *pInfo, PCSZ pszApplicationName,
                int copies, int destination, char *file )
{
  PCSZ pszLogAddress;
  PCSZ pszDataType;
  LONG dcType;
  DEVOPENSTRUC dop;

  if (!pInfo || !pszApplicationName)
    return 0;

  if (destination != printToFile) {
    pszLogAddress = pInfo->QueueName();
    pszDataType = "PM_Q_STD";

    if (destination == printPreview)
      dcType = OD_METAFILE;
    else
      dcType = OD_QUEUED;
  } else {
    if (file && *file)
      pszLogAddress = (PSZ)file;
    else
      pszLogAddress = "FILE";

    pszDataType = "PM_Q_RAW";
    dcType = OD_DIRECT;
  }

  dop.pszLogAddress      = const_cast<PSZ>(pszLogAddress);
  dop.pszDriverName      = (char*)pInfo->DriverName();
  dop.pdriv              = pInfo->DriverData();
  dop.pszDataType        = const_cast<PSZ>(pszDataType);
  dop.pszComment         = const_cast<PSZ>(pszApplicationName);
  dop.pszQueueProcName   = const_cast<PSZ>(pInfo->QueueProc());
  dop.pszQueueProcParams = 0;
  dop.pszSpoolerParams   = 0;
  dop.pszNetworkParams   = 0;

  return DevOpenDC(0, dcType, "*", 9, (PDEVOPENDATA)&dop, 0);
}

BOOL  PrnQueryHardcopyCaps(HDC hdc, PHCINFO pHCInfo)
{
  if (!hdc || !pHCInfo)
    return FALSE;

  // query how many forms are available
  long lAvail = DevQueryHardcopyCaps(hdc, 0, 0, 0);
  PHCINFO pBuffer = (PHCINFO)malloc(lAvail * sizeof(HCINFO));

  DevQueryHardcopyCaps(hdc, 0, lAvail, pBuffer);

  BOOL rc = FALSE;
  for (long i = 0; i < lAvail; i++) {
    if (pBuffer[i].flAttributes & HCAPS_CURRENT) {
      memcpy(pHCInfo, &pBuffer[i], sizeof(HCINFO));
      rc = TRUE;
      break;
    }
  }
  free( pBuffer);

  return rc;
}

//---------------------------------------------------------------------------

