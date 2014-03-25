/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object to wrap rendering objects that should be scrollable */

#ifndef nsGfxScrollFrame_h___
#define nsGfxScrollFrame_h___

#include "nsContainerFrame.h"
#include "nsIAnonymousContentCreator.h"
#include "nsBoxFrame.h"
#include "nsDisplayList.h"
#include "nsIScrollableFrame.h"
#include "nsIScrollPositionListener.h"
#include "nsIStatefulFrame.h"
#include "nsThreadUtils.h"
#include "nsIReflowCallback.h"
#include "nsBoxLayoutState.h"
#include "nsQueryFrame.h"
#include "nsCOMArray.h"
#include "nsSVGIntegrationUtils.h"
#include "nsExpirationTracker.h"

class nsPresContext;
class nsIPresShell;
class nsIContent;
class nsIAtom;
class nsIDocument;
class nsIScrollFrameInternal;
class nsPresState;
struct ScrollReflowState;

namespace mozilla {
class ScrollbarActivity;
}

// When set, the next scroll operation on the scrollframe will invalidate its
// entire contents. Useful for text-overflow.
// This bit is cleared after each time the scrollframe is scrolled. Whoever
// needs to set it should set it again on each paint.
#define NS_SCROLLFRAME_INVALIDATE_CONTENTS_ON_SCROLL NS_FRAME_STATE_BIT(20)

class nsGfxScrollFrameInner : public nsIReflowCallback {
public:
  class AsyncScroll;

  nsGfxScrollFrameInner(nsContainerFrame* aOuter, bool aIsRoot);
  ~nsGfxScrollFrameInner();

  typedef nsIScrollableFrame::ScrollbarStyles ScrollbarStyles;
  ScrollbarStyles GetScrollbarStylesFromFrame() const;

  // If a child frame was added or removed on the scrollframe,
  // reload our child frame list.
  // We need this if a scrollbar frame is recreated.
  void ReloadChildFrames();

  nsresult CreateAnonymousContent(
    nsTArray<nsIAnonymousContentCreator::ContentInfo>& aElements);
  void AppendAnonymousContentTo(nsBaseContentList& aElements, uint32_t aFilter);
  nsresult FireScrollPortEvent();
  void PostOverflowEvent();
  void Destroy();

  bool ShouldBuildLayer() const;

  nsresult BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                            const nsRect&           aDirtyRect,
                            const nsDisplayListSet& aLists);

  void AppendScrollPartsTo(nsDisplayListBuilder*   aBuilder,
                           const nsRect&           aDirtyRect,
                           const nsDisplayListSet& aLists,
                           bool&                   aCreateLayer,
                           bool                    aPositioned);

  bool GetBorderRadii(nscoord aRadii[8]) const;

  // nsIReflowCallback
  virtual bool ReflowFinished();
  virtual void ReflowCallbackCanceled();

  /**
   * @note This method might destroy the frame, pres shell and other objects.
   * Called when the 'curpos' attribute on one of the scrollbars changes.
   */
  void CurPosAttributeChanged(nsIContent* aChild);

  void PostScrollEvent();
  void FireScrollEvent();
  void PostScrolledAreaEvent();
  void FireScrolledAreaEvent();

  class ScrollEvent : public nsRunnable {
  public:
    NS_DECL_NSIRUNNABLE
    ScrollEvent(nsGfxScrollFrameInner *inner) : mInner(inner) {}
    void Revoke() { mInner = nullptr; }
  private:
    nsGfxScrollFrameInner *mInner;
  };

  class AsyncScrollPortEvent : public nsRunnable {
  public:
    NS_DECL_NSIRUNNABLE
    AsyncScrollPortEvent(nsGfxScrollFrameInner *inner) : mInner(inner) {}
    void Revoke() { mInner = nullptr; }
  private:
    nsGfxScrollFrameInner *mInner;
  };

  class ScrolledAreaEvent : public nsRunnable {
  public:
    NS_DECL_NSIRUNNABLE
    ScrolledAreaEvent(nsGfxScrollFrameInner *inner) : mInner(inner) {}
    void Revoke() { mInner = nullptr; }
  private:
    nsGfxScrollFrameInner *mInner;
  };

  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  void FinishReflowForScrollbar(nsIContent* aContent, nscoord aMinXY,
                                nscoord aMaxXY, nscoord aCurPosXY,
                                nscoord aPageIncrement,
                                nscoord aIncrement);
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  void SetScrollbarEnabled(nsIContent* aContent, nscoord aMaxPos);
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  void SetCoordAttribute(nsIContent* aContent, nsIAtom* aAtom, nscoord aSize);

  nscoord GetCoordAttribute(nsIFrame* aFrame, nsIAtom* aAtom, nscoord aDefaultValue,
                            nscoord* aRangeStart, nscoord* aRangeLength);

  /**
   * @note This method might destroy the frame, pres shell and other objects.
   * Update scrollbar curpos attributes to reflect current scroll position
   */
  void UpdateScrollbarPosition();

  nsRect GetScrollPortRect() const { return mScrollPort; }
  nsPoint GetScrollPosition() const {
    return mScrollPort.TopLeft() - mScrolledFrame->GetPosition();
  }
  /**
   * For LTR frames, the logical scroll position is the offset of the top left
   * corner of the frame from the top left corner of the scroll port (same as
   * GetScrollPosition).
   * For RTL frames, it is the offset of the top right corner of the frame from
   * the top right corner of the scroll port
   */
  nsPoint GetLogicalScrollPosition() const {
    nsPoint pt;
    pt.x = IsLTR() ?
      mScrollPort.x - mScrolledFrame->GetPosition().x :
      mScrollPort.XMost() - mScrolledFrame->GetRect().XMost();
    pt.y = mScrollPort.y - mScrolledFrame->GetPosition().y;
    return pt;
  }
  nsRect GetScrollRange() const;
  // Get the scroll range assuming the scrollport has size (aWidth, aHeight).
  nsRect GetScrollRange(nscoord aWidth, nscoord aHeight) const;
  nsSize GetScrollPositionClampingScrollPortSize() const;
protected:
  nsRect GetScrollRangeForClamping() const;

public:
  static void AsyncScrollCallback(void* anInstance, mozilla::TimeStamp aTime);
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   * aRange is the range of allowable scroll positions around the desired
   * aScrollPosition. Null means only aScrollPosition is allowed.
   * This is a closed-ended range --- aRange.XMost()/aRange.YMost() are allowed.
   */
  void ScrollTo(nsPoint aScrollPosition, nsIScrollableFrame::ScrollMode aMode,
                const nsRect* aRange = nullptr) {
    ScrollToWithOrigin(aScrollPosition, aMode, nsGkAtoms::other, aRange);
  }
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  void ScrollToCSSPixels(nsIntPoint aScrollPosition);
  nsIntPoint GetScrollPositionCSSPixels();
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  void ScrollToImpl(nsPoint aScrollPosition, const nsRect& aRange);
  void ScrollVisual(nsPoint aOldScrolledFramePosition);
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  void ScrollBy(nsIntPoint aDelta, nsIScrollableFrame::ScrollUnit aUnit,
                nsIScrollableFrame::ScrollMode aMode, nsIntPoint* aOverflow, nsIAtom *aOrigin = nullptr);
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  void ScrollToRestoredPosition();

  nsSize GetLineScrollAmount() const;
  nsSize GetPageScrollAmount() const;

  nsPresState* SaveState(nsIStatefulFrame::SpecialStateID aStateID);
  void RestoreState(nsPresState* aState);

  nsIFrame* GetScrolledFrame() const { return mScrolledFrame; }
  nsIFrame* GetScrollbarBox(bool aVertical) const {
    return aVertical ? mVScrollbarBox : mHScrollbarBox;
  }

  void AddScrollPositionListener(nsIScrollPositionListener* aListener) {
    mListeners.AppendElement(aListener);
  }
  void RemoveScrollPositionListener(nsIScrollPositionListener* aListener) {
    mListeners.RemoveElement(aListener);
  }

  static void SetScrollbarVisibility(nsIFrame* aScrollbar, bool aVisible);

  /**
   * GetScrolledRect is designed to encapsulate deciding which
   * directions of overflow should be reachable by scrolling and which
   * should not.  Callers should NOT depend on it having any particular
   * behavior (although nsXULScrollFrame currently does).
   * 
   * This should only be called when the scrolled frame has been
   * reflowed with the scroll port size given in mScrollPort.
   *
   * Currently it allows scrolling down and to the right for
   * nsHTMLScrollFrames with LTR directionality and for all
   * nsXULScrollFrames, and allows scrolling down and to the left for
   * nsHTMLScrollFrames with RTL directionality.
   */
  nsRect GetScrolledRect() const;

  /**
   * GetScrolledRectInternal is designed to encapsulate deciding which
   * directions of overflow should be reachable by scrolling and which
   * should not.  Callers should NOT depend on it having any particular
   * behavior (although nsXULScrollFrame currently does).
   * 
   * Currently it allows scrolling down and to the right for
   * nsHTMLScrollFrames with LTR directionality and for all
   * nsXULScrollFrames, and allows scrolling down and to the left for
   * nsHTMLScrollFrames with RTL directionality.
   */
  nsRect GetScrolledRectInternal(const nsRect& aScrolledOverflowArea,
                                 const nsSize& aScrollPortSize) const;

  uint32_t GetScrollbarVisibility() const {
    return (mHasVerticalScrollbar ? nsIScrollableFrame::VERTICAL : 0) |
           (mHasHorizontalScrollbar ? nsIScrollableFrame::HORIZONTAL : 0);
  }
  nsMargin GetActualScrollbarSizes() const;
  nsMargin GetDesiredScrollbarSizes(nsBoxLayoutState* aState);
  bool IsLTR() const;
  bool IsScrollbarOnRight() const;
  bool IsScrollingActive() const { return mScrollingActive || ShouldBuildLayer(); }

  bool UpdateOverflow();

  // adjust the scrollbar rectangle aRect to account for any visible resizer.
  // aHasResizer specifies if there is a content resizer, however this method
  // will also check if a widget resizer is present as well.
  void AdjustScrollbarRectForResizer(nsIFrame* aFrame, nsPresContext* aPresContext,
                                     nsRect& aRect, bool aHasResizer, bool aVertical);
  // returns true if a resizer should be visible
  bool HasResizer() { return mResizerBox && !mCollapsedResizer; }
  void LayoutScrollbars(nsBoxLayoutState& aState,
                        const nsRect& aContentArea,
                        const nsRect& aOldScrollArea);

  bool IsIgnoringViewportClipping() const;

  bool ShouldClampScrollPosition() const;

  bool IsAlwaysActive() const;
  void MarkActive();
  void MarkInactive();
  nsExpirationState* GetExpirationState() { return &mActivityExpirationState; }

  void ScheduleSyntheticMouseMove();
  static void ScrollActivityCallback(nsITimer *aTimer, void* anInstance);

  // owning references to the nsIAnonymousContentCreator-built content
  nsCOMPtr<nsIContent> mHScrollbarContent;
  nsCOMPtr<nsIContent> mVScrollbarContent;
  nsCOMPtr<nsIContent> mScrollCornerContent;
  nsCOMPtr<nsIContent> mResizerContent;

  nsRevocableEventPtr<ScrollEvent> mScrollEvent;
  nsRevocableEventPtr<AsyncScrollPortEvent> mAsyncScrollPortEvent;
  nsRevocableEventPtr<ScrolledAreaEvent> mScrolledAreaEvent;
  nsIFrame* mHScrollbarBox;
  nsIFrame* mVScrollbarBox;
  nsIFrame* mScrolledFrame;
  nsIFrame* mScrollCornerBox;
  nsIFrame* mResizerBox;
  nsContainerFrame* mOuter;
  nsRefPtr<AsyncScroll> mAsyncScroll;
  nsAutoPtr<mozilla::ScrollbarActivity> mScrollbarActivity;
  nsTArray<nsIScrollPositionListener*> mListeners;
  nsRect mScrollPort;
  // Where we're currently scrolling to, if we're scrolling asynchronously.
  // If we're not in the middle of an asynchronous scroll then this is
  // just the current scroll position. ScrollBy will choose its
  // destination based on this value.
  nsPoint mDestination;
  nsPoint mScrollPosAtLastPaint;

  nsPoint mRestorePos;
  nsPoint mLastPos;

  nsExpirationState mActivityExpirationState;

  nsCOMPtr<nsITimer> mScrollActivityTimer;

  bool mNeverHasVerticalScrollbar:1;
  bool mNeverHasHorizontalScrollbar:1;
  bool mHasVerticalScrollbar:1;
  bool mHasHorizontalScrollbar:1;
  bool mFrameIsUpdatingScrollbar:1;
  bool mDidHistoryRestore:1;
  // Is this the scrollframe for the document's viewport?
  bool mIsRoot:1;
  // If true, don't try to layout the scrollbars in Reflow().  This can be
  // useful if multiple passes are involved, because we don't want to place the
  // scrollbars at the wrong size.
  bool mSupppressScrollbarUpdate:1;
  // If true, we skipped a scrollbar layout due to mSupppressScrollbarUpdate
  // being set at some point.  That means we should lay out scrollbars even if
  // it might not strictly be needed next time mSupppressScrollbarUpdate is
  // false.
  bool mSkippedScrollbarLayout:1;

  bool mHadNonInitialReflow:1;
  // State used only by PostScrollEvents so we know
  // which overflow states have changed.
  bool mHorizontalOverflow:1;
  bool mVerticalOverflow:1;
  bool mPostedReflowCallback:1;
  bool mMayHaveDirtyFixedChildren:1;
  // If true, need to actually update our scrollbar attributes in the
  // reflow callback.
  bool mUpdateScrollbarAttributes:1;
  // If true, we should be prepared to scroll using this scrollframe
  // by placing descendant content into its own layer(s)
  bool mScrollingActive:1;
  // If true, the resizer is collapsed and not displayed
  bool mCollapsedResizer:1;

  // If true, the layer should always be active because we always build a layer.
  // Used for asynchronous scrolling.
  bool mShouldBuildLayer:1;

protected:
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  void ScrollToWithOrigin(nsPoint aScrollPosition,
                          nsIScrollableFrame::ScrollMode aMode,
                          nsIAtom *aOrigin, // nullptr indicates "other" origin
                          const nsRect* aRange);
};

/**
 * The scroll frame creates and manages the scrolling view
 *
 * It only supports having a single child frame that typically is an area
 * frame, but doesn't have to be. The child frame must have a view, though
 *
 * Scroll frames don't support incremental changes, i.e. you can't replace
 * or remove the scrolled frame
 */
class nsHTMLScrollFrame : public nsContainerFrame,
                          public nsIScrollableFrame,
                          public nsIAnonymousContentCreator,
                          public nsIStatefulFrame {
public:
  friend nsIFrame* NS_NewHTMLScrollFrame(nsIPresShell* aPresShell, nsStyleContext* aContext, bool aIsRoot);

  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS

  // Called to set the child frames. We typically have three: the scroll area,
  // the vertical scrollbar, and the horizontal scrollbar.
  NS_IMETHOD SetInitialChildList(ChildListID     aListID,
                                 nsFrameList&    aChildList);

  NS_IMETHOD BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                              const nsRect&           aDirtyRect,
                              const nsDisplayListSet& aLists) {
    return mInner.BuildDisplayList(aBuilder, aDirtyRect, aLists);
  }

  bool TryLayout(ScrollReflowState* aState,
                   nsHTMLReflowMetrics* aKidMetrics,
                   bool aAssumeVScroll, bool aAssumeHScroll,
                   bool aForce, nsresult* aResult);
  bool ScrolledContentDependsOnHeight(ScrollReflowState* aState);
  nsresult ReflowScrolledFrame(ScrollReflowState* aState,
                               bool aAssumeHScroll,
                               bool aAssumeVScroll,
                               nsHTMLReflowMetrics* aMetrics,
                               bool aFirstPass);
  nsresult ReflowContents(ScrollReflowState* aState,
                          const nsHTMLReflowMetrics& aDesiredSize);
  void PlaceScrollArea(const ScrollReflowState& aState,
                       const nsPoint& aScrollPosition);
  nscoord GetIntrinsicVScrollbarWidth(nsRenderingContext *aRenderingContext);

  virtual bool GetBorderRadii(nscoord aRadii[8]) const {
    return mInner.GetBorderRadii(aRadii);
  }

  virtual nscoord GetMinWidth(nsRenderingContext *aRenderingContext);
  virtual nscoord GetPrefWidth(nsRenderingContext *aRenderingContext);
  NS_IMETHOD GetPadding(nsMargin& aPadding);
  virtual bool IsCollapsed();
  
  NS_IMETHOD Reflow(nsPresContext*          aPresContext,
                    nsHTMLReflowMetrics&     aDesiredSize,
                    const nsHTMLReflowState& aReflowState,
                    nsReflowStatus&          aStatus);

  // Because there can be only one child frame, these two function return
  // NS_ERROR_FAILURE
  NS_IMETHOD AppendFrames(ChildListID     aListID,
                          nsFrameList&    aFrameList);
  NS_IMETHOD InsertFrames(ChildListID     aListID,
                          nsIFrame*       aPrevFrame,
                          nsFrameList&    aFrameList);

  virtual void DestroyFrom(nsIFrame* aDestructRoot);


  NS_IMETHOD RemoveFrame(ChildListID     aListID,
                         nsIFrame*       aOldFrame);

  virtual nsIScrollableFrame* GetScrollTargetFrame() {
    return this;
  }

  virtual nsIFrame* GetContentInsertionFrame() {
    return mInner.GetScrolledFrame()->GetContentInsertionFrame();
  }

  virtual void InvalidateInternal(const nsRect& aDamageRect,
                                  nscoord aX, nscoord aY, nsIFrame* aForChild,
                                  uint32_t aFlags);

  virtual bool DoesClipChildren() { return true; }
  virtual nsSplittableType GetSplittableType() const;

  virtual nsPoint GetPositionOfChildIgnoringScrolling(nsIFrame* aChild)
  { nsPoint pt = aChild->GetPosition();
    if (aChild == mInner.GetScrolledFrame()) pt += GetScrollPosition();
    return pt;
  }

  // nsIAnonymousContentCreator
  virtual nsresult CreateAnonymousContent(nsTArray<ContentInfo>& aElements);
  virtual void AppendAnonymousContentTo(nsBaseContentList& aElements,
                                        uint32_t aFilter);

  // nsIScrollableFrame
  virtual nsIFrame* GetScrolledFrame() const {
    return mInner.GetScrolledFrame();
  }
  virtual nsGfxScrollFrameInner::ScrollbarStyles GetScrollbarStyles() const {
    return mInner.GetScrollbarStylesFromFrame();
  }
  virtual uint32_t GetScrollbarVisibility() const {
    return mInner.GetScrollbarVisibility();
  }
  virtual nsMargin GetActualScrollbarSizes() const {
    return mInner.GetActualScrollbarSizes();
  }
  virtual nsMargin GetDesiredScrollbarSizes(nsBoxLayoutState* aState) {
    return mInner.GetDesiredScrollbarSizes(aState);
  }
  virtual nsMargin GetDesiredScrollbarSizes(nsPresContext* aPresContext,
          nsRenderingContext* aRC) {
    nsBoxLayoutState bls(aPresContext, aRC, 0);
    return GetDesiredScrollbarSizes(&bls);
  }
  virtual nsRect GetScrollPortRect() const {
    return mInner.GetScrollPortRect();
  }
  virtual nsPoint GetScrollPosition() const {
    return mInner.GetScrollPosition();
  }
  virtual nsRect GetScrollRange() const {
    return mInner.GetScrollRange();
  }
  virtual nsSize GetScrollPositionClampingScrollPortSize() const {
    return mInner.GetScrollPositionClampingScrollPortSize();
  }
  virtual nsSize GetLineScrollAmount() const {
    return mInner.GetLineScrollAmount();
  }
  virtual nsSize GetPageScrollAmount() const {
    return mInner.GetPageScrollAmount();
  }
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void ScrollTo(nsPoint aScrollPosition, ScrollMode aMode,
                        const nsRect* aRange = nullptr) {
    mInner.ScrollTo(aScrollPosition, aMode, aRange);
  }
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void ScrollToCSSPixels(nsIntPoint aScrollPosition) {
    mInner.ScrollToCSSPixels(aScrollPosition);
  }
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual nsIntPoint GetScrollPositionCSSPixels() {
    return mInner.GetScrollPositionCSSPixels();
  }
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void ScrollBy(nsIntPoint aDelta, ScrollUnit aUnit, ScrollMode aMode,
                        nsIntPoint* aOverflow, nsIAtom *aOrigin = nullptr) {
    mInner.ScrollBy(aDelta, aUnit, aMode, aOverflow, aOrigin);
  }
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void ScrollToRestoredPosition() {
    mInner.ScrollToRestoredPosition();
  }
  virtual void AddScrollPositionListener(nsIScrollPositionListener* aListener) {
    mInner.AddScrollPositionListener(aListener);
  }
  virtual void RemoveScrollPositionListener(nsIScrollPositionListener* aListener) {
    mInner.RemoveScrollPositionListener(aListener);
  }
  virtual nsIFrame* GetScrollbarBox(bool aVertical) {
    return mInner.GetScrollbarBox(aVertical);
  }
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void CurPosAttributeChanged(nsIContent* aChild) {
    mInner.CurPosAttributeChanged(aChild);
  }
  NS_IMETHOD PostScrolledAreaEventForCurrentArea() {
    mInner.PostScrolledAreaEvent();
    return NS_OK;
  }
  virtual bool IsScrollingActive() {
    return mInner.IsScrollingActive();
  }
  virtual bool UpdateOverflow() {
    return mInner.UpdateOverflow();
  }

  // nsIStatefulFrame
  NS_IMETHOD SaveState(SpecialStateID aStateID, nsPresState** aState) {
    NS_ENSURE_ARG_POINTER(aState);
    *aState = mInner.SaveState(aStateID);
    return NS_OK;
  }
  NS_IMETHOD RestoreState(nsPresState* aState) {
    NS_ENSURE_ARG_POINTER(aState);
    mInner.RestoreState(aState);
    return NS_OK;
  }

  /**
   * Get the "type" of the frame
   *
   * @see nsGkAtoms::scrollFrame
   */
  virtual nsIAtom* GetType() const;
  
#ifdef DEBUG
  NS_IMETHOD GetFrameName(nsAString& aResult) const;
#endif

  bool DidHistoryRestore() { return mInner.mDidHistoryRestore; }

#ifdef ACCESSIBILITY
  virtual already_AddRefed<Accessible> CreateAccessible();
#endif

protected:
  nsHTMLScrollFrame(nsIPresShell* aShell, nsStyleContext* aContext, bool aIsRoot);
  virtual int GetSkipSides() const;
  
  void SetSuppressScrollbarUpdate(bool aSuppress) {
    mInner.mSupppressScrollbarUpdate = aSuppress;
  }
  bool GuessHScrollbarNeeded(const ScrollReflowState& aState);
  bool GuessVScrollbarNeeded(const ScrollReflowState& aState);

  bool IsScrollbarUpdateSuppressed() const {
    return mInner.mSupppressScrollbarUpdate;
  }

  // Return whether we're in an "initial" reflow.  Some reflows with
  // NS_FRAME_FIRST_REFLOW set are NOT "initial" as far as we're concerned.
  bool InInitialReflow() const;
  
  /**
   * Override this to return false if computed height/min-height/max-height
   * should NOT be propagated to child content.
   * nsListControlFrame uses this.
   */
  virtual bool ShouldPropagateComputedHeightToScrolledContent() const { return true; }

private:
  friend class nsGfxScrollFrameInner;
  nsGfxScrollFrameInner mInner;
};

/**
 * The scroll frame creates and manages the scrolling view
 *
 * It only supports having a single child frame that typically is an area
 * frame, but doesn't have to be. The child frame must have a view, though
 *
 * Scroll frames don't support incremental changes, i.e. you can't replace
 * or remove the scrolled frame
 */
class nsXULScrollFrame : public nsBoxFrame,
                         public nsIScrollableFrame,
                         public nsIAnonymousContentCreator,
                         public nsIStatefulFrame {
public:
  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS

  friend nsIFrame* NS_NewXULScrollFrame(nsIPresShell* aPresShell, nsStyleContext* aContext, bool aIsRoot);

  // Called to set the child frames. We typically have three: the scroll area,
  // the vertical scrollbar, and the horizontal scrollbar.
  NS_IMETHOD SetInitialChildList(ChildListID     aListID,
                                 nsFrameList&    aChildList);

  NS_IMETHOD BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                              const nsRect&           aDirtyRect,
                              const nsDisplayListSet& aLists) {
    return mInner.BuildDisplayList(aBuilder, aDirtyRect, aLists);
  }

  // XXXldb Is this actually used?
#if 0
  virtual nscoord GetMinWidth(nsRenderingContext *aRenderingContext);
#endif

  // Because there can be only one child frame, these two function return
  // NS_ERROR_FAILURE
  NS_IMETHOD AppendFrames(ChildListID     aListID,
                          nsFrameList&    aFrameList);
  NS_IMETHOD InsertFrames(ChildListID     aListID,
                          nsIFrame*       aPrevFrame,
                          nsFrameList&    aFrameList);

  virtual void DestroyFrom(nsIFrame* aDestructRoot);

  NS_IMETHOD RemoveFrame(ChildListID     aListID,
                         nsIFrame*       aOldFrame);

  virtual nsIScrollableFrame* GetScrollTargetFrame() {
    return this;
  }

  virtual nsIFrame* GetContentInsertionFrame() {
    return mInner.GetScrolledFrame()->GetContentInsertionFrame();
  }

  virtual void InvalidateInternal(const nsRect& aDamageRect,
                                  nscoord aX, nscoord aY, nsIFrame* aForChild,
                                  uint32_t aFlags);

  virtual bool DoesClipChildren() { return true; }
  virtual nsSplittableType GetSplittableType() const;

  virtual nsPoint GetPositionOfChildIgnoringScrolling(nsIFrame* aChild)
  { nsPoint pt = aChild->GetPosition();
    if (aChild == mInner.GetScrolledFrame())
      pt += mInner.GetLogicalScrollPosition();
    return pt;
  }

  // nsIAnonymousContentCreator
  virtual nsresult CreateAnonymousContent(nsTArray<ContentInfo>& aElements);
  virtual void AppendAnonymousContentTo(nsBaseContentList& aElements,
                                        uint32_t aFilter);

  virtual nsSize GetMinSize(nsBoxLayoutState& aBoxLayoutState);
  virtual nsSize GetPrefSize(nsBoxLayoutState& aBoxLayoutState);
  virtual nsSize GetMaxSize(nsBoxLayoutState& aBoxLayoutState);
  virtual nscoord GetBoxAscent(nsBoxLayoutState& aBoxLayoutState);

  NS_IMETHOD DoLayout(nsBoxLayoutState& aBoxLayoutState);
  NS_IMETHOD GetPadding(nsMargin& aPadding);

  virtual bool GetBorderRadii(nscoord aRadii[8]) const {
    return mInner.GetBorderRadii(aRadii);
  }

  nsresult Layout(nsBoxLayoutState& aState);
  void LayoutScrollArea(nsBoxLayoutState& aState, const nsPoint& aScrollPosition);

  static bool AddRemoveScrollbar(bool& aHasScrollbar, 
                                   nscoord& aXY, 
                                   nscoord& aSize, 
                                   nscoord aSbSize, 
                                   bool aOnRightOrBottom, 
                                   bool aAdd);
  
  bool AddRemoveScrollbar(nsBoxLayoutState& aState, 
                            bool aOnRightOrBottom, 
                            bool aHorizontal, 
                            bool aAdd);
  
  bool AddHorizontalScrollbar (nsBoxLayoutState& aState, bool aOnBottom);
  bool AddVerticalScrollbar   (nsBoxLayoutState& aState, bool aOnRight);
  void RemoveHorizontalScrollbar(nsBoxLayoutState& aState, bool aOnBottom);
  void RemoveVerticalScrollbar  (nsBoxLayoutState& aState, bool aOnRight);

  static void AdjustReflowStateForPrintPreview(nsBoxLayoutState& aState, bool& aSetBack);
  static void AdjustReflowStateBack(nsBoxLayoutState& aState, bool aSetBack);

  // nsIScrollableFrame
  virtual nsIFrame* GetScrolledFrame() const {
    return mInner.GetScrolledFrame();
  }
  virtual nsGfxScrollFrameInner::ScrollbarStyles GetScrollbarStyles() const {
    return mInner.GetScrollbarStylesFromFrame();
  }
  virtual uint32_t GetScrollbarVisibility() const {
    return mInner.GetScrollbarVisibility();
  }
  virtual nsMargin GetActualScrollbarSizes() const {
    return mInner.GetActualScrollbarSizes();
  }
  virtual nsMargin GetDesiredScrollbarSizes(nsBoxLayoutState* aState) {
    return mInner.GetDesiredScrollbarSizes(aState);
  }
  virtual nsMargin GetDesiredScrollbarSizes(nsPresContext* aPresContext,
          nsRenderingContext* aRC) {
    nsBoxLayoutState bls(aPresContext, aRC, 0);
    return GetDesiredScrollbarSizes(&bls);
  }
  virtual nsRect GetScrollPortRect() const {
    return mInner.GetScrollPortRect();
  }
  virtual nsPoint GetScrollPosition() const {
    return mInner.GetScrollPosition();
  }
  virtual nsRect GetScrollRange() const {
    return mInner.GetScrollRange();
  }
  virtual nsSize GetScrollPositionClampingScrollPortSize() const {
    return mInner.GetScrollPositionClampingScrollPortSize();
  }
  virtual nsSize GetLineScrollAmount() const {
    return mInner.GetLineScrollAmount();
  }
  virtual nsSize GetPageScrollAmount() const {
    return mInner.GetPageScrollAmount();
  }
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void ScrollTo(nsPoint aScrollPosition, ScrollMode aMode,
                        const nsRect* aRange = nullptr) {
    mInner.ScrollTo(aScrollPosition, aMode, aRange);
  }
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void ScrollToCSSPixels(nsIntPoint aScrollPosition) {
    mInner.ScrollToCSSPixels(aScrollPosition);
  }
  virtual nsIntPoint GetScrollPositionCSSPixels() {
    return mInner.GetScrollPositionCSSPixels();
  }
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void ScrollBy(nsIntPoint aDelta, ScrollUnit aUnit, ScrollMode aMode,
                        nsIntPoint* aOverflow, nsIAtom *aOrigin = nullptr) {
    mInner.ScrollBy(aDelta, aUnit, aMode, aOverflow, aOrigin);
  }
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void ScrollToRestoredPosition() {
    mInner.ScrollToRestoredPosition();
  }
  virtual void AddScrollPositionListener(nsIScrollPositionListener* aListener) {
    mInner.AddScrollPositionListener(aListener);
  }
  virtual void RemoveScrollPositionListener(nsIScrollPositionListener* aListener) {
    mInner.RemoveScrollPositionListener(aListener);
  }
  virtual nsIFrame* GetScrollbarBox(bool aVertical) {
    return mInner.GetScrollbarBox(aVertical);
  }
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void CurPosAttributeChanged(nsIContent* aChild) {
    mInner.CurPosAttributeChanged(aChild);
  }
  NS_IMETHOD PostScrolledAreaEventForCurrentArea() {
    mInner.PostScrolledAreaEvent();
    return NS_OK;
  }
  virtual bool IsScrollingActive() {
    return mInner.IsScrollingActive();
  }
  virtual bool UpdateOverflow() {
    return mInner.UpdateOverflow();
  }

  // nsIStatefulFrame
  NS_IMETHOD SaveState(SpecialStateID aStateID, nsPresState** aState) {
    NS_ENSURE_ARG_POINTER(aState);
    *aState = mInner.SaveState(aStateID);
    return NS_OK;
  }
  NS_IMETHOD RestoreState(nsPresState* aState) {
    NS_ENSURE_ARG_POINTER(aState);
    mInner.RestoreState(aState);
    return NS_OK;
  }

  /**
   * Get the "type" of the frame
   *
   * @see nsGkAtoms::scrollFrame
   */
  virtual nsIAtom* GetType() const;
  
  virtual bool IsFrameOfType(uint32_t aFlags) const
  {
    // Override bogus IsFrameOfType in nsBoxFrame.
    if (aFlags & (nsIFrame::eReplacedContainsBlock | nsIFrame::eReplaced))
      return false;
    return nsBoxFrame::IsFrameOfType(aFlags);
  }

#ifdef DEBUG
  NS_IMETHOD GetFrameName(nsAString& aResult) const;
#endif

protected:
  nsXULScrollFrame(nsIPresShell* aShell, nsStyleContext* aContext, bool aIsRoot);
  virtual int GetSkipSides() const;

  void ClampAndSetBounds(nsBoxLayoutState& aState, 
                         nsRect& aRect,
                         nsPoint aScrollPosition,
                         bool aRemoveOverflowAreas = false) {
    /* 
     * For RTL frames, restore the original scrolled position of the right
     * edge, then subtract the current width to find the physical position.
     */
    if (!mInner.IsLTR()) {
      aRect.x = mInner.mScrollPort.XMost() - aScrollPosition.x - aRect.width;
    }
    mInner.mScrolledFrame->SetBounds(aState, aRect, aRemoveOverflowAreas);
  }

private:
  friend class nsGfxScrollFrameInner;
  nsGfxScrollFrameInner mInner;
};

#endif /* nsGfxScrollFrame_h___ */
