/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsFilePicker_h__
#define nsFilePicker_h__

#define INCL_BASE
#define INCL_PM
#include <os2.h>

#include "nsISimpleEnumerator.h"
#include "nsCOMArray.h"
#include "nsTArray.h"
#include "nsIUnicodeDecoder.h"
#include "nsIUnicodeEncoder.h"
#include "nsBaseFilePicker.h"
#include "nsString.h"

/**
 * Native OS/2 FileSelector wrapper
 */

class nsFilePicker : public nsBaseFilePicker
{
  virtual ~nsFilePicker();
public:
  nsFilePicker(); 

  static void ReleaseGlobals();

  NS_DECL_ISUPPORTS

    // nsIFilePicker (less what's in nsBaseFilePicker)
  NS_IMETHOD GetDefaultString(nsAString& aDefaultString);
  NS_IMETHOD SetDefaultString(const nsAString& aDefaultString);
  NS_IMETHOD GetDefaultExtension(nsAString& aDefaultExtension);
  NS_IMETHOD SetDefaultExtension(const nsAString& aDefaultExtension);
  NS_IMETHOD GetFilterIndex(int32_t *aFilterIndex);
  NS_IMETHOD SetFilterIndex(int32_t aFilterIndex);
  NS_IMETHOD GetFile(nsIFile * *aFile);
  NS_IMETHOD GetFileURL(nsIURI * *aFileURL);
  NS_IMETHOD GetFiles(nsISimpleEnumerator **aFiles);
  NS_IMETHOD Show(int16_t *_retval); 
  NS_IMETHOD AppendFilter(const nsAString& aTitle, const nsAString& aFilter);

protected:
  /* method from nsBaseFilePicker */
  virtual void InitNative(nsIWidget *aParent, const nsAString& aTitle);


  void GetFilterListArray(nsString& aFilterList);
  static void GetFileSystemCharset(nsCString & fileSystemCharset);
  char * ConvertToFileSystemCharset(const nsAString& inString);
  char16_t * ConvertFromFileSystemCharset(const char *inString);

  HWND                   mWnd;
  nsString               mTitle;
  nsCString              mFile;
  nsString               mDefault;
  nsString               mDefaultExtension;
  nsTArray<nsString>     mFilters;
  nsTArray<nsString>     mTitles;
  nsCOMPtr<nsIUnicodeEncoder> mUnicodeEncoder;
  nsCOMPtr<nsIUnicodeDecoder> mUnicodeDecoder;
  int16_t                mSelectedType;
  nsCOMArray<nsIFile>    mFiles;
  static char            mLastUsedDirectory[];
};

#endif // nsFilePicker_h__
