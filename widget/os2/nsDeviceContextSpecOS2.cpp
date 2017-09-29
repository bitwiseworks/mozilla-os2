/* vim: set sw=2 sts=2 et cin: */
/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//---------------------------------------------------------------------------

#include <stdlib.h>

#include "nsDeviceContextSpecOS2.h"

#define INCL_GRE_DEVICE
#include <pmddim.h>
#include "nsReadableUtils.h"
#include "nsTArray.h"
#include "prtime.h"
#include "nsIServiceManager.h"
#include "nsUnicharUtils.h"
#include "nsStringFwd.h"
#include "nsStringEnumerator.h"
#include "nsNativeCharsetUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIFileStreams.h"
#include "gfxPDFSurface.h"
#include "gfxPSSurface.h"
#include "gfxOS2Surface.h"
#include "nsIPrintSettingsService.h"
#include "mozilla/Services.h"
#include "nsIWindowWatcher.h"
#include "nsIDOMWindow.h"
#include "nsIFilePicker.h"
#include "nsIStringBundle.h"
#include "nsILocalFile.h"

#include "nsPrintOS2.h"
#include "nsPrintfCString.h"
#include "mozilla/Preferences.h"

#define QQ_(str) #str
#define Q_(str) QQ_(str)
#define MOZ_APP_DISPLAYNAME_STR Q_(MOZ_APP_DISPLAYNAME)

using namespace mozilla;

//---------------------------------------------------------------------------

#define NS_ERROR_GFX_PRINTER_BUNDLE_URL \
            "chrome://global/locale/printing.properties"

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
//  Static Functions
//---------------------------------------------------------------------------

static int16_t  AdjustFormatAndExtension(int16_t aFormat,
                                         nsAString& aFileName);
static nsresult GetFileNameForPrintSettings(nsIPrintSettings* aPS,
                                            nsAString& aFileName);
#if 0 // This is temporariy disabled, see #171.
static char *   GetACPString(const char16_t* aStr);
#endif
static char *   GetACPString(const nsAString& aStr);
static void     SetDevModeFromSettings(ULONG printer,
                                       nsIPrintSettings* aPrintSettings);

//---------------------------------------------------------------------------
//  Static Data
//---------------------------------------------------------------------------

os2Printers sPrinterList;

// Pref Constants
#if 0 // This is temporariy disabled, see #171.
static const char kOS2UseBuiltinPS[]    = "print.os2.postscript.use_builtin";
#endif
static const char kOS2UseIbmNull[]      = "print.os2.postscript.use_ibmnull";

//---------------------------------------------------------------------------
//  os2NullOutputStream class
//---------------------------------------------------------------------------
// os2NullOutputStream avoids creating an empty file when doing
// a print preview for PS or PDF output.  As the name implies,
// it discards anything written to this stream.

class os2NullOutputStream : public nsIOutputStream
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOUTPUTSTREAM

  os2NullOutputStream();

private:
  virtual ~os2NullOutputStream()  { DBGNX(); }
#ifdef debug_enterexit
  int32_t   mWrites;
#endif
};

//---------------------------------------------------------------------------
//  os2SpoolerStream class
//---------------------------------------------------------------------------
// os2SpoolerStream enables PS output to be written directly
// to the spooler without the need for an intermediate file.

class os2SpoolerStream : public nsIOutputStream
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOUTPUTSTREAM

  os2SpoolerStream();

  nsresult  Init(os2PrintQ* aQueue, const char* aTitle);
  nsresult  BeginDocument(const char* aTitle);
  void      IncPageCount()  { mPages++; }

private:
  virtual ~os2SpoolerStream()       { DBGNX(); }

  HSPL      mSpl;
  uint32_t  mPages;
#ifdef debug_enterexit
  int32_t   mWrites;
#endif
};

//---------------------------------------------------------------------------
//  nsDeviceContextSpecOS2 implementation
//---------------------------------------------------------------------------

nsDeviceContextSpecOS2::nsDeviceContextSpecOS2()
  : mQueue(nullptr), mPrintDC(NULLHANDLE), mPrintingStarted(false), mPages(0),
    mXPixels(1), mYPixels(1), mXDpi(-1), mYDpi(-1)
{
  DBGNX();
}

nsDeviceContextSpecOS2::~nsDeviceContextSpecOS2()
{
  DBGNX();

  if (mQueue)
    delete mQueue;
}

NS_IMPL_ISUPPORTS(nsDeviceContextSpecOS2, nsIDeviceContextSpec)

//---------------------------------------------------------------------------

NS_IMETHODIMP nsDeviceContextSpecOS2::Init(nsIWidget *aWidget,
                                           nsIPrintSettings* aPrintSettings,
                                           bool aIsPrintPreview)
{
  DBGN();

  NS_ENSURE_ARG_POINTER(aPrintSettings);
  mPrintSettings = aPrintSettings;

  nsXPIDLString printerName;
  mPrintSettings->GetPrinterName(getter_Copies(printerName));
  NS_ENSURE_TRUE(!printerName.IsEmpty(), NS_ERROR_GFX_PRINTER_NO_PRINTER_AVAILABLE);

  int32_t printerNdx = sPrinterList.GetPrinterIndex(printerName);
  if (printerNdx < 0) {
    DBGM("GetPrinterIndex failed");
    return NS_ERROR_FAILURE;
  }

  // Set dynamic job properties (orientation, nbr of copies).
  mPrintSettings->GetNumCopies(&mCopies);
  NS_ENSURE_TRUE((mCopies && mCopies < 1000), NS_ERROR_FAILURE);
  SetDevModeFromSettings(printerNdx, mPrintSettings);

  // Get a device context we can query.
  HDC hdc = sPrinterList.OpenHDC(printerNdx);
  if (!hdc) {
    DBGM("OpenHDC failed");
    return NS_ERROR_FAILURE;
  }

  // Get paper's size and unprintable margins.
  // This should be settable in SetPrintSettingsFromDevMode() but
  // UnwriteableMargin gets reset to zero by the time it is used.
  HCINFO  hci;
  if (!PrnQueryHardcopyCaps(hdc, &hci)) {
    DBGM("PrnQueryHardcopyCaps failed");
    return NS_ERROR_FAILURE;
  }

  mPrintSettings->SetPaperWidth(double(hci.cx / 25.4));
  mPrintSettings->SetPaperHeight(double(hci.cy / 25.4));
  nsIntMargin margin(
      NSToIntRoundUp(NS_MILLIMETERS_TO_TWIPS(hci.xLeftClip)),
      NSToIntRoundUp(NS_MILLIMETERS_TO_TWIPS(hci.cy - hci.yTopClip)),
      NSToIntRoundUp(NS_MILLIMETERS_TO_TWIPS(hci.cx - hci.xRightClip)),
      NSToIntRoundUp(NS_MILLIMETERS_TO_TWIPS(hci.yBottomClip)));
  mPrintSettings->SetUnwriteableMarginInTwips(margin);

  // Pixel counts are only used for native format.
  mXPixels = hci.xPels;
  mYPixels = hci.yPels;

  // Determine if this is a raster or Postscript printer.
  long driverType;
  if (!DevQueryCaps(hdc, CAPS_TECHNOLOGY, 1, &driverType))
    driverType = 0;

  if (!DevQueryCaps(hdc, CAPS_HORIZONTAL_FONT_RES, 1, (LONG*)&mXDpi))
    mXDpi = 300;
  if (!DevQueryCaps(hdc, CAPS_VERTICAL_FONT_RES, 1, (LONG*)&mYDpi))
    mYDpi = 300;

  DevCloseDC(hdc);
  NS_ENSURE_TRUE(driverType, NS_ERROR_FAILURE);

  // Create a default name that can be used as a file or print job name.
  PRExplodedTime time;
  PR_ExplodeTime(PR_Now(), PR_LocalTimeParameters, &time);
  mDefaultName.Assign(
      nsPrintfCString("%s_%04d%02d%02d_%02d%02d%02d",
                      MOZ_APP_DISPLAYNAME_STR, time.tm_year, time.tm_month+1,
                      time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec));

  // Identify the print job's destination - it may get changed below.
  // Note that we ignore print preview for now and set it below.
  bool tofile = false;
  mPrintSettings->GetPrintToFile(&tofile);
  if (tofile)
    mDestination = printToFile;
  else
    mDestination = printToPrinter;

  // Identify the output format - it may get changed below.
  int16_t outputFormat;
  mPrintSettings->GetOutputFormat(&outputFormat);

  // If there's already a filename, check its extension.  If it's
  // missing, add it;  if it doesn't match the format, change the format.
  nsXPIDLString fileName;
  mPrintSettings->GetToFileName(getter_Copies(fileName));
  if (!fileName.IsEmpty())
    outputFormat = AdjustFormatAndExtension(outputFormat, fileName);

  // Adjust the destination based on the format.  In some cases,
  // the format will also be changed based on other constraints.
  outputFormat = AdjustDestinationForFormat(outputFormat, driverType);

  // Now that the final output format is known,
  // override the destination if this is a print preview.
  if (aIsPrintPreview)
    mDestination = printPreview;

  // If the destination is a file, update Print Settings after ensuring
  // there's a filename.  The only reason the name would be empty at this
  // point is if we forced print-to-file because we couldn't generate
  // native output.  Consequently, changing the file's extension to ",ps"
  // will cause Cairo's PS generator to be used, not the native driver's.

  if (mDestination == printToFile) {

    if (fileName.IsEmpty()) {
      CopyASCIItoUTF16(mDefaultName, fileName);

      if (outputFormat == nsIPrintSettings::kOutputFormatPS)
        fileName.Append(NS_LITERAL_STRING(".ps"));
      else
        fileName.Append(NS_LITERAL_STRING(".pdf"));

      nsresult rv = GetFileNameForPrintSettings(mPrintSettings, fileName);
      NS_ENSURE_SUCCESS(rv, rv);
      outputFormat = AdjustFormatAndExtension(outputFormat, fileName);
    }

    mPrintSettings->SetPrintToFile(true);
    mPrintSettings->SetToFileName(fileName.get());
  }

  // Save the final choice of format.
  mPrintSettings->SetOutputFormat(outputFormat);

  // Preserve the driver settings by making a copy
  // of the os2PrintQ object we're going to use.
  mQueue = sPrinterList.ClonePrintQ(printerNdx);

#ifdef debug_thebes_print
  if (outputFormat == nsIPrintSettings::kOutputFormatNative)
    printf("Init - format= Native  dest= %s\n",
           (mDestination == printToPrinter ? "Printer" :
            (mDestination == printPreview ?  "Preview" :
             NS_LossyConvertUTF16toASCII(fileName).get())));
  else
    printf("Init - format= %s  dest= %s\n",
           (outputFormat == nsIPrintSettings::kOutputFormatPS ? "PS" : "PDF"),
           (mDestination == printToPrinter ? "Printer" :
            (mDestination == printPreview ?  "Preview" :
             NS_LossyConvertUTF16toASCII(fileName).get())));

  printf("Page size =  %.2f x %.2f twips - %d x %d pixels\n"
         "Margins   =  lt= %d  rt= %d  tp= %d  bt= %d twips\n",
         NS_MILLIMETERS_TO_TWIPS(hci.cx),
         NS_MILLIMETERS_TO_TWIPS(hci.cy),
         mXPixels, mYPixels,
         margin.left, margin.right, margin.top, margin.bottom);
#endif

  DBGX();
  return NS_OK;
}

//---------------------------------------------------------------------------

int16_t nsDeviceContextSpecOS2::AdjustDestinationForFormat(int16_t aFormat,
                                                           long driverType)
{
  // PDF format - redirect to file
  if (aFormat == nsIPrintSettings::kOutputFormatPDF) {
    mDestination = printToFile;
    return aFormat;
  }

  // Determine whether to use the native or builtin PS generator
#if 1
  // With native GPI printing temporarily disabled, we always want Cairo
  // (builtin) PS generator, see #171.
  enum { useBuiltinPS = true };
#else
  bool useBuiltinPS = false;
  if (!NS_SUCCEEDED(Preferences::GetBool(kOS2UseBuiltinPS, &useBuiltinPS)))
    Preferences::SetBool(kOS2UseBuiltinPS, false);
#endif

  // Postscript format - If the driver can't handle PS, redirect to file.
  // If it can and the user wants native support, change the format.
  // Otherwise, leave as-is
  if (aFormat == nsIPrintSettings::kOutputFormatPS) {
    if (driverType != CAPS_TECH_POSTSCRIPT)
      mDestination = printToFile;
    else
    if (!useBuiltinPS)
      aFormat = nsIPrintSettings::kOutputFormatNative;
    return aFormat;
  }

  // Unknown format - change format to PDF & redirect to file
  if (aFormat != nsIPrintSettings::kOutputFormatNative) {
    mDestination = printToFile;
    return nsIPrintSettings::kOutputFormatPDF;
  }

  // Native format - driver handles PS but user wants Cairo's PS,
  // so change the format.
  if (driverType == CAPS_TECH_POSTSCRIPT && useBuiltinPS)
    return nsIPrintSettings::kOutputFormatPS;

  return aFormat;
}

//---------------------------------------------------------------------------

// If there's a filename, let its extension force a change of format.
// If there isn't an appropriate extension, set one.

static
int16_t   AdjustFormatAndExtension(int16_t aFormat, nsAString& aFileName)
{
  if (StringEndsWith(aFileName, NS_LITERAL_STRING(".pdf"),
                     nsCaseInsensitiveStringComparator())) {
    aFormat = nsIPrintSettings::kOutputFormatPDF;
  }
  else
  if (StringEndsWith(aFileName, NS_LITERAL_STRING(".ps"),
                     nsCaseInsensitiveStringComparator())) {
    aFormat = nsIPrintSettings::kOutputFormatPS;
  }
  else
  if (aFormat == nsIPrintSettings::kOutputFormatPDF) {
    aFileName.Append(NS_LITERAL_STRING(".pdf"));
  }
  else
  if (aFormat == nsIPrintSettings::kOutputFormatPS) {
    aFileName.Append(NS_LITERAL_STRING(".ps"));
  }

  return aFormat;
}

//---------------------------------------------------------------------------

static
nsresult  GetFileNameForPrintSettings(nsIPrintSettings* aPS,
                                      nsAString& aFileName)
{
  nsresult rv;

  nsCOMPtr<nsIFilePicker> filePicker =
      do_CreateInstance("@mozilla.org/filepicker;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIStringBundleService> bundleService =
    mozilla::services::GetStringBundleService();
  if (!bundleService)
    return NS_ERROR_FAILURE;
  nsCOMPtr<nsIStringBundle> bundle;
  rv = bundleService->CreateBundle(NS_ERROR_GFX_PRINTER_BUNDLE_URL,
                                   getter_AddRefs(bundle));
  NS_ENSURE_SUCCESS(rv, rv);

  nsXPIDLString title;
  rv = bundle->GetStringFromName(NS_LITERAL_STRING("PrintToFile").get(),
                                 getter_Copies(title));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIWindowWatcher> wwatch =
    (do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMWindow> window;
  wwatch->GetActiveWindow(getter_AddRefs(window));

  rv = filePicker->Init(window, title, nsIFilePicker::modeSave);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = filePicker->AppendFilters(nsIFilePicker::filterAll);
  NS_ENSURE_SUCCESS(rv, rv);

  filePicker->SetDefaultString(aFileName);

  int16_t dialogResult;
  filePicker->Show(&dialogResult);

  if (dialogResult == nsIFilePicker::returnCancel) {
    return NS_ERROR_ABORT;
  }

  nsCOMPtr<nsIFile> localFile;
  rv = filePicker->GetFile(getter_AddRefs(localFile));
  NS_ENSURE_SUCCESS(rv, rv);

  if (dialogResult == nsIFilePicker::returnReplace) {
    // be extra safe and only delete when the file is really a file
    bool isFile;
    rv = localFile->IsFile(&isFile);
    if (NS_SUCCEEDED(rv) && isFile) {
      rv = localFile->Remove(false /* recursive delete */);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  rv = localFile->GetPath(aFileName);
  NS_ENSURE_SUCCESS(rv,rv);

  if (aFileName.IsEmpty()) {
    rv = NS_ERROR_ABORT;
  }

  return rv;
}

//---------------------------------------------------------------------------

NS_IMETHODIMP nsDeviceContextSpecOS2::GetSurfaceForPrinter(gfxASurface **surface)
{
  DBGN();
  NS_ENSURE_ARG_POINTER(mQueue);

  *surface = nullptr;

#ifdef debug_thebes_print
  printf("GetSurface - Driver= %s  Device= %s\n  "
         "Printer= %s  QName= %s  QTitle= %s\n",
         mQueue->DriverName(), mQueue->DeviceName(),
         mQueue->PrinterName(), mQueue->QueueName(), mQueue->QueueTitle());
#endif

  int16_t outputFormat;
  mPrintSettings->GetOutputFormat(&outputFormat);

  RefPtr<gfxASurface> newSurface;

//*** PDF
  if (outputFormat == nsIPrintSettings::kOutputFormatPDF) {
    nsCOMPtr<nsIOutputStream> stream;
    nsresult rv = CreateStreamForFormat(outputFormat, getter_AddRefs(stream));
    NS_ENSURE_SUCCESS(rv, rv);

    // convert inches to points
    double width, height;
    mPrintSettings->GetPaperWidth(&width);
    mPrintSettings->GetPaperHeight(&height);
    width  *= POINTS_PER_INCH_FLOAT;
    height *= POINTS_PER_INCH_FLOAT;

    newSurface = new(std::nothrow) gfxPDFSurface(stream, gfxSize(width, height));

  } else

//*** PostScript
  if (outputFormat == nsIPrintSettings::kOutputFormatPS) {

    nsCOMPtr<nsIOutputStream> stream;
    nsresult rv = CreateStreamForFormat(outputFormat, getter_AddRefs(stream));
    NS_ENSURE_SUCCESS(rv, rv);

    // convert inches to points
    double width, height;
    mPrintSettings->GetPaperWidth(&width);
    mPrintSettings->GetPaperHeight(&height);
    width  *= POINTS_PER_INCH_FLOAT;
    height *= POINTS_PER_INCH_FLOAT;

    int32_t orientation;
    mPrintSettings->GetOrientation(&orientation);

    newSurface = new(std::nothrow) gfxPSSurface(stream, gfxSize(width, height),
                      (orientation == nsIPrintSettings::kLandscapeOrientation  ?
                       gfxPSSurface::LANDSCAPE : gfxPSSurface::PORTRAIT));

    if (newSurface)
      static_cast<gfxPSSurface*>(newSurface.get())->SetDPI(double(mXDpi), double(mYDpi));
  }

//*** Native
#if 0 // This is temporariy disabled, see #171.
  else {
    char* filePath = nullptr;
    if (mDestination == printToFile) {
      char16_t* fileName;

      mPrintSettings->GetToFileName(&fileName);
      filePath = GetACPString(fileName);
      free(fileName);
    }

    mPrintingStarted = true;
    mPrintDC = PrnOpenDC(mQueue, "Mozilla", mCopies, mDestination, filePath);
    NS_ENSURE_TRUE(mPrintDC, NS_ERROR_FAILURE);

    if (filePath)
      free(filePath);

    newSurface = new(std::nothrow)
      gfxOS2Surface(mPrintDC, gfxIntSize(int(mXPixels), int(mYPixels)),
                    (mDestination == printPreview));
  }
#endif

  if (!newSurface) {
    DBGM("new gfxOS2Surface failed");
    return NS_ERROR_FAILURE;
  }

  *surface = newSurface;
  NS_ADDREF(*surface);

  DBGX();
  return NS_OK;
}

//---------------------------------------------------------------------------

nsresult nsDeviceContextSpecOS2::CreateStreamForFormat(int16_t aFormat,
                                                       nsIOutputStream **aStream)
{
  DBGN();

  nsresult rv;

  // This function only works for PS and PDF surfaces
  if (aFormat != nsIPrintSettings::kOutputFormatPS &&
      aFormat != nsIPrintSettings::kOutputFormatPDF) {
    return NS_ERROR_ILLEGAL_VALUE;
  }

  // For print preview, create a null output stream to prevent creation
  // of a PS or PDF file that contains only header info and no content.
  if (mDestination == printPreview) {
    os2NullOutputStream * nullStream = new os2NullOutputStream();
    NS_ENSURE_TRUE(nullStream, NS_ERROR_FAILURE);

    nsCOMPtr<nsIOutputStream> stream(do_QueryInterface(nullStream, &rv));
    NS_ENSURE_SUCCESS(rv, rv);

    *aStream = stream;
    NS_ADDREF(*aStream);
    return NS_OK;
  }

  // For print to printer using Cairo's Postscript output,
  // create a stream that will write directly to the spooler.
  if (mDestination == printToPrinter) {
    mSpoolerStream = new os2SpoolerStream();
    NS_ENSURE_TRUE(mSpoolerStream, NS_ERROR_FAILURE);
    rv = mSpoolerStream->Init(mQueue, mDefaultName.get());
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIOutputStream> stream(do_QueryInterface(mSpoolerStream, &rv));
    NS_ENSURE_SUCCESS(rv, rv);

    *aStream = stream;
    NS_ADDREF(*aStream);
    return NS_OK;
  }

  // For print to file, create a file output stream using
  // the name set in Init().
  nsXPIDLString fileName;
  mPrintSettings->GetToFileName(getter_Copies(fileName));
  if (fileName.IsEmpty()) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsILocalFile> file =
      do_CreateInstance("@mozilla.org/file/local;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = file->InitWithPath(fileName);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFileOutputStream> fileStream =
      do_CreateInstance("@mozilla.org/network/file-output-stream;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = fileStream->Init(file, -1, -1, 0);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIOutputStream> stream(do_QueryInterface(fileStream, &rv));
  NS_ENSURE_SUCCESS(rv, rv);
  *aStream = stream;
  NS_ADDREF(*aStream);

  DBGX();

  return rv;
}

//---------------------------------------------------------------------------
// Helper function to convert the string to the native codepage,
// similar to UnicodeToCodepage() in nsDragService.cpp.

#if 0 // This is temporariy disabled, see #171.
static
char* GetACPString(const char16_t* aStr)
{
   return GetACPString(nsDependentString(aStr));
}
#endif

static
char* GetACPString(const nsAString& aStr)
{
   if (aStr.Length() == 0) {
      return nullptr;
   }

   nsAutoCString buf;
   NS_CopyUnicodeToNative(aStr, buf);
   return ToNewCString(buf);
}

//---------------------------------------------------------------------------

NS_IMETHODIMP nsDeviceContextSpecOS2::BeginDocument(const nsAString& aTitle,
                                                    char16_t* aPrintToFileName,
                                                    int32_t aStartPage,
                                                    int32_t aEndPage)
{
  DBGN();

#ifdef debug_thebes_print
  printf("BeginDoc - tile= %s  file= %s  fromPg= %d  toPg= %d\n",
         NS_LossyConvertUTF16toASCII(aTitle).get(),
         NS_LossyConvertUTF16toASCII(nsString(aPrintToFileName)).get(),
         aStartPage, aEndPage);
#endif

  if (mSpoolerStream) {
    if (! aTitle.IsEmpty ()) {
      char *title = GetACPString(aTitle);
      nsresult rv = mSpoolerStream->BeginDocument(title);
      free(title);
      return rv;
    }
    else
      return mSpoolerStream->BeginDocument(mDefaultName.get());
  }

  // don't try to send device escapes for non-native output (like PDF)
  int16_t outputFormat;
  mPrintSettings->GetOutputFormat(&outputFormat);
  if (outputFormat != nsIPrintSettings::kOutputFormatNative) {
    return NS_OK;
  }

  char *title = GetACPString(aTitle);
  PCSZ pszGenericDocName = "Mozilla Document";
  PCSZ pszDocName = title ? title : pszGenericDocName;
  LONG lResult = DevEscape(mPrintDC, DEVESC_STARTDOC, strlen(pszDocName) + 1,
                           const_cast<BYTE*>(pszDocName), 0, 0);
  mPrintingStarted = true;
  if (title) {
    free(title);
  }

  DBGX();

  return lResult == DEV_OK ? NS_OK : NS_ERROR_GFX_PRINTER_STARTDOC;
}

//---------------------------------------------------------------------------

NS_IMETHODIMP nsDeviceContextSpecOS2::EndDocument()
{
  DBGN();

  // don't try to send device escapes for non-native output (like PDF) but
  // clear the filename to make sure that we don't overwrite it next time
  int16_t outputFormat;
  mPrintSettings->GetOutputFormat(&outputFormat);

  mPages = 0;

  if (outputFormat != nsIPrintSettings::kOutputFormatNative) {
    mPrintSettings->SetToFileName(nullptr);
    nsCOMPtr<nsIPrintSettingsService> pss =
      do_GetService("@mozilla.org/gfx/printsettings-service;1");
    if (pss)
      pss->SavePrintSettingsToPrefs(mPrintSettings, true,
                                    nsIPrintSettings::kInitSaveToFileName);
    DBGX();
    return NS_OK;
  }

  USHORT usJobID = 0;
  LONG   lOutCount = sizeof(usJobID);
  LONG   lResult = DevEscape(mPrintDC, DEVESC_ENDDOC, 0, 0,
                             &lOutCount, (PBYTE)&usJobID);

  DBGX();

  return lResult == DEV_OK ? NS_OK : NS_ERROR_GFX_PRINTER_ENDDOC;
}

//---------------------------------------------------------------------------

NS_IMETHODIMP nsDeviceContextSpecOS2::BeginPage()
{
  DBGN();

  mPages++;
  if (mSpoolerStream)
    mSpoolerStream->IncPageCount();
#ifdef debug_thebes_print
  printf("BeginPage - nbr= %d\n", mPages);
#endif

  int16_t outputFormat;
  mPrintSettings->GetOutputFormat(&outputFormat);
  if (outputFormat != nsIPrintSettings::kOutputFormatNative) {
    DBGX();
    return NS_OK;
  }

  if (mPrintingStarted) {
    // we don't want an extra page break at the start of the document
    mPrintingStarted = false;
    DBGX();
    return NS_OK;
  }

  LONG lResult = DevEscape(mPrintDC, DEVESC_NEWFRAME, 0, 0, 0, 0);

  DBGX();
  return lResult == DEV_OK ? NS_OK : NS_ERROR_GFX_PRINTER_STARTPAGE;
}

//---------------------------------------------------------------------------

NS_IMETHODIMP nsDeviceContextSpecOS2::EndPage()
{
  DBGNX();

  return NS_OK;
}

//---------------------------------------------------------------------------

nsresult nsDeviceContextSpecOS2::SetPrintSettingsFromDevMode(
                                          nsIPrintSettings* aPrintSettings,
                                          ULONG printer)
{
  if (!aPrintSettings)
   return NS_ERROR_FAILURE;

  int bufferSize = 3 * sizeof(DJP_ITEM);
  PBYTE pDJP_Buffer = new BYTE[bufferSize];
  memset(pDJP_Buffer, 0, bufferSize);
  PDJP_ITEM pDJP = (PDJP_ITEM) pDJP_Buffer;

  HDC hdc = sPrinterList.OpenHDC(printer);

  //Get Number of Copies from Job Properties
  pDJP->lType = DJP_CURRENT;
  pDJP->cb = sizeof(DJP_ITEM);
  pDJP->ulNumReturned = 1;
  pDJP->ulProperty = DJP_SJ_COPIES;
  pDJP->ulValue = 1;
  pDJP++;

  //Get Orientation from Job Properties
  pDJP->lType = DJP_CURRENT;
  pDJP->cb = sizeof(DJP_ITEM);
  pDJP->ulNumReturned = 1;
  pDJP->ulProperty = DJP_SJ_ORIENTATION;
  pDJP->ulValue = 1;
  pDJP++;

  pDJP->lType = DJP_NONE;
  pDJP->cb = sizeof(DJP_ITEM);
  pDJP->ulNumReturned = 1;
  pDJP->ulProperty = 0;
  pDJP->ulValue = 0;

  LONG driverSize = sPrinterList.GetDriverDataSize(printer);
  LONG rc = GreEscape(hdc, DEVESC_QUERYJOBPROPERTIES, bufferSize, pDJP_Buffer,
                      &driverSize, PBYTE(sPrinterList.GetDriverData(printer)));

  pDJP = (PDJP_ITEM)pDJP_Buffer;
  if (rc == DEV_OK || rc == DEV_WARNING) {
    while (pDJP->lType != DJP_NONE) {
      if (pDJP->ulProperty == DJP_SJ_ORIENTATION && pDJP->lType > 0) {
        if (pDJP->ulValue == DJP_ORI_PORTRAIT ||
            pDJP->ulValue == DJP_ORI_REV_PORTRAIT)
          aPrintSettings->SetOrientation(nsIPrintSettings::kPortraitOrientation);
        else
          aPrintSettings->SetOrientation(nsIPrintSettings::kLandscapeOrientation);
      }
      if ((pDJP->ulProperty == DJP_SJ_COPIES) && (pDJP->lType > 0)){
        aPrintSettings->SetNumCopies(int32_t(pDJP->ulValue));
      }
      pDJP = DJP_NEXT_STRUCTP(pDJP);
    }
  }

  delete [] pDJP_Buffer;
  DevCloseDC(hdc);

  return NS_OK;
}

//---------------------------------------------------------------------------

static
void  SetDevModeFromSettings(ULONG printer, nsIPrintSettings* aPrintSettings)
{
  if (!aPrintSettings)
    return;

  int bufferSize = 3 * sizeof(DJP_ITEM);
  PBYTE pDJP_Buffer = new BYTE[bufferSize];
  memset(pDJP_Buffer, 0, bufferSize);
  PDJP_ITEM pDJP = (PDJP_ITEM) pDJP_Buffer;

  HDC hdc = sPrinterList.OpenHDC(printer);
  char* driver = sPrinterList.GetDriverName(printer);

  // Setup Orientation
  int32_t orientation;
  aPrintSettings->GetOrientation(&orientation);
  if (!strcmp(driver, "LASERJET"))
    pDJP->lType = DJP_ALL;
  else
    pDJP->lType = DJP_CURRENT;
  pDJP->cb = sizeof(DJP_ITEM);
  pDJP->ulNumReturned = 1;
  pDJP->ulProperty = DJP_SJ_ORIENTATION;
  pDJP->ulValue = (orientation == nsIPrintSettings::kPortraitOrientation) ?
                  DJP_ORI_PORTRAIT : DJP_ORI_LANDSCAPE;
  pDJP++;

  // Setup Number of Copies
  int32_t copies;
  aPrintSettings->GetNumCopies(&copies);
  pDJP->cb = sizeof(DJP_ITEM);
  pDJP->lType = DJP_CURRENT;
  pDJP->ulNumReturned = 1;
  pDJP->ulProperty = DJP_SJ_COPIES;
  pDJP->ulValue = copies;
  pDJP++;

  pDJP->cb = sizeof(DJP_ITEM);
  pDJP->lType = DJP_NONE;
  pDJP->ulNumReturned = 1;
  pDJP->ulProperty = 0;
  pDJP->ulValue = 0;

  LONG driverSize = sPrinterList.GetDriverDataSize(printer);
  GreEscape (hdc, DEVESC_SETJOBPROPERTIES,
             bufferSize, pDJP_Buffer, &driverSize,
             PBYTE(sPrinterList.GetDriverData(printer)));

  delete [] pDJP_Buffer;
  DevCloseDC(hdc);
}

//---------------------------------------------------------------------------
//  nsPrinterEnumeratorOS2 implementation
//---------------------------------------------------------------------------

nsPrinterEnumeratorOS2::nsPrinterEnumeratorOS2()
{
DBGNX();
}

nsPrinterEnumeratorOS2::~nsPrinterEnumeratorOS2()
{
DBGNX();
}

NS_IMPL_ISUPPORTS(nsPrinterEnumeratorOS2, nsIPrinterEnumerator)

NS_IMETHODIMP nsPrinterEnumeratorOS2::GetPrinterNameList(
                                      nsIStringEnumerator **aPrinterNameList)
{
  NS_ENSURE_ARG_POINTER(aPrinterNameList);
  *aPrinterNameList = nullptr;

  sPrinterList.RefreshPrintQueue();

  ULONG numPrinters = sPrinterList.GetNumPrinters();
  nsTArray<nsString> *printers = new nsTArray<nsString>(numPrinters);
  if (!printers)
    return NS_ERROR_OUT_OF_MEMORY;

  ULONG count = 0;
  while (count < numPrinters) {
    printers->AppendElement(*sPrinterList.GetPrinterTitle(count++));
  }

  return NS_NewAdoptingStringEnumerator(aPrinterNameList, printers);
}

NS_IMETHODIMP nsPrinterEnumeratorOS2::GetDefaultPrinterName(
                                          char16_t * *aDefaultPrinterName)
{
  NS_ENSURE_ARG_POINTER(aDefaultPrinterName);

  nsAString *aPrinterName = sPrinterList.GetPrinterTitle(0);

  if (aPrinterName != nullptr)
    *aDefaultPrinterName = ToNewUnicode(*aPrinterName);
  else
    *aDefaultPrinterName = ToNewUnicode(EmptyString());

  return NS_OK;
}

NS_IMETHODIMP nsPrinterEnumeratorOS2::InitPrintSettingsFromPrinter(
                                          const char16_t *aPrinterName,
                                          nsIPrintSettings *aPrintSettings)
{
  NS_ENSURE_ARG_POINTER(aPrinterName);
  NS_ENSURE_ARG_POINTER(aPrintSettings);

  if (!*aPrinterName)
    return NS_OK;

  int32_t index = sPrinterList.GetPrinterIndex(aPrinterName);
  if (index < 0)
    return NS_ERROR_FAILURE;

  nsDeviceContextSpecOS2::SetPrintSettingsFromDevMode(aPrintSettings, index);
  aPrintSettings->SetIsInitializedFromPrinter(true);

  return NS_OK;
}

NS_IMETHODIMP nsPrinterEnumeratorOS2::DisplayPropertiesDlg(
                                          const char16_t *aPrinterName,
                                          nsIPrintSettings *aPrintSettings)
{
  int32_t index = sPrinterList.GetPrinterIndex(aPrinterName);
  if (index < 0)
    return NS_ERROR_FAILURE;

  SetDevModeFromSettings(index, aPrintSettings);
  if (sPrinterList.ShowProperties(index)) {
    nsDeviceContextSpecOS2::SetPrintSettingsFromDevMode(aPrintSettings, index);
    return NS_OK;
  }

  return NS_ERROR_FAILURE;
}

//---------------------------------------------------------------------------
//  os2NullOutputStream implementation
//---------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(os2NullOutputStream, nsIOutputStream)

os2NullOutputStream::os2NullOutputStream()
{
  DBGNX();
#ifdef debug_enterexit
  mWrites = 0;
#endif
}

NS_IMETHODIMP os2NullOutputStream::Close()
{
  DBGNX();
  return NS_OK;
}

NS_IMETHODIMP os2NullOutputStream::Flush()
{
  DBGNX();
  return NS_OK;
}

NS_IMETHODIMP os2NullOutputStream::Write(const char *aBuf, uint32_t aCount,
                                         uint32_t *_retval)
{
#ifdef debug_enterexit
  if (!mWrites) {
    mWrites++;
    DBGNX();
  }
#endif
    *_retval = aCount;
    return NS_OK;
}

NS_IMETHODIMP os2NullOutputStream::WriteFrom(nsIInputStream *aFromStream,
                                             uint32_t aCount,
                                             uint32_t *_retval)
{
    DBGNX();
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP os2NullOutputStream::WriteSegments(nsReadSegmentFun aReader,
                                                 void *aClosure, uint32_t aCount,
                                                 uint32_t *_retval)
{
    DBGNX();
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP os2NullOutputStream::IsNonBlocking(bool *_retval)
{
    DBGNX();
    *_retval = true;
    return NS_OK;
}

//---------------------------------------------------------------------------
//  os2SpoolerStream implementation
//---------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(os2SpoolerStream, nsIOutputStream)

os2SpoolerStream::os2SpoolerStream()
  : mSpl(0), mPages(0)
{
  DBGNX();
#ifdef debug_enterexit
  mWrites = 0;
#endif
}

nsresult os2SpoolerStream::Init(os2PrintQ* aQueue, const char* aTitle)
{
  DBGN();

  ULONG   rc;
  ULONG   ulSize = 0;

  // Determine whether to use IBMNULL or the native driver
  bool useIbmNull = true;
  if (!NS_SUCCEEDED(Preferences::GetBool(kOS2UseIbmNull, &useIbmNull)))
    Preferences::SetBool(kOS2UseIbmNull, true);

  // Sending Moz's Postscript output through the native PS driver adds printer
  // feature settings at the beginning which may make it unusable for CUPS.
  // To work around this, we send it through IBMNULL which won't add anything.
  // However, IBMNULL must be associated with the target printer, and it's
  // unlikely that the user would have the printer setup that way.   So...
  // this adds IBMNULL to the printer's list of drivers.

  if (useIbmNull) {
    // Get a PRDINFO3 for this printer and save its list of associated drivers.
    rc = SplQueryDevice("", aQueue->PrinterName(), 3, 0, 0, &ulSize);
    if (rc && rc != NERR_BufTooSmall) {
#ifdef debug_thebes_print
      printf("SplQueryDevice for bufsize failed - rc= %ld\n", rc);
#endif
      return rc == NERR_DestNotFound ?
             NS_ERROR_GFX_PRINTER_NAME_NOT_FOUND : NS_ERROR_FAILURE;
    }

    PRDINFO3 * pInfo = (PRDINFO3*)malloc(ulSize);
    NS_ENSURE_TRUE(pInfo, NS_ERROR_OUT_OF_MEMORY);

    rc = SplQueryDevice("", aQueue->PrinterName(), 3, pInfo, ulSize, &ulSize);
    if (rc) {
      free(pInfo);
#ifdef debug_thebes_print
      printf("SplQueryDevice failed - rc= %ld\n", rc);
#endif
      return rc == NERR_DestNotFound ?
             NS_ERROR_GFX_PRINTER_NAME_NOT_FOUND : NS_ERROR_FAILURE;
    }

    nsCString drivers(pInfo->pszDrivers);
    free(pInfo);

    // If the list doesn't include IBMNULL, add it.
    if (!FindInReadable(NS_LITERAL_CSTRING("IBMNULL"), drivers)) {
      drivers.AppendLiteral(",IBMNULL");
      rc = SplSetDevice("", aQueue->PrinterName(), 3, (void*)drivers.get(),
                        drivers.Length() + 1, PRD_DRIVERS_PARMNUM);
      if (rc) {
#ifdef debug_thebes_print
        printf("SplSetDevice failed - rc= %ld\n", rc);
#endif
        return NS_ERROR_FAILURE;
      }
    }
  }

  PDEVOPENSTRUC pData  = (PDEVOPENSTRUC)calloc(1, sizeof(DEVOPENSTRUC));
  pData->pszLogAddress = const_cast<PSZ>(aQueue->QueueName());
  pData->pszDataType   = const_cast<PSZ>("PM_Q_RAW");
  pData->pszComment    = const_cast<PSZ>(aTitle);

  if (useIbmNull) {
    pData->pszDriverName = const_cast<PSZ>("IBMNULL");
  } else {
    pData->pszDriverName = (char*)aQueue->DriverName();
    pData->pdriv         = aQueue->DriverData();
  }

#ifdef debug_thebes_print
  printf("os2SpoolerStream will use '%s' - pdriv= %lx\n",
         pData->pszDriverName, (ULONG)pData->pdriv);
#endif

  mSpl = SplQmOpen("*", 5L, (PQMOPENDATA)pData);
  free(pData);
  if (!mSpl) {
#ifdef debug_thebes_print
    printf("SplQmOpen failed\n");
#endif
    return WinGetLastError(0) == PMERR_SPL_QUEUE_NOT_FOUND ?
           NS_ERROR_GFX_PRINTER_NAME_NOT_FOUND : NS_ERROR_FAILURE;
  }

  DBGX();

  return NS_OK;
}

nsresult os2SpoolerStream::BeginDocument(const char* aTitle)
{
  DBGN();

  // tag print job with app title
  if (!SplQmStartDoc(mSpl, aTitle)) {
#ifdef debug_thebes_print
    printf("SplQmStartDoc failed\n");
#endif
    SplQmAbort(mSpl);
    return NS_ERROR_GFX_PRINTER_STARTDOC;
  }

  if (!SplQmNewPage(mSpl, 1)) {
#ifdef debug_thebes_print
    printf("SplQmNewPage failed\n");
#endif
    SplQmAbort(mSpl);
    return NS_ERROR_GFX_PRINTER_STARTDOC;
  }

  DBGX();

  return NS_OK;
}

NS_IMETHODIMP os2SpoolerStream::Close()
{
  DBGN();

  DebugOnly <BOOL> rc = SplQmNewPage(mSpl, mPages);
#ifdef debug_thebes_print
  if (!rc) {
    printf("SplQmNewPage failed setting page count - continuing\n");
  }
#endif

  ULONG ulJobID = SplQmEndDoc(mSpl);
  if (ulJobID == SPL_ERROR) {
#ifdef debug_thebes_print
    printf("SplQmEndDoc failed\n");
#endif
    SplQmAbort(mSpl);
    return NS_ERROR_GFX_PRINTER_ENDDOC;
  }

  rc = SplQmClose(mSpl);
#ifdef debug_thebes_print
  printf("Close spooler - job= %lu  pages= %d - %s\n",
         ulJobID, mPages, rc ? "success" : "failure");
#endif

  DBGX();

  return NS_OK;
}

NS_IMETHODIMP os2SpoolerStream::Flush()
{
  DBGNX();
  return NS_OK;
}

NS_IMETHODIMP os2SpoolerStream::Write(const char *aBuf, uint32_t aCount,
                                      uint32_t *_retval)
{
#ifdef debug_enterexit
  if (!mWrites)
    DBGN();
#endif

  uint32_t total = 0;
  uint32_t write;

  while (aCount) {
    // writes must be < 64k
    write = (aCount > 0xF000) ? 0xF000 : aCount;
    SplQmWrite(mSpl, write, (void*)&aBuf[total]);
    total  += write;
    aCount -= write;
  }

  *_retval = total;

#ifdef debug_enterexit
  if (!mWrites) {
    mWrites++;
    DBGX();
  }
#endif

  return NS_OK;
}

NS_IMETHODIMP os2SpoolerStream::WriteFrom(nsIInputStream *aFromStream,
                                          uint32_t aCount,
                                          uint32_t *_retval)
{
    DBGNX();
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP os2SpoolerStream::WriteSegments(nsReadSegmentFun aReader,
                                              void *aClosure, uint32_t aCount,
                                              uint32_t *_retval)
{
    DBGNX();
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP os2SpoolerStream::IsNonBlocking(bool *_retval)
{
    DBGNX();
    *_retval = false;
    return NS_OK;
}

//---------------------------------------------------------------------------

