/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_encoders_icon_os2_nsIconChannel_h
#define mozilla_image_encoders_icon_os2_nsIconChannel_h

#include "mozilla/Attributes.h"

#include "nsCOMPtr.h"
#include "nsXPIDLString.h"
#include "nsIChannel.h"
#include "nsILoadGroup.h"
#include "nsILoadInfo.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIURI.h"
#include "nsIInputStreamPump.h"
#include "nsIStreamListener.h"

class nsIFile;

class nsIconChannel final : public nsIChannel, public nsIStreamListener
{
  ~nsIconChannel();

public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIREQUEST
  NS_DECL_NSICHANNEL
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER

  nsIconChannel();

  nsresult Init(nsIURI* uri);

protected:
  nsCOMPtr<nsIURI> mUrl;
  nsCOMPtr<nsIURI> mOriginalURI;
  nsCOMPtr<nsILoadGroup> mLoadGroup;
  nsCOMPtr<nsIInterfaceRequestor> mCallbacks;
  nsCOMPtr<nsISupports>  mOwner;
  nsCOMPtr<nsILoadInfo>  mLoadInfo;

  nsCOMPtr<nsIInputStreamPump> mPump;
  nsCOMPtr<nsIStreamListener>  mListener;

  nsresult ExtractIconInfoFromUrl(nsIFile** aLocalFile,
                                  uint32_t* aDesiredImageSize,
                                  nsACString& aContentType,
                                  nsACString& aFileExtension);
  nsresult MakeInputStream(nsIInputStream** _retval, bool nonBlocking);
};

#define ID_ICO_FILE    5800
#define ID_ICO_FOLDER  5801

#endif /* mozilla_image_encoders_icon_os2_nsIconChannel_h */
