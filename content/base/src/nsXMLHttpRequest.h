/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsXMLHttpRequest_h__
#define nsXMLHttpRequest_h__

#include "nsIXMLHttpRequest.h"
#include "nsISupportsUtils.h"
#include "nsString.h"
#include "nsIURI.h"
#include "nsIHttpChannel.h"
#include "nsIDocument.h"
#include "nsIStreamListener.h"
#include "nsWeakReference.h"
#include "jsapi.h"
#include "nsIScriptContext.h"
#include "nsIChannelEventSink.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIInterfaceRequestor.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsIProgressEventSink.h"
#include "nsCOMArray.h"
#include "nsJSUtils.h"
#include "nsTArray.h"
#include "nsIJSNativeInitializer.h"
#include "nsIDOMLSProgressEvent.h"
#include "nsITimer.h"
#include "nsDOMProgressEvent.h"
#include "nsDOMEventTargetHelper.h"
#include "nsContentUtils.h"
#include "nsDOMFile.h"
#include "nsDOMBlobBuilder.h"
#include "nsIPrincipal.h"
#include "nsIScriptObjectPrincipal.h"

#include "mozilla/Assertions.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/dom/XMLHttpRequestBinding.h"
#include "mozilla/dom/XMLHttpRequestUploadBinding.h"

class nsILoadGroup;
class AsyncVerifyRedirectCallbackForwarder;
class nsIUnicodeDecoder;
class nsIDOMFormData;

#define IMPL_EVENT_HANDLER(_lowercase, _capitalized)                    \
  JSObject* GetOn##_lowercase(JSContext* /* unused */ )                 \
  {                                                                     \
    return GetListenerAsJSObject(mOn##_capitalized##Listener);          \
  }                                                                     \
  void SetOn##_lowercase(JSContext* aCx, JSObject* aCallback, ErrorResult& aRv) \
  {                                                                     \
    aRv = SetJSObjectListener(aCx, NS_LITERAL_STRING(#_lowercase),      \
                              mOn##_capitalized##Listener,              \
                              aCallback);                               \
  }

class nsXHREventTarget : public nsDOMEventTargetHelper,
                         public nsIXMLHttpRequestEventTarget
{
public:
  typedef mozilla::dom::XMLHttpRequestResponseType
          XMLHttpRequestResponseType;

  virtual ~nsXHREventTarget() {}
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsXHREventTarget,
                                           nsDOMEventTargetHelper)
  NS_DECL_NSIXMLHTTPREQUESTEVENTTARGET
  NS_FORWARD_NSIDOMEVENTTARGET(nsDOMEventTargetHelper::)

  IMPL_EVENT_HANDLER(loadstart, LoadStart)
  IMPL_EVENT_HANDLER(progress, Progress)
  IMPL_EVENT_HANDLER(abort, Abort)
  IMPL_EVENT_HANDLER(error, Error)
  IMPL_EVENT_HANDLER(load, Load)
  IMPL_EVENT_HANDLER(timeout, Timeout)
  IMPL_EVENT_HANDLER(loadend, Loadend)
  
  virtual void DisconnectFromOwner();
protected:
  static inline JSObject* GetListenerAsJSObject(nsDOMEventListenerWrapper* aWrapper)
  {
    if (!aWrapper) {
      return nullptr;
    }

    nsCOMPtr<nsIXPConnectJSObjectHolder> holder =
        do_QueryInterface(aWrapper->GetInner());
    JSObject* obj;
    return holder && NS_SUCCEEDED(holder->GetJSObject(&obj)) ? obj : nullptr;
  }
  inline nsresult SetJSObjectListener(JSContext* aCx,
                                      const nsAString& aType,
                                      nsRefPtr<nsDOMEventListenerWrapper>& aWrapper,
                                      JSObject* aCallback)
  {
    nsCOMPtr<nsIDOMEventListener> listener;
    if (aCallback) {
      nsresult rv =
        nsContentUtils::XPConnect()->WrapJS(aCx,
                                            aCallback,
                                            NS_GET_IID(nsIDOMEventListener),
                                            getter_AddRefs(listener));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    return RemoveAddEventListener(aType, aWrapper, listener);
  }

  nsRefPtr<nsDOMEventListenerWrapper> mOnLoadListener;
  nsRefPtr<nsDOMEventListenerWrapper> mOnErrorListener;
  nsRefPtr<nsDOMEventListenerWrapper> mOnAbortListener;
  nsRefPtr<nsDOMEventListenerWrapper> mOnLoadStartListener;
  nsRefPtr<nsDOMEventListenerWrapper> mOnProgressListener;
  nsRefPtr<nsDOMEventListenerWrapper> mOnLoadendListener;
  nsRefPtr<nsDOMEventListenerWrapper> mOnTimeoutListener;
};

class nsXMLHttpRequestUpload : public nsXHREventTarget,
                               public nsIXMLHttpRequestUpload
{
public:
  nsXMLHttpRequestUpload(nsDOMEventTargetHelper* aOwner)
  {
    BindToOwner(aOwner);
    SetIsDOMBinding();
  }                                         
  NS_DECL_ISUPPORTS_INHERITED
  NS_FORWARD_NSIXMLHTTPREQUESTEVENTTARGET(nsXHREventTarget::)
  NS_FORWARD_NSIDOMEVENTTARGET(nsXHREventTarget::)
  NS_DECL_NSIXMLHTTPREQUESTUPLOAD

  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope,
                               bool *triedToWrap)
  {
    return mozilla::dom::XMLHttpRequestUploadBinding::Wrap(cx, scope, this, triedToWrap);
  }
  nsISupports* GetParentObject()
  {
    return GetOwner();
  }

  bool HasListeners()
  {
    return mListenerManager && mListenerManager->HasListeners();
  }
};

class nsXMLHttpRequestXPCOMifier;

// Make sure that any non-DOM interfaces added here are also added to
// nsXMLHttpRequestXPCOMifier.
class nsXMLHttpRequest : public nsXHREventTarget,
                         public nsIXMLHttpRequest,
                         public nsIJSXMLHttpRequest,
                         public nsIStreamListener,
                         public nsIChannelEventSink,
                         public nsIProgressEventSink,
                         public nsIInterfaceRequestor,
                         public nsSupportsWeakReference,
                         public nsIJSNativeInitializer,
                         public nsITimerCallback
{
  friend class nsXHRParseEndListener;
  friend class nsXMLHttpRequestXPCOMifier;

public:
  nsXMLHttpRequest();
  virtual ~nsXMLHttpRequest();

  virtual JSObject* WrapObject(JSContext *cx, JSObject *scope,
                               bool *triedToWrap)
  {
    return mozilla::dom::XMLHttpRequestBinding::Wrap(cx, scope, this, triedToWrap);
  }
  nsISupports* GetParentObject()
  {
    return GetOwner();
  }

  // The WebIDL constructors.
  static already_AddRefed<nsXMLHttpRequest>
  Constructor(JSContext* aCx,
              nsISupports* aGlobal,
              const mozilla::dom::MozXMLHttpRequestParameters& aParams,
              ErrorResult& aRv)
  {
    nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(aGlobal);
    nsCOMPtr<nsIScriptObjectPrincipal> principal = do_QueryInterface(aGlobal);
    if (!window || ! principal) {
      aRv.Throw(NS_ERROR_FAILURE);
      return NULL;
    }

    nsRefPtr<nsXMLHttpRequest> req = new nsXMLHttpRequest();
    req->Construct(principal->GetPrincipal(), window);
    req->InitParameters(aParams.mozAnon, aParams.mozSystem);
    return req.forget();
  }

  static already_AddRefed<nsXMLHttpRequest>
  Constructor(JSContext* aCx,
              nsISupports* aGlobal,
              const nsAString& ignored,
              ErrorResult& aRv)
  {
    // Pretend like someone passed null, so we can pick up the default values
    mozilla::dom::MozXMLHttpRequestParameters params;
    if (!params.Init(aCx, JS::NullValue())) {
      aRv.Throw(NS_ERROR_UNEXPECTED);
      return nullptr;
    }

    return Constructor(aCx, aGlobal, params, aRv);
  }

  void Construct(nsIPrincipal* aPrincipal,
                 nsPIDOMWindow* aOwnerWindow,
                 nsIURI* aBaseURI = NULL)
  {
    MOZ_ASSERT(aPrincipal);
    MOZ_ASSERT_IF(aOwnerWindow, aOwnerWindow->IsInnerWindow());
    mPrincipal = aPrincipal;
    BindToOwner(aOwnerWindow);
    mBaseURI = aBaseURI;
  }

  // Initialize XMLHttpRequestParameter object.
  nsresult InitParameters(JSContext* aCx, const jsval* aParams);
  void InitParameters(bool aAnon, bool aSystem);

  NS_DECL_ISUPPORTS_INHERITED

  // nsIXMLHttpRequest
  NS_DECL_NSIXMLHTTPREQUEST

  NS_FORWARD_NSIXMLHTTPREQUESTEVENTTARGET(nsXHREventTarget::)

  // nsIStreamListener
  NS_DECL_NSISTREAMLISTENER

  // nsIRequestObserver
  NS_DECL_NSIREQUESTOBSERVER

  // nsIChannelEventSink
  NS_DECL_NSICHANNELEVENTSINK

  // nsIProgressEventSink
  NS_DECL_NSIPROGRESSEVENTSINK

  // nsIInterfaceRequestor
  NS_DECL_NSIINTERFACEREQUESTOR

  // nsITimerCallback
  NS_DECL_NSITIMERCALLBACK

  // nsIJSNativeInitializer
  NS_IMETHOD Initialize(nsISupports* aOwner, JSContext* cx, JSObject* obj,
                       uint32_t argc, jsval* argv);

  NS_FORWARD_NSIDOMEVENTTARGET(nsXHREventTarget::)

#ifdef DEBUG
  void StaticAssertions();
#endif

  // event handler
  IMPL_EVENT_HANDLER(readystatechange, Readystatechange)

  // states
  uint16_t GetReadyState();

  // request
  void Open(const nsAString& aMethod, const nsAString& aUrl, bool aAsync,
            const mozilla::dom::Optional<nsAString>& aUser,
            const mozilla::dom::Optional<nsAString>& aPassword,
            ErrorResult& aRv)
  {
    aRv = Open(NS_ConvertUTF16toUTF8(aMethod), NS_ConvertUTF16toUTF8(aUrl),
               aAsync, aUser, aPassword);
  }
  void SetRequestHeader(const nsAString& aHeader, const nsAString& aValue,
                        ErrorResult& aRv)
  {
    aRv = SetRequestHeader(NS_ConvertUTF16toUTF8(aHeader),
                           NS_ConvertUTF16toUTF8(aValue));
  }
  uint32_t GetTimeout()
  {
    return mTimeoutMilliseconds;
  }
  void SetTimeout(uint32_t aTimeout, ErrorResult& aRv);
  bool GetWithCredentials();
  void SetWithCredentials(bool aWithCredentials, nsresult& aRv);
  nsXMLHttpRequestUpload* GetUpload();

private:
  class RequestBody
  {
  public:
    RequestBody() : mType(Uninitialized)
    {
    }
    RequestBody(mozilla::dom::ArrayBuffer* aArrayBuffer) : mType(ArrayBuffer)
    {
      mValue.mArrayBuffer = aArrayBuffer;
    }
    RequestBody(nsIDOMBlob* aBlob) : mType(Blob)
    {
      mValue.mBlob = aBlob;
    }
    RequestBody(nsIDocument* aDocument) : mType(Document)
    {
      mValue.mDocument = aDocument;
    }
    RequestBody(const nsAString& aString) : mType(DOMString)
    {
      mValue.mString = &aString;
    }
    RequestBody(nsIDOMFormData* aFormData) : mType(FormData)
    {
      mValue.mFormData = aFormData;
    }
    RequestBody(nsIInputStream* aStream) : mType(InputStream)
    {
      mValue.mStream = aStream;
    }

    enum Type {
      Uninitialized,
      ArrayBuffer,
      Blob,
      Document,
      DOMString,
      FormData,
      InputStream
    };
    union Value {
      mozilla::dom::ArrayBuffer* mArrayBuffer;
      nsIDOMBlob* mBlob;
      nsIDocument* mDocument;
      const nsAString* mString;
      nsIDOMFormData* mFormData;
      nsIInputStream* mStream;
    };

    Type GetType() const
    {
      MOZ_ASSERT(mType != Uninitialized);
      return mType;
    }
    Value GetValue() const
    {
      MOZ_ASSERT(mType != Uninitialized);
      return mValue;
    }

  private:
    Type mType;
    Value mValue;
  };

  static nsresult GetRequestBody(nsIVariant* aVariant,
                                 const Nullable<RequestBody>& aBody,
                                 nsIInputStream** aResult,
                                 nsACString& aContentType,
                                 nsACString& aCharset);

  nsresult Send(nsIVariant* aVariant, const Nullable<RequestBody>& aBody);
  nsresult Send(const Nullable<RequestBody>& aBody)
  {
    return Send(nullptr, aBody);
  }
  nsresult Send(const RequestBody& aBody)
  {
    return Send(Nullable<RequestBody>(aBody));
  }

public:
  void Send(ErrorResult& aRv)
  {
    aRv = Send(Nullable<RequestBody>());
  }
  void Send(mozilla::dom::ArrayBuffer& aArrayBuffer, ErrorResult& aRv)
  {
    aRv = Send(RequestBody(&aArrayBuffer));
  }
  void Send(nsIDOMBlob* aBlob, ErrorResult& aRv)
  {
    NS_ASSERTION(aBlob, "Null should go to string version");
    aRv = Send(RequestBody(aBlob));
  }
  void Send(nsIDocument* aDoc, ErrorResult& aRv)
  {
    NS_ASSERTION(aDoc, "Null should go to string version");
    aRv = Send(RequestBody(aDoc));
  }
  void Send(const nsAString& aString, ErrorResult& aRv)
  {
    if (DOMStringIsNull(aString)) {
      Send(aRv);
    }
    else {
      aRv = Send(RequestBody(aString));
    }
  }
  void Send(nsIDOMFormData* aFormData, ErrorResult& aRv)
  {
    NS_ASSERTION(aFormData, "Null should go to string version");
    aRv = Send(RequestBody(aFormData));
  }
  void Send(nsIInputStream* aStream, ErrorResult& aRv)
  {
    NS_ASSERTION(aStream, "Null should go to string version");
    aRv = Send(RequestBody(aStream));
  }
  void SendAsBinary(const nsAString& aBody, ErrorResult& aRv);

  void Abort();

  // response
  uint32_t GetStatus();
  void GetStatusText(nsString& aStatusText);
  void GetResponseHeader(const nsACString& aHeader, nsACString& aResult,
                         ErrorResult& aRv);
  void GetResponseHeader(const nsAString& aHeader, nsString& aResult,
                         ErrorResult& aRv)
  {
    nsCString result;
    GetResponseHeader(NS_ConvertUTF16toUTF8(aHeader), result, aRv);
    if (result.IsVoid()) {
      aResult.SetIsVoid(true);
    }
    else {
      // We use UTF8ToNewUnicode here because it truncates after invalid UTF-8
      // characters, CopyUTF8toUTF16 just doesn't copy in that case.
      uint32_t length;
      PRUnichar* chars = UTF8ToNewUnicode(result, &length);
      aResult.Adopt(chars, length);
    }
  }
  void GetAllResponseHeaders(nsString& aResponseHeaders);
  void OverrideMimeType(const nsAString& aMimeType)
  {
    // XXX Should we do some validation here?
    mOverrideMimeType = aMimeType;
  }
  XMLHttpRequestResponseType GetResponseType()
  {
    return XMLHttpRequestResponseType(mResponseType);
  }
  void SetResponseType(XMLHttpRequestResponseType aType, ErrorResult& aRv);
  JS::Value GetResponse(JSContext* aCx, ErrorResult& aRv);
  void GetResponseText(nsString& aResponseText, ErrorResult& aRv);
  nsIDocument* GetResponseXML(ErrorResult& aRv);

  bool GetMozBackgroundRequest();
  void SetMozBackgroundRequest(bool aMozBackgroundRequest, nsresult& aRv);
  bool GetMultipart();
  void SetMultipart(bool aMultipart, nsresult& aRv);

  bool GetMozAnon();
  bool GetMozSystem();

  nsIChannel* GetChannel()
  {
    return mChannel;
  }

  // We need a GetInterface callable from JS for chrome JS
  JS::Value GetInterface(JSContext* aCx, nsIJSID* aIID, ErrorResult& aRv);

  // This creates a trusted readystatechange event, which is not cancelable and
  // doesn't bubble.
  static nsresult CreateReadystatechangeEvent(nsIDOMEvent** aDOMEvent);
  // For backwards compatibility aPosition should contain the headers for upload
  // and aTotalSize is LL_MAXUINT when unknown. Both those values are
  // used by nsXMLHttpProgressEvent. Normal progress event should not use
  // headers in aLoaded and aTotal is 0 when unknown.
  void DispatchProgressEvent(nsDOMEventTargetHelper* aTarget,
                             const nsAString& aType,
                             // Whether to use nsXMLHttpProgressEvent,
                             // which implements LS Progress Event.
                             bool aUseLSEventWrapper,
                             bool aLengthComputable,
                             // For Progress Events
                             uint64_t aLoaded, uint64_t aTotal,
                             // For LS Progress Events
                             uint64_t aPosition, uint64_t aTotalSize);
  void DispatchProgressEvent(nsDOMEventTargetHelper* aTarget,
                             const nsAString& aType,
                             bool aLengthComputable,
                             uint64_t aLoaded, uint64_t aTotal)
  {
    DispatchProgressEvent(aTarget, aType, false,
                          aLengthComputable, aLoaded, aTotal,
                          aLoaded, aLengthComputable ? aTotal : LL_MAXUINT);
  }

  // Dispatch the "progress" event on the XHR or XHR.upload object if we've
  // received data since the last "progress" event. Also dispatches
  // "uploadprogress" as needed.
  void MaybeDispatchProgressEvents(bool aFinalProgress);

  // This is called by the factory constructor.
  nsresult Init();

  void SetRequestObserver(nsIRequestObserver* aObserver);

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_SCRIPT_HOLDER_CLASS_INHERITED(nsXMLHttpRequest,
                                                                   nsXHREventTarget)
  bool AllowUploadProgress();
  void RootJSResultObjects();

  virtual void DisconnectFromOwner();
protected:
  friend class nsMultipartProxyListener;

  nsresult DetectCharset();
  nsresult AppendToResponseText(const char * aBuffer, uint32_t aBufferLen);
  static NS_METHOD StreamReaderFunc(nsIInputStream* in,
                void* closure,
                const char* fromRawSegment,
                uint32_t toOffset,
                uint32_t count,
                uint32_t *writeCount);
  nsresult CreateResponseParsedJSON(JSContext* aCx);
  nsresult CreatePartialBlob(void);
  bool CreateDOMFile(nsIRequest *request);
  // Change the state of the object with this. The broadcast argument
  // determines if the onreadystatechange listener should be called.
  nsresult ChangeState(uint32_t aState, bool aBroadcast = true);
  already_AddRefed<nsILoadGroup> GetLoadGroup() const;
  nsIURI *GetBaseURI();

  nsresult RemoveAddEventListener(const nsAString& aType,
                                  nsRefPtr<nsDOMEventListenerWrapper>& aCurrent,
                                  nsIDOMEventListener* aNew);

  nsresult GetInnerEventListener(nsRefPtr<nsDOMEventListenerWrapper>& aWrapper,
                                 nsIDOMEventListener** aListener);

  already_AddRefed<nsIHttpChannel> GetCurrentHttpChannel();

  bool IsSystemXHR();

  void ChangeStateToDone();

  /**
   * Check if aChannel is ok for a cross-site request by making sure no
   * inappropriate headers are set, and no username/password is set.
   *
   * Also updates the XML_HTTP_REQUEST_USE_XSITE_AC bit.
   */
  nsresult CheckChannelForCrossSiteRequest(nsIChannel* aChannel);

  void StartProgressEventTimer();

  friend class AsyncVerifyRedirectCallbackForwarder;
  void OnRedirectVerifyCallback(nsresult result);

  nsresult Open(const nsACString& method, const nsACString& url, bool async,
                const mozilla::dom::Optional<nsAString>& user,
                const mozilla::dom::Optional<nsAString>& password);

  already_AddRefed<nsXMLHttpRequestXPCOMifier> EnsureXPCOMifier();

  nsCOMPtr<nsISupports> mContext;
  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCOMPtr<nsIChannel> mChannel;
  // mReadRequest is different from mChannel for multipart requests
  nsCOMPtr<nsIRequest> mReadRequest;
  nsCOMPtr<nsIDocument> mResponseXML;
  nsCOMPtr<nsIChannel> mCORSPreflightChannel;
  nsTArray<nsCString> mCORSUnsafeHeaders;

  nsRefPtr<nsDOMEventListenerWrapper> mOnReadystatechangeListener;

  nsCOMPtr<nsIStreamListener> mXMLParserStreamListener;

  // used to implement getAllResponseHeaders()
  class nsHeaderVisitor : public nsIHttpHeaderVisitor {
  public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSIHTTPHEADERVISITOR
    nsHeaderVisitor(bool aIsSystemXHR) : mIsSystemXHR(aIsSystemXHR) { }
    virtual ~nsHeaderVisitor() {}
    const nsACString &Headers() { return mHeaders; }
  private:
    bool mIsSystemXHR;
    nsCString mHeaders;
  };

  // The bytes of our response body. Only used for DEFAULT, ARRAYBUFFER and
  // BLOB responseTypes
  nsCString mResponseBody;

  // The text version of our response body. This is incrementally decoded into
  // as we receive network data. However for the DEFAULT responseType we
  // lazily decode into this from mResponseBody only when .responseText is
  // accessed.
  // Only used for DEFAULT and TEXT responseTypes.
  nsString mResponseText;
  
  // For DEFAULT responseType we use this to keep track of how far we've
  // lazily decoded from mResponseBody to mResponseText
  uint32_t mResponseBodyDecodedPos;

  // Decoder used for decoding into mResponseText
  // Only used for DEFAULT, TEXT and JSON responseTypes.
  // In cases where we've only received half a surrogate, the decoder itself
  // carries the state to remember this. Next time we receive more data we
  // simply feed the new data into the decoder which will handle the second
  // part of the surrogate.
  nsCOMPtr<nsIUnicodeDecoder> mDecoder;

  nsCString mResponseCharset;

  enum ResponseType {
    XML_HTTP_RESPONSE_TYPE_DEFAULT,
    XML_HTTP_RESPONSE_TYPE_ARRAYBUFFER,
    XML_HTTP_RESPONSE_TYPE_BLOB,
    XML_HTTP_RESPONSE_TYPE_DOCUMENT,
    XML_HTTP_RESPONSE_TYPE_JSON,
    XML_HTTP_RESPONSE_TYPE_TEXT,
    XML_HTTP_RESPONSE_TYPE_CHUNKED_TEXT,
    XML_HTTP_RESPONSE_TYPE_CHUNKED_ARRAYBUFFER,
    XML_HTTP_RESPONSE_TYPE_MOZ_BLOB
  };

  void SetResponseType(nsXMLHttpRequest::ResponseType aType, ErrorResult& aRv);

  ResponseType mResponseType;

  // It is either a cached blob-response from the last call to GetResponse,
  // but is also explicitly set in OnStopRequest.
  nsCOMPtr<nsIDOMBlob> mResponseBlob;
  // Non-null only when we are able to get a os-file representation of the
  // response, i.e. when loading from a file, or when the http-stream
  // caches into a file or is reading from a cached file.
  nsRefPtr<nsDOMFile> mDOMFile;
  // We stream data to mBuilder when response type is "blob" or "moz-blob"
  // and mDOMFile is null.
  nsRefPtr<nsDOMBlobBuilder> mBuilder;

  nsString mOverrideMimeType;

  /**
   * The notification callbacks the channel had when Send() was
   * called.  We want to forward things here as needed.
   */
  nsCOMPtr<nsIInterfaceRequestor> mNotificationCallbacks;
  /**
   * Sink interfaces that we implement that mNotificationCallbacks may
   * want to also be notified for.  These are inited lazily if we're
   * asked for the relevant interface.
   */
  nsCOMPtr<nsIChannelEventSink> mChannelEventSink;
  nsCOMPtr<nsIProgressEventSink> mProgressEventSink;

  nsIRequestObserver* mRequestObserver;

  nsCOMPtr<nsIURI> mBaseURI;

  uint32_t mState;

  nsRefPtr<nsXMLHttpRequestUpload> mUpload;
  uint64_t mUploadTransferred;
  uint64_t mUploadTotal;
  bool mUploadLengthComputable;
  bool mUploadComplete;
  bool mProgressSinceLastProgressEvent;
  uint64_t mUploadProgress; // For legacy
  uint64_t mUploadProgressMax; // For legacy

  // Timeout support
  PRTime mRequestSentTime;
  uint32_t mTimeoutMilliseconds;
  nsCOMPtr<nsITimer> mTimeoutTimer;
  void StartTimeoutTimer();
  void HandleTimeoutCallback();

  bool mErrorLoad;
  bool mWaitingForOnStopRequest;
  bool mProgressTimerIsActive;
  bool mProgressEventWasDelayed;
  bool mIsHtml;
  bool mWarnAboutMultipartHtml;
  bool mWarnAboutSyncHtml;
  bool mLoadLengthComputable;
  uint64_t mLoadTotal; // 0 if not known.
  uint64_t mLoadTransferred;
  nsCOMPtr<nsITimer> mProgressNotifier;
  void HandleProgressTimerCallback();

  bool mIsSystem;
  bool mIsAnon;

  /**
   * Close the XMLHttpRequest's channels and dispatch appropriate progress
   * events.
   *
   * @param aType The progress event type.
   * @param aFlag A XML_HTTP_REQUEST_* state flag defined in
   *              nsXMLHttpRequest.cpp.
   */
  void CloseRequestWithError(const nsAString& aType, const uint32_t aFlag);

  bool mFirstStartRequestSeen;
  bool mInLoadProgressEvent;
  
  nsCOMPtr<nsIAsyncVerifyRedirectCallback> mRedirectCallback;
  nsCOMPtr<nsIChannel> mNewRedirectChannel;
  
  jsval mResultJSON;
  JSObject* mResultArrayBuffer;

  void ResetResponse();

  struct RequestHeader
  {
    nsCString header;
    nsCString value;
  };
  nsTArray<RequestHeader> mModifiedRequestHeaders;

  // Helper object to manage our XPCOM scriptability bits
  nsXMLHttpRequestXPCOMifier* mXPCOMifier;
};

#undef IMPL_EVENT_HANDLER

// A shim class designed to expose the non-DOM interfaces of
// XMLHttpRequest via XPCOM stuff.
class nsXMLHttpRequestXPCOMifier MOZ_FINAL : public nsIStreamListener,
                                             public nsIChannelEventSink,
                                             public nsIProgressEventSink,
                                             public nsIInterfaceRequestor,
                                             public nsITimerCallback,
                                             public nsCycleCollectionParticipant
{
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsXMLHttpRequestXPCOMifier,
                                           nsIStreamListener)

  nsXMLHttpRequestXPCOMifier(nsXMLHttpRequest* aXHR) :
    mXHR(aXHR)
  {
  }

  ~nsXMLHttpRequestXPCOMifier() {
    if (mXHR) {
      mXHR->mXPCOMifier = nullptr;
    }
  }

  NS_FORWARD_NSISTREAMLISTENER(mXHR->)
  NS_FORWARD_NSIREQUESTOBSERVER(mXHR->)
  NS_FORWARD_NSICHANNELEVENTSINK(mXHR->)
  NS_FORWARD_NSIPROGRESSEVENTSINK(mXHR->)
  NS_FORWARD_NSITIMERCALLBACK(mXHR->)

  NS_DECL_NSIINTERFACEREQUESTOR

private:
  nsRefPtr<nsXMLHttpRequest> mXHR;
};

// helper class to expose a progress DOM Event

class nsXMLHttpProgressEvent : public nsIDOMProgressEvent,
                               public nsIDOMLSProgressEvent
{
public:
  nsXMLHttpProgressEvent(nsIDOMProgressEvent* aInner,
                         uint64_t aCurrentProgress,
                         uint64_t aMaxProgress,
                         nsPIDOMWindow* aWindow);
  virtual ~nsXMLHttpProgressEvent();

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsXMLHttpProgressEvent, nsIDOMProgressEvent)
  NS_FORWARD_NSIDOMEVENT(mInner->)
  NS_FORWARD_NSIDOMPROGRESSEVENT(mInner->)
  NS_DECL_NSIDOMLSPROGRESSEVENT

protected:
  void WarnAboutLSProgressEvent(nsIDocument::DeprecatedOperations);

  // Use nsDOMProgressEvent so that we can forward
  // most of the method calls easily.
  nsRefPtr<nsDOMProgressEvent> mInner;
  nsCOMPtr<nsPIDOMWindow> mWindow;
  uint64_t mCurProgress;
  uint64_t mMaxProgress;
};

class nsXHRParseEndListener : public nsIDOMEventListener
{
public:
  NS_DECL_ISUPPORTS
  NS_IMETHOD HandleEvent(nsIDOMEvent *event)
  {
    nsCOMPtr<nsIXMLHttpRequest> xhr = do_QueryReferent(mXHR);
    if (xhr) {
      static_cast<nsXMLHttpRequest*>(xhr.get())->ChangeStateToDone();
    }
    mXHR = nullptr;
    return NS_OK;
  }
  nsXHRParseEndListener(nsIXMLHttpRequest* aXHR)
    : mXHR(do_GetWeakReference(aXHR)) {}
  virtual ~nsXHRParseEndListener() {}
private:
  nsWeakPtr mXHR;
};

#endif
