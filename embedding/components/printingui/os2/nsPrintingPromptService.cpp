/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsPrintingPromptService.h"

#include "nsIComponentManager.h"
#include "nsIDialogParamBlock.h"
#include "nsIDOMWindow.h"
#include "nsIServiceManager.h"
#include "nsISupportsUtils.h"
#include "nsISupportsArray.h"
#include "nsString.h"

// used to fix ShowPrintPrompt() bug when printing mail/news messages
#include "nsPIDOMWindow.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIBaseWindow.h"
#include "nsIWidget.h"

// Printing Progress Includes
#include "nsPrintProgress.h"
#include "nsPrintProgressParams.h"

// Print Service Includes
#include "nsIPrintOptions.h"
#include "nsIServiceManager.h"
#include "nsGfxCIID.h"
static const char sPrintOptionsContractID[] = "@mozilla.org/gfx/printsettings-service;1";

static const char *kPrintDialogURL         = "chrome://global/content/printdialog.xul";
static const char *kPrintProgressDialogURL = "chrome://global/content/printProgress.xul";
static const char *kPrtPrvProgressDialogURL = "chrome://global/content/printPreviewProgress.xul";
static const char *kPageSetupDialogURL     = "chrome://global/content/printPageSetup.xul";

/****************************************************************
 ************************* ParamBlock ***************************
 ****************************************************************/

class ParamBlock {

public:
    ParamBlock() 
    {
        mBlock = 0;
    }
    ~ParamBlock() 
    {
        NS_IF_RELEASE(mBlock);
    }
    nsresult Init() {
      return CallCreateInstance(NS_DIALOGPARAMBLOCK_CONTRACTID, &mBlock);
    }
    nsIDialogParamBlock * operator->() const { return mBlock; }
    operator nsIDialogParamBlock * const ()  { return mBlock; }

private:
    nsIDialogParamBlock *mBlock;
};

/****************************************************************
 ***************** nsPrintingPromptService **********************
 ****************************************************************/

NS_IMPL_ISUPPORTS(nsPrintingPromptService, nsIPrintingPromptService, nsIWebProgressListener)

nsPrintingPromptService::nsPrintingPromptService() 
{
}

nsPrintingPromptService::~nsPrintingPromptService() 
{
}

nsresult
nsPrintingPromptService::Init()
{
    nsresult rv;
    mWatcher = do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
    return rv;
}

/* void showPrintDialog (in nsIDOMWindow parent, in nsIWebBrowserPrint webBrowserPrint, in nsIPrintSettings printSettings); */
NS_IMETHODIMP 
nsPrintingPromptService::ShowPrintDialog(nsIDOMWindow *parent, nsIWebBrowserPrint *webBrowserPrint, nsIPrintSettings *printSettings)
{
    NS_ENSURE_ARG(webBrowserPrint);
    NS_ENSURE_ARG(printSettings);

    ParamBlock block;
    nsresult rv = block.Init();
    if (NS_FAILED(rv))
      return rv;

    block->SetInt(0, 0);

    // nsWindowWatcher->OpenWindow() will fail if |aParent| is an invisible
    // window with no chrome.  When printing mailnews messages, |aParent|
    // fits that description.  As a workaround, this tests for its visibility
    // and zeroes it out if invisible, causing DoDialog() to reset it to
    // the active window.  This is typically the main mailnews window which
    // is what we wanted anyway.  If there is no active window, the dialog
    // will still be shown but it won't be modal.

    if (parent) {
      nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(parent);
      if (window) {
        nsIDocShell *docshell = window->GetDocShell();
        if (docshell) {
          nsCOMPtr<nsIDocShellTreeItem> treeItem = do_QueryInterface(docshell);
          if (treeItem) {
            nsCOMPtr<nsIDocShellTreeOwner> parentTreeOwner;
            treeItem->GetTreeOwner(getter_AddRefs(parentTreeOwner));
            if (parentTreeOwner) {
              nsCOMPtr<nsIBaseWindow> parentWindow = do_QueryInterface(parentTreeOwner);
              if (parentWindow) {
                nsCOMPtr<nsIWidget> parentWidget;
                parentWindow->GetMainWidget(getter_AddRefs(parentWidget));
                if (parentWidget) {
                  bool parentVisible = parentWidget->IsVisible();
                  if (!parentVisible)
                    parent = 0;
                }
              }
            }
          }
        }
      }
    }

    return DoDialog(parent, block, webBrowserPrint, printSettings, kPrintDialogURL);
}

/* void showProgress (in nsIDOMWindow parent, in nsIWebBrowserPrint webBrowserPrint, in nsIPrintSettings printSettings, in nsIObserver openDialogObserver, in boolean isForPrinting, out nsIWebProgressListener webProgressListener, out nsIPrintProgressParams printProgressParams, out boolean notifyOnOpen); */
NS_IMETHODIMP 
nsPrintingPromptService::ShowProgress(nsIDOMWindow*            parent, 
                                      nsIWebBrowserPrint*      webBrowserPrint,    // ok to be null
                                      nsIPrintSettings*        printSettings,      // ok to be null
                                      nsIObserver*             openDialogObserver, // ok to be null
                                      bool                     isForPrinting,
                                      nsIWebProgressListener** webProgressListener,
                                      nsIPrintProgressParams** printProgressParams,
                                      bool*                  notifyOnOpen)
{
    NS_ENSURE_ARG(webProgressListener);
    NS_ENSURE_ARG(printProgressParams);
    NS_ENSURE_ARG(notifyOnOpen);

    *notifyOnOpen = false;

    nsPrintProgress* prtProgress = new nsPrintProgress();
    mPrintProgress = prtProgress;
    mWebProgressListener = prtProgress;

    nsCOMPtr<nsIPrintProgressParams> prtProgressParams = new nsPrintProgressParams();
      
    nsCOMPtr<nsIDOMWindow> parentWindow = parent;

    if (mWatcher && !parentWindow) {
        mWatcher->GetActiveWindow(getter_AddRefs(parentWindow));
    }

    if (parentWindow) {
        mPrintProgress->OpenProgressDialog(parentWindow,
                                           isForPrinting ? kPrintProgressDialogURL : kPrtPrvProgressDialogURL,
                                           prtProgressParams, openDialogObserver, notifyOnOpen);
    }

    prtProgressParams.forget(printProgressParams);
    nsCOMPtr<nsIWebProgressListener> myWebProgressListener = this;
    myWebProgressListener.forget(webProgressListener);

    return NS_OK;
}

/* void showPageSetup (in nsIDOMWindow parent, in nsIPrintSettings printSettings); */
NS_IMETHODIMP 
nsPrintingPromptService::ShowPageSetup(nsIDOMWindow *parent, nsIPrintSettings *printSettings, nsIObserver *aObs)
{
    NS_ENSURE_ARG(printSettings);

    ParamBlock block;
    nsresult rv = block.Init();
    if (NS_FAILED(rv))
      return rv;

    block->SetInt(0, 0);
    return DoDialog(parent, block, nullptr, printSettings, kPageSetupDialogURL);
}

/* void showPrinterProperties (in nsIDOMWindow parent, in wstring printerName, in nsIPrintSettings printSettings); */
NS_IMETHODIMP 
nsPrintingPromptService::ShowPrinterProperties(nsIDOMWindow *parent, const char16_t *printerName, nsIPrintSettings *printSettings)
{
  nsresult rv = NS_ERROR_FAILURE;
  nsCOMPtr<nsIPrintOptions> printService = do_GetService(sPrintOptionsContractID, &rv);
  if (NS_SUCCEEDED(rv)) {
    bool displayed;
    rv = printService->DisplayJobProperties(printerName, printSettings, &displayed);
  }
  return rv;
}

nsresult
nsPrintingPromptService::DoDialog(nsIDOMWindow *aParent,
                                  nsIDialogParamBlock *aParamBlock, 
                                  nsIWebBrowserPrint *aWebBrowserPrint, 
                                  nsIPrintSettings* aPS,
                                  const char *aChromeURL)
{
    NS_ENSURE_ARG(aParamBlock);
    NS_ENSURE_ARG(aPS);
    NS_ENSURE_ARG(aChromeURL);

    if (!mWatcher)
        return NS_ERROR_FAILURE;

    nsresult rv = NS_OK;

    // get a parent, if at all possible
    // (though we'd rather this didn't fail, it's OK if it does. so there's
    // no failure or null check.)
    nsCOMPtr<nsIDOMWindow> activeParent; // retain ownership for method lifetime
    if (!aParent) 
    {
        mWatcher->GetActiveWindow(getter_AddRefs(activeParent));
        aParent = activeParent;
    }

    // create a nsISupportsArray of the parameters 
    // being passed to the window
    nsCOMPtr<nsISupportsArray> array;
    NS_NewISupportsArray(getter_AddRefs(array));
    if (!array) return NS_ERROR_FAILURE;

    nsCOMPtr<nsISupports> psSupports(do_QueryInterface(aPS));
    NS_ASSERTION(psSupports, "PrintSettings must be a supports");
    array->AppendElement(psSupports);

    if (aWebBrowserPrint) {
      nsCOMPtr<nsISupports> wbpSupports(do_QueryInterface(aWebBrowserPrint));
      NS_ASSERTION(wbpSupports, "nsIWebBrowserPrint must be a supports");
      array->AppendElement(wbpSupports);
    }

    nsCOMPtr<nsISupports> blkSupps(do_QueryInterface(aParamBlock));
    NS_ASSERTION(blkSupps, "IOBlk must be a supports");
    array->AppendElement(blkSupps);

    nsCOMPtr<nsISupports> arguments(do_QueryInterface(array));
    NS_ASSERTION(array, "array must be a supports");


    nsCOMPtr<nsIDOMWindow> dialog;
    rv = mWatcher->OpenWindow(aParent, aChromeURL, "_blank",
                              "centerscreen,chrome,modal,titlebar", arguments,
                              getter_AddRefs(dialog));

    // if aWebBrowserPrint is not null then we are printing
    // so we want to pass back NS_ERROR_ABORT on cancel
    if (NS_SUCCEEDED(rv) && aWebBrowserPrint) 
    {
        int32_t status;
        aParamBlock->GetInt(0, &status);
        return status == 0?NS_ERROR_ABORT:NS_OK;
    }

    return rv;
}

//////////////////////////////////////////////////////////////////////
// nsIWebProgressListener
//////////////////////////////////////////////////////////////////////

/* void onStateChange (in nsIWebProgress aWebProgress, in nsIRequest aRequest, in unsigned long aStateFlags, in nsresult aStatus); */
NS_IMETHODIMP 
nsPrintingPromptService::OnStateChange(nsIWebProgress *aWebProgress, nsIRequest *aRequest, uint32_t aStateFlags, nsresult aStatus)
{
  if ((aStateFlags & STATE_STOP) && mWebProgressListener) {
    mWebProgressListener->OnStateChange(aWebProgress, aRequest, aStateFlags, aStatus);
    if (mPrintProgress) {
      mPrintProgress->CloseProgressDialog(true);
    }
    mPrintProgress       = nullptr;
    mWebProgressListener = nullptr;
  }
  return NS_OK;
}

/* void onProgressChange (in nsIWebProgress aWebProgress, in nsIRequest aRequest, in long aCurSelfProgress, in long aMaxSelfProgress, in long aCurTotalProgress, in long aMaxTotalProgress); */
NS_IMETHODIMP 
nsPrintingPromptService::OnProgressChange(nsIWebProgress *aWebProgress, nsIRequest *aRequest, int32_t aCurSelfProgress, int32_t aMaxSelfProgress, int32_t aCurTotalProgress, int32_t aMaxTotalProgress)
{
  if (mWebProgressListener) {
    return mWebProgressListener->OnProgressChange(aWebProgress, aRequest, aCurSelfProgress, aMaxSelfProgress, aCurTotalProgress, aMaxTotalProgress);
  }
  return NS_OK;
}

/* void onLocationChange (in nsIWebProgress aWebProgress, in nsIRequest aRequest, in nsIURI location, in unsigned long aFlags); */
NS_IMETHODIMP 
nsPrintingPromptService::OnLocationChange(nsIWebProgress *aWebProgress, nsIRequest *aRequest, nsIURI *location, uint32_t aFlags)
{
  if (mWebProgressListener) {
    return mWebProgressListener->OnLocationChange(aWebProgress, aRequest, location, aFlags);
  }
  return NS_OK;
}

/* void onStatusChange (in nsIWebProgress aWebProgress, in nsIRequest aRequest, in nsresult aStatus, in wstring aMessage); */
NS_IMETHODIMP 
nsPrintingPromptService::OnStatusChange(nsIWebProgress *aWebProgress, nsIRequest *aRequest, nsresult aStatus, const char16_t *aMessage)
{
  if (mWebProgressListener) {
    return mWebProgressListener->OnStatusChange(aWebProgress, aRequest, aStatus, aMessage);
  }
  return NS_OK;
}

/* void onSecurityChange (in nsIWebProgress aWebProgress, in nsIRequest aRequest, in unsigned long state); */
NS_IMETHODIMP 
nsPrintingPromptService::OnSecurityChange(nsIWebProgress *aWebProgress, nsIRequest *aRequest, uint32_t state)
{
  if (mWebProgressListener) {
    return mWebProgressListener->OnSecurityChange(aWebProgress, aRequest, state);
  }
  return NS_OK;
}
