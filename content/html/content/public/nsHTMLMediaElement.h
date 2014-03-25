/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#if !defined(nsHTMLMediaElement_h__)
#define nsHTMLMediaElement_h__

#include "nsIDOMHTMLMediaElement.h"
#include "nsGenericHTMLElement.h"
#include "nsMediaDecoder.h"
#include "nsIChannel.h"
#include "nsIHttpChannel.h"
#include "nsThreadUtils.h"
#include "nsIDOMRange.h"
#include "nsCycleCollectionParticipant.h"
#include "nsILoadGroup.h"
#include "nsIObserver.h"
#include "nsDataHashtable.h"
#include "nsAudioStream.h"
#include "VideoFrameContainer.h"
#include "mozilla/CORSMode.h"
#include "nsDOMMediaStream.h"
#include "mozilla/Mutex.h"
#include "nsTimeRanges.h"

// Define to output information on decoding and painting framerate
/* #define DEBUG_FRAME_RATE 1 */

typedef uint16_t nsMediaNetworkState;
typedef uint16_t nsMediaReadyState;

namespace mozilla {
class MediaResource;
}

class nsRange;

class nsHTMLMediaElement : public nsGenericHTMLElement,
                           public nsIObserver
{
public:
  typedef mozilla::TimeStamp TimeStamp;
  typedef mozilla::layers::ImageContainer ImageContainer;
  typedef mozilla::VideoFrameContainer VideoFrameContainer;
  typedef mozilla::MediaStream MediaStream;
  typedef mozilla::MediaResource MediaResource;

  typedef nsDataHashtable<nsCStringHashKey, nsCString> MetadataTags;

  enum CanPlayStatus {
    CANPLAY_NO,
    CANPLAY_MAYBE,
    CANPLAY_YES
  };

  mozilla::CORSMode GetCORSMode() {
    return mCORSMode;
  }

  nsHTMLMediaElement(already_AddRefed<nsINodeInfo> aNodeInfo);
  virtual ~nsHTMLMediaElement();

  /**
   * This is used when the browser is constructing a video element to play
   * a channel that we've already started loading. The src attribute and
   * <source> children are ignored.
   * @param aChannel the channel to use
   * @param aListener returns a stream listener that should receive
   * notifications for the stream
   */
  nsresult LoadWithChannel(nsIChannel *aChannel, nsIStreamListener **aListener);

  // nsIDOMHTMLMediaElement
  NS_DECL_NSIDOMHTMLMEDIAELEMENT

  NS_DECL_NSIOBSERVER

  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsHTMLMediaElement,
                                           nsGenericHTMLElement)

  virtual bool ParseAttribute(int32_t aNamespaceID,
                                nsIAtom* aAttribute,
                                const nsAString& aValue,
                                nsAttrValue& aResult);
  // SetAttr override.  C++ is stupid, so have to override both
  // overloaded methods.
  nsresult SetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                   const nsAString& aValue, bool aNotify)
  {
    return SetAttr(aNameSpaceID, aName, nullptr, aValue, aNotify);
  }
  virtual nsresult SetAttr(int32_t aNameSpaceID, nsIAtom* aName,
                           nsIAtom* aPrefix, const nsAString& aValue,
                           bool aNotify);
  virtual nsresult UnsetAttr(int32_t aNameSpaceID, nsIAtom* aAttr,
                             bool aNotify);

  virtual nsresult BindToTree(nsIDocument* aDocument, nsIContent* aParent,
                              nsIContent* aBindingParent,
                              bool aCompileEventHandlers);
  virtual void UnbindFromTree(bool aDeep = true,
                              bool aNullParent = true);
  virtual void DoneCreatingElement();

  /**
   * Call this to reevaluate whether we should start/stop due to our owner
   * document being active or inactive.
   */
  void NotifyOwnerDocumentActivityChanged();

  // Called by the video decoder object, on the main thread,
  // when it has read the metadata containing video dimensions,
  // etc.
  void MetadataLoaded(uint32_t aChannels,
                      uint32_t aRate,
                      bool aHasAudio,
                      const MetadataTags* aTags);

  // Called by the video decoder object, on the main thread,
  // when it has read the first frame of the video
  // aResourceFullyLoaded should be true if the resource has been
  // fully loaded and the caller will call ResourceLoaded next.
  void FirstFrameLoaded(bool aResourceFullyLoaded);

  // Called by the video decoder object, on the main thread,
  // when the resource has completed downloading.
  void ResourceLoaded();

  // Called by the video decoder object, on the main thread,
  // when the resource has a network error during loading.
  void NetworkError();

  // Called by the video decoder object, on the main thread, when the
  // resource has a decode error during metadata loading or decoding.
  void DecodeError();

  // Called by the video decoder object, on the main thread, when the
  // resource load has been cancelled.
  void LoadAborted();

  // Called by the video decoder object, on the main thread,
  // when the video playback has ended.
  void PlaybackEnded();

  // Called by the video decoder object, on the main thread,
  // when the resource has started seeking.
  void SeekStarted();

  // Called by the video decoder object, on the main thread,
  // when the resource has completed seeking.
  void SeekCompleted();

  // Called by the media stream, on the main thread, when the download
  // has been suspended by the cache or because the element itself
  // asked the decoder to suspend the download.
  void DownloadSuspended();

  // Called by the media stream, on the main thread, when the download
  // has been resumed by the cache or because the element itself
  // asked the decoder to resumed the download.
  // If aForceNetworkLoading is True, ignore the fact that the download has
  // previously finished. We are downloading the middle of the media after
  // having downloaded the end, we need to notify the element a download in
  // ongoing.
  void DownloadResumed(bool aForceNetworkLoading = false);

  // Called by the media decoder to indicate that the download has stalled
  // (no data has arrived for a while).
  void DownloadStalled();

  // Called by the media decoder to indicate whether the media cache has
  // suspended the channel.
  void NotifySuspendedByCache(bool aIsSuspended);

  // Called when a "MozAudioAvailable" event listener is added. The media
  // element will then notify its decoder that it needs to make a copy of
  // the audio data sent to hardware and dispatch it in "mozaudioavailable"
  // events. This allows us to not perform the copy and thus reduce overhead
  // in the common case where we don't have a "MozAudioAvailable" listener.
  void NotifyAudioAvailableListener();

  // Called by the media decoder and the video frame to get the
  // ImageContainer containing the video data.
  VideoFrameContainer* GetVideoFrameContainer();
  ImageContainer* GetImageContainer()
  {
    VideoFrameContainer* container = GetVideoFrameContainer();
    return container ? container->GetImageContainer() : nullptr;
  }

  // Called by the video frame to get the print surface, if this is
  // a static document and we're not actually playing video
  gfxASurface* GetPrintSurface() { return mPrintSurface; }

  // Dispatch events
  using nsGenericHTMLElement::DispatchEvent;
  nsresult DispatchEvent(const nsAString& aName);
  nsresult DispatchAsyncEvent(const nsAString& aName);
  nsresult DispatchAudioAvailableEvent(float* aFrameBuffer,
                                       uint32_t aFrameBufferLength,
                                       float aTime);

  // Dispatch events that were raised while in the bfcache
  nsresult DispatchPendingMediaEvents();

  // Called by the decoder when some data has been downloaded or
  // buffering/seeking has ended. aNextFrameAvailable is true when
  // the data for the next frame is available. This method will
  // decide whether to set the ready state to HAVE_CURRENT_DATA,
  // HAVE_FUTURE_DATA or HAVE_ENOUGH_DATA.
  enum NextFrameStatus {
    // The next frame of audio/video is available
    NEXT_FRAME_AVAILABLE,
    // The next frame of audio/video is unavailable because the decoder
    // is paused while it buffers up data
    NEXT_FRAME_UNAVAILABLE_BUFFERING,
    // The next frame of audio/video is unavailable for some other reasons
    NEXT_FRAME_UNAVAILABLE
  };
  void UpdateReadyStateForData(NextFrameStatus aNextFrame);

  // Use this method to change the mReadyState member, so required
  // events can be fired.
  void ChangeReadyState(nsMediaReadyState aState);

  // Return true if we can activate autoplay assuming enough data has arrived.
  bool CanActivateAutoplay();

  // Notify that enough data has arrived to start autoplaying.
  // If the element is 'autoplay' and is ready to play back (not paused,
  // autoplay pref enabled, etc), it should start playing back.
  void NotifyAutoplayDataReady();

  // Check if the media element had crossorigin set when loading started
  bool ShouldCheckAllowOrigin();

  // Is the media element potentially playing as defined by the HTML 5 specification.
  // http://www.whatwg.org/specs/web-apps/current-work/#potentially-playing
  bool IsPotentiallyPlaying() const;

  // Has playback ended as defined by the HTML 5 specification.
  // http://www.whatwg.org/specs/web-apps/current-work/#ended
  bool IsPlaybackEnded() const;

  // principal of the currently playing resource. Anything accessing the contents
  // of this element must have a principal that subsumes this principal.
  // Returns null if nothing is playing.
  already_AddRefed<nsIPrincipal> GetCurrentPrincipal();

  // called to notify that the principal of the decoder's media resource has changed.
  void NotifyDecoderPrincipalChanged();

  // Update the visual size of the media. Called from the decoder on the
  // main thread when/if the size changes.
  void UpdateMediaSize(nsIntSize size);

  // Returns the CanPlayStatus indicating if we can handle this
  // MIME type. The MIME type should not include the codecs parameter.
  // If it returns anything other than CANPLAY_NO then it also
  // returns a null-terminated list of supported codecs
  // in *aSupportedCodecs. This list should not be freed, it is static data.
  static CanPlayStatus CanHandleMediaType(const char* aMIMEType,
                                          char const *const ** aSupportedCodecs);

  // Returns the CanPlayStatus indicating if we can handle the
  // full MIME type including the optional codecs parameter.
  static CanPlayStatus GetCanPlay(const nsAString& aType);

  // Returns true if we should handle this MIME type when it appears
  // as an <object> or as a toplevel page. If, in practice, our support
  // for the type is more limited than appears in the wild, we should return
  // false here even if CanHandleMediaType would return true.
  static bool ShouldHandleMediaType(const char* aMIMEType);

#ifdef MOZ_RAW
  static bool IsRawEnabled();
#endif

#ifdef MOZ_OGG
  static bool IsOggEnabled();
  static bool IsOggType(const nsACString& aType);
  static const char gOggTypes[3][16];
  static char const *const gOggCodecs[3];
  static bool IsOpusEnabled();
  static char const *const gOggCodecsWithOpus[4];
#endif

#ifdef MOZ_WAVE
  static bool IsWaveEnabled();
  static bool IsWaveType(const nsACString& aType);
  static const char gWaveTypes[4][15];
  static char const *const gWaveCodecs[2];
#endif

#ifdef MOZ_WEBM
  static bool IsWebMEnabled();
  static bool IsWebMType(const nsACString& aType);
  static const char gWebMTypes[2][11];
  static char const *const gWebMCodecs[4];
#endif

#ifdef MOZ_GSTREAMER
  static bool IsH264Enabled();
  static bool IsH264Type(const nsACString& aType);
  static const char gH264Types[3][16];
  static char const *const gH264Codecs[7];
#endif

#ifdef MOZ_MEDIA_PLUGINS
  static bool IsMediaPluginsEnabled();
  static bool IsMediaPluginsType(const nsACString& aType);
#endif

  /**
   * Called when a child source element is added to this media element. This
   * may queue a task to run the select resource algorithm if appropriate.
   */
  void NotifyAddedSource();

  /**
   * Called when there's been an error fetching the resource. This decides
   * whether it's appropriate to fire an error event.
   */
  void NotifyLoadError();

  /**
   * Called when data has been written to the underlying audio stream.
   */
  void NotifyAudioAvailable(float* aFrameBuffer, uint32_t aFrameBufferLength,
                            float aTime);

  virtual bool IsNodeOfType(uint32_t aFlags) const;

  /**
   * Returns the current load ID. Asynchronous events store the ID that was
   * current when they were enqueued, and if it has changed when they come to
   * fire, they consider themselves cancelled, and don't fire.
   */
  uint32_t GetCurrentLoadID() { return mCurrentLoadID; }

  /**
   * Returns the load group for this media element's owner document.
   * XXX XBL2 issue.
   */
  already_AddRefed<nsILoadGroup> GetDocumentLoadGroup();

  /**
   * Returns true if the media has played or completed a seek.
   * Used by video frame to determine whether to paint the poster.
   */
  bool GetPlayedOrSeeked() const { return mHasPlayedOrSeeked; }

  nsresult CopyInnerTo(nsGenericElement* aDest);

  /**
   * Sets the Accept header on the HTTP channel to the required
   * video or audio MIME types.
   */
  virtual nsresult SetAcceptHeader(nsIHttpChannel* aChannel) = 0;

  /**
   * Sets the required request headers on the HTTP channel for
   * video or audio requests.
   */
  void SetRequestHeaders(nsIHttpChannel* aChannel);

  /**
   * Fires a timeupdate event. If aPeriodic is true, the event will only
   * be fired if we've not fired a timeupdate event (for any reason) in the
   * last 250ms, as required by the spec when the current time is periodically
   * increasing during playback.
   */
  void FireTimeUpdate(bool aPeriodic);

  MediaStream* GetSrcMediaStream()
  {
    NS_ASSERTION(mSrcStream, "Don't call this when not playing a stream");
    return mSrcStream->GetStream();
  }

protected:
  class MediaLoadListener;
  class StreamListener;

  /**
   * Logs a warning message to the web console to report various failures.
   * aMsg is the localized message identifier, aParams is the parameters to
   * be substituted into the localized message, and aParamCount is the number
   * of parameters in aParams.
   */
  void ReportLoadError(const char* aMsg,
                       const PRUnichar** aParams = nullptr,
                       uint32_t aParamCount = 0);

  /**
   * Changes mHasPlayedOrSeeked to aValue. If mHasPlayedOrSeeked changes
   * we'll force a reflow so that the video frame gets reflowed to reflect
   * the poster hiding or showing immediately.
   */
  void SetPlayedOrSeeked(bool aValue);

  /**
   * Initialize the media element for playback of mSrcAttrStream
   */
  void SetupSrcMediaStreamPlayback();
  /**
   * Stop playback on mSrcStream.
   */
  void EndSrcMediaStreamPlayback();

  /**
   * Returns an nsDOMMediaStream containing the played contents of this
   * element. When aFinishWhenEnded is true, when this element ends playback
   * we will finish the stream and not play any more into it.
   * When aFinishWhenEnded is false, ending playback does not finish the stream.
   * The stream will never finish.
   */
  already_AddRefed<nsDOMMediaStream> CaptureStreamInternal(bool aFinishWhenEnded);

  /**
   * Create a decoder for the given aMIMEType. Returns null if we
   * were unable to create the decoder.
   */
  already_AddRefed<nsMediaDecoder> CreateDecoder(const nsACString& aMIMEType);

  /**
   * Initialize a decoder as a clone of an existing decoder in another
   * element.
   * mLoadingSrc must already be set.
   */
  nsresult InitializeDecoderAsClone(nsMediaDecoder* aOriginal);

  /**
   * Initialize a decoder to load the given channel. The decoder's stream
   * listener is returned via aListener.
   * mLoadingSrc must already be set.
   */
  nsresult InitializeDecoderForChannel(nsIChannel *aChannel,
                                       nsIStreamListener **aListener);

  /**
   * Finish setting up the decoder after Load() has been called on it.
   * Called by InitializeDecoderForChannel/InitializeDecoderAsClone.
   */
  nsresult FinishDecoderSetup(nsMediaDecoder* aDecoder,
                              MediaResource* aStream,
                              nsIStreamListener **aListener,
                              nsMediaDecoder* aCloneDonor);

  /**
   * Call this after setting up mLoadingSrc and mDecoder.
   */
  void AddMediaElementToURITable();
  /**
   * Call this before clearing mLoadingSrc.
   */
  void RemoveMediaElementFromURITable();
  /**
   * Call this to find a media element with the same NodePrincipal and mLoadingSrc
   * set to aURI, and with a decoder on which Load() has been called.
   */
  nsHTMLMediaElement* LookupMediaElementURITable(nsIURI* aURI);

  /**
   * Shutdown and clear mDecoder and maintain associated invariants.
   */
  void ShutdownDecoder();
  /**
   * Execute the initial steps of the load algorithm that ensure existing
   * loads are aborted, the element is emptied, and a new load ID is
   * created.
   */
  void AbortExistingLoads();

  /**
   * Create a URI for the given aURISpec string.
   */
  nsresult NewURIFromString(const nsAutoString& aURISpec, nsIURI** aURI);

  /**
   * Called when all potential resources are exhausted. Changes network
   * state to NETWORK_NO_SOURCE, and sends error event with code
   * MEDIA_ERR_SRC_NOT_SUPPORTED.
   */
  void NoSupportedMediaSourceError();

  /**
   * Attempts to load resources from the <source> children. This is a
   * substep of the resource selection algorithm. Do not call this directly,
   * call QueueLoadFromSourceTask() instead.
   */
  void LoadFromSourceChildren();

  /**
   * Asynchronously awaits a stable state, and then causes
   * LoadFromSourceChildren() to be called on the main threads' event loop.
   */
  void QueueLoadFromSourceTask();

  /**
   * Runs the media resource selection algorithm.
   */
  void SelectResource();

  /**
   * A wrapper function that allows us to cleanly reset flags after a call
   * to SelectResource()
   */
  void SelectResourceWrapper();

  /**
   * Asynchronously awaits a stable state, and then causes SelectResource()
   * to be run on the main thread's event loop.
   */
  void QueueSelectResourceTask();

  /**
   * The resource-fetch algorithm step of the load algorithm.
   */
  nsresult LoadResource();

  /**
   * Selects the next <source> child from which to load a resource. Called
   * during the resource selection algorithm. Stores the return value in
   * mSourceLoadCandidate before returning.
   */
  nsIContent* GetNextSource();

  /**
   * Changes mDelayingLoadEvent, and will call BlockOnLoad()/UnblockOnLoad()
   * on the owning document, so it can delay the load event firing.
   */
  void ChangeDelayLoadStatus(bool aDelay);

  /**
   * If we suspended downloading after the first frame, unsuspend now.
   */
  void StopSuspendingAfterFirstFrame();

  /**
   * Called when our channel is redirected to another channel.
   * Updates our mChannel reference to aNewChannel.
   */
  nsresult OnChannelRedirect(nsIChannel *aChannel,
                             nsIChannel *aNewChannel,
                             uint32_t aFlags);

  /**
   * Call this to reevaluate whether we should be holding a self-reference.
   */
  void AddRemoveSelfReference();

  /**
   * Called asynchronously to release a self-reference to this element.
   */
  void DoRemoveSelfReference();
  
  /**
   * Possible values of the 'preload' attribute.
   */
  enum PreloadAttrValue {
    PRELOAD_ATTR_EMPTY,    // set to ""
    PRELOAD_ATTR_NONE,     // set to "none"
    PRELOAD_ATTR_METADATA, // set to "metadata"
    PRELOAD_ATTR_AUTO      // set to "auto"
  };

  /**
   * The preloading action to perform. These dictate how we react to the 
   * preload attribute. See mPreloadAction.
   */
  enum PreloadAction {
    PRELOAD_UNDEFINED = 0, // not determined - used only for initialization
    PRELOAD_NONE = 1,      // do not preload
    PRELOAD_METADATA = 2,  // preload only the metadata (and first frame)
    PRELOAD_ENOUGH = 3     // preload enough data to allow uninterrupted
                           // playback
  };

  /**
   * Suspends the load of mLoadingSrc, so that it can be resumed later
   * by ResumeLoad(). This is called when we have a media with a 'preload'
   * attribute value of 'none', during the resource selection algorithm.
   */
  void SuspendLoad();

  /**
   * Resumes a previously suspended load (suspended by SuspendLoad(uri)).
   * Will continue running the resource selection algorithm.
   * Sets mPreloadAction to aAction.
   */
  void ResumeLoad(PreloadAction aAction);

  /**
   * Handle a change to the preload attribute. Should be called whenever the
   * value (or presence) of the preload attribute changes. The change in 
   * attribute value may cause a change in the mPreloadAction of this
   * element. If there is a change then this method will initiate any
   * behaviour that is necessary to implement the action.
   */
  void UpdatePreloadAction();

  /**
   * Dispatches an error event to a child source element.
   */
  void DispatchAsyncSourceError(nsIContent* aSourceElement);

  /**
   * Resets the media element for an error condition as per aErrorCode.
   * aErrorCode must be one of nsIDOMHTMLMediaError codes.
   */
  void Error(uint16_t aErrorCode);

  /**
   * Returns the URL spec of the currentSrc.
   **/
  void GetCurrentSpec(nsCString& aString);

  /**
   * Process any media fragment entries in the URI
   */
  void ProcessMediaFragmentURI();

  // The current decoder. Load() has been called on this decoder.
  // At most one of mDecoder and mSrcStream can be non-null.
  nsRefPtr<nsMediaDecoder> mDecoder;

  // A reference to the VideoFrameContainer which contains the current frame
  // of video to display.
  nsRefPtr<VideoFrameContainer> mVideoFrameContainer;

  // Holds a reference to the DOM wrapper for the MediaStream that has been
  // set in the src attribute.
  nsRefPtr<nsDOMMediaStream> mSrcAttrStream;

  // Holds a reference to the DOM wrapper for the MediaStream that we're
  // actually playing.
  // At most one of mDecoder and mSrcStream can be non-null.
  nsRefPtr<nsDOMMediaStream> mSrcStream;

  // Holds references to the DOM wrappers for the MediaStreams that we're
  // writing to.
  struct OutputMediaStream {
    nsRefPtr<nsDOMMediaStream> mStream;
    bool mFinishWhenEnded;
  };
  nsTArray<OutputMediaStream> mOutputStreams;

  // Holds a reference to the MediaStreamListener attached to mSrcStream.
  nsRefPtr<StreamListener> mSrcStreamListener;

  // Holds a reference to the first channel we open to the media resource.
  // Once the decoder is created, control over the channel passes to the
  // decoder, and we null out this reference. We must store this in case
  // we need to cancel the channel before control of it passes to the decoder.
  nsCOMPtr<nsIChannel> mChannel;

  // Error attribute
  nsCOMPtr<nsIDOMMediaError> mError;

  // The current media load ID. This is incremented every time we start a
  // new load. Async events note the ID when they're first sent, and only fire
  // if the ID is unchanged when they come to fire.
  uint32_t mCurrentLoadID;

  // Points to the child source elements, used to iterate through the children
  // when selecting a resource to load.
  nsRefPtr<nsRange> mSourcePointer;

  // Points to the document whose load we're blocking. This is the document
  // we're bound to when loading starts.
  nsCOMPtr<nsIDocument> mLoadBlockedDoc;

  // Contains names of events that have been raised while in the bfcache.
  // These events get re-dispatched when the bfcache is exited.
  nsTArray<nsString> mPendingEvents;

  // Media loading flags. See:
  //   http://www.whatwg.org/specs/web-apps/current-work/#video)
  nsMediaNetworkState mNetworkState;
  nsMediaReadyState mReadyState;

  enum LoadAlgorithmState {
    // No load algorithm instance is waiting for a source to be added to the
    // media in order to continue loading.
    NOT_WAITING,
    // We've run the load algorithm, and we tried all source children of the 
    // media element, and failed to load any successfully. We're waiting for
    // another source element to be added to the media element, and will try
    // to load any such element when its added.
    WAITING_FOR_SOURCE
  };

  // Denotes the waiting state of a load algorithm instance. When the load
  // algorithm is waiting for a source element child to be added, this is set
  // to WAITING_FOR_SOURCE, otherwise it's NOT_WAITING.
  LoadAlgorithmState mLoadWaitStatus;

  // Current audio volume
  double mVolume;

  // Current number of audio channels.
  uint32_t mChannels;

  // Current audio sample rate.
  uint32_t mRate;

  // Helper function to iterate over a hash table
  // and convert it to a JSObject.
  static PLDHashOperator BuildObjectFromTags(nsCStringHashKey::KeyType aKey,
                                             nsCString aValue,
                                             void* aUserArg);
  nsAutoPtr<const MetadataTags> mTags;

  // URI of the resource we're attempting to load. This stores the value we
  // return in the currentSrc attribute. Use GetCurrentSrc() to access the
  // currentSrc attribute.
  // This is always the original URL we're trying to load --- before
  // redirects etc.
  nsCOMPtr<nsIURI> mLoadingSrc;
  
  // Stores the current preload action for this element. Initially set to
  // PRELOAD_UNDEFINED, its value is changed by calling
  // UpdatePreloadAction().
  PreloadAction mPreloadAction;

  // Size of the media. Updated by the decoder on the main thread if
  // it changes. Defaults to a width and height of -1 if not set.
  // We keep this separate from the intrinsic size stored in the
  // VideoFrameContainer so that it doesn't change unexpectedly under us
  // due to decoder activity.
  nsIntSize mMediaSize;

  // Time that the last timeupdate event was fired. Read/Write from the
  // main thread only.
  TimeStamp mTimeUpdateTime;

  // Media 'currentTime' value when the last timeupdate event occurred.
  // Read/Write from the main thread only.
  double mLastCurrentTime;

  // Logical start time of the media resource in seconds as obtained
  // from any media fragments. A negative value indicates that no
  // fragment time has been set. Read/Write from the main thread only.
  double mFragmentStart;

  // Logical end time of the media resource in seconds as obtained
  // from any media fragments. A negative value indicates that no
  // fragment time has been set. Read/Write from the main thread only.
  double mFragmentEnd;

  nsRefPtr<gfxASurface> mPrintSurface;

  // Reference to the source element last returned by GetNextSource().
  // This is the child source element which we're trying to load from.
  nsCOMPtr<nsIContent> mSourceLoadCandidate;

  // An audio stream for writing audio directly from JS.
  nsRefPtr<nsAudioStream> mAudioStream;

  // Range of time played.
  nsTimeRanges mPlayed;

  // Stores the time at the start of the current 'played' range.
  double mCurrentPlayRangeStart;

  // True if MozAudioAvailable events can be safely dispatched, based on
  // a media and element same-origin check.
  bool mAllowAudioData;

  // If true then we have begun downloading the media content.
  // Set to false when completed, or not yet started.
  bool mBegun;

  // True when the decoder has loaded enough data to display the
  // first frame of the content.
  bool mLoadedFirstFrame;

  // Indicates whether current playback is a result of user action
  // (ie. calling of the Play method), or automatic playback due to
  // the 'autoplay' attribute being set. A true value indicates the
  // latter case.
  // The 'autoplay' HTML attribute indicates that the video should
  // start playing when loaded. The 'autoplay' attribute of the object
  // is a mirror of the HTML attribute. These are different from this
  // 'mAutoplaying' flag, which indicates whether the current playback
  // is a result of the autoplay attribute.
  bool mAutoplaying;

  // Indicates whether |autoplay| will actually autoplay based on the pref
  // media.autoplay.enabled
  bool mAutoplayEnabled;

  // Playback of the video is paused either due to calling the
  // 'Pause' method, or playback not yet having started.
  bool mPaused;

  // True if the sound is muted.
  bool mMuted;

  // True if the sound is being captured.
  bool mAudioCaptured;

  // If TRUE then the media element was actively playing before the currently
  // in progress seeking. If FALSE then the media element is either not seeking
  // or was not actively playing before the current seek. Used to decide whether
  // to raise the 'waiting' event as per 4.7.1.8 in HTML 5 specification.
  bool mPlayingBeforeSeek;

  // True iff this element is paused because the document is inactive
  bool mPausedForInactiveDocument;

  // True if we've reported a "waiting" event since the last
  // readyState change to HAVE_CURRENT_DATA.
  bool mWaitingFired;

  // True if we're running the "load()" method.
  bool mIsRunningLoadMethod;

  // True if we're loading the resource from the child source elements.
  bool mIsLoadingFromSourceChildren;

  // True if we're delaying the "load" event. They are delayed until either
  // an error occurs, or the first frame is loaded.
  bool mDelayingLoadEvent;

  // True when we've got a task queued to call SelectResource(),
  // or while we're running SelectResource().
  bool mIsRunningSelectResource;

  // True when we already have select resource call queued
  bool mHaveQueuedSelectResource;

  // True if we suspended the decoder because we were paused,
  // preloading metadata is enabled, autoplay was not enabled, and we loaded
  // the first frame.
  bool mSuspendedAfterFirstFrame;

  // True if we are allowed to suspend the decoder because we were paused,
  // preloading metdata was enabled, autoplay was not enabled, and we loaded
  // the first frame.
  bool mAllowSuspendAfterFirstFrame;

  // True if we've played or completed a seek. We use this to determine
  // when the poster frame should be shown.
  bool mHasPlayedOrSeeked;

  // True if we've added a reference to ourselves to keep the element
  // alive while no-one is referencing it but the element may still fire
  // events of its own accord.
  bool mHasSelfReference;

  // True if we've received a notification that the engine is shutting
  // down.
  bool mShuttingDown;

  // True if we've suspended a load in the resource selection algorithm
  // due to loading a preload:none media. When true, the resource we'll
  // load when the user initiates either playback or an explicit load is
  // stored in mPreloadURI.
  bool mSuspendedForPreloadNone;

  // True if a same-origin check has been done for the media element and resource.
  bool mMediaSecurityVerified;

  // The CORS mode when loading the media element
  mozilla::CORSMode mCORSMode;

  // True if the media has an audio track
  bool mHasAudio;

  // True if the media's channel's download has been suspended.
  bool mDownloadSuspendedByCache;
};

#endif
