/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsListControlFrame_h___
#define nsListControlFrame_h___

#ifdef DEBUG_evaughan
//#define DEBUG_rods
#endif

#ifdef DEBUG_rods
//#define DO_REFLOW_DEBUG
//#define DO_REFLOW_COUNTER
//#define DO_UNCONSTRAINED_CHECK
//#define DO_PIXELS
#endif

#include "nsGfxScrollFrame.h"
#include "nsIFormControlFrame.h"
#include "nsIListControlFrame.h"
#include "nsISelectControlFrame.h"
#include "nsIDOMEventListener.h"
#include "nsIContent.h"
#include "nsAutoPtr.h"
#include "nsSelectsAreaFrame.h"

class nsIDOMHTMLSelectElement;
class nsIDOMHTMLOptionsCollection;
class nsIDOMHTMLOptionElement;
class nsIComboboxControlFrame;
class nsPresContext;
class nsListEventListener;

/**
 * Frame-based listbox.
 */

class nsListControlFrame : public nsHTMLScrollFrame,
                           public nsIFormControlFrame, 
                           public nsIListControlFrame,
                           public nsISelectControlFrame
{
public:
  friend nsIFrame* NS_NewListControlFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);

  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS

    // nsIFrame
  NS_IMETHOD HandleEvent(nsPresContext* aPresContext,
                         nsGUIEvent* aEvent,
                         nsEventStatus* aEventStatus);
  
  NS_IMETHOD SetInitialChildList(ChildListID     aListID,
                                 nsFrameList&    aChildList);

  virtual nscoord GetPrefWidth(nsRenderingContext *aRenderingContext);
  virtual nscoord GetMinWidth(nsRenderingContext *aRenderingContext);

  NS_IMETHOD Reflow(nsPresContext*          aCX,
                    nsHTMLReflowMetrics&     aDesiredSize,
                    const nsHTMLReflowState& aReflowState,
                    nsReflowStatus&          aStatus);

  NS_IMETHOD Init(nsIContent*      aContent,
                   nsIFrame*        aParent,
                   nsIFrame*        aPrevInFlow);

  NS_IMETHOD DidReflow(nsPresContext*           aPresContext, 
                       const nsHTMLReflowState*  aReflowState, 
                       nsDidReflowStatus         aStatus);
  virtual void DestroyFrom(nsIFrame* aDestructRoot);

  NS_IMETHOD BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                              const nsRect&           aDirtyRect,
                              const nsDisplayListSet& aLists);

  virtual nsIFrame* GetContentInsertionFrame();

  /**
   * Get the "type" of the frame
   *
   * @see nsGkAtoms::scrollFrame
   */
  virtual nsIAtom* GetType() const;

  virtual bool IsFrameOfType(uint32_t aFlags) const
  {
    return nsHTMLScrollFrame::IsFrameOfType(aFlags &
      ~(nsIFrame::eReplaced | nsIFrame::eReplacedContainsBlock));
  }

  virtual void InvalidateInternal(const nsRect& aDamageRect,
                                  nscoord aX, nscoord aY, nsIFrame* aForChild,
                                  uint32_t aFlags);

#ifdef DEBUG
  NS_IMETHOD GetFrameName(nsAString& aResult) const;
#endif

    // nsIFormControlFrame
  virtual nsresult SetFormProperty(nsIAtom* aName, const nsAString& aValue);
  virtual nsresult GetFormProperty(nsIAtom* aName, nsAString& aValue) const; 
  virtual void SetFocus(bool aOn = true, bool aRepaint = false);

  virtual nsGfxScrollFrameInner::ScrollbarStyles GetScrollbarStyles() const;
  virtual bool ShouldPropagateComputedHeightToScrolledContent() const;

    // for accessibility purposes
#ifdef ACCESSIBILITY
  virtual already_AddRefed<Accessible> CreateAccessible();
#endif

    // nsContainerFrame
  virtual int GetSkipSides() const;

    // nsIListControlFrame
  virtual void SetComboboxFrame(nsIFrame* aComboboxFrame);
  virtual int32_t GetSelectedIndex();
  virtual already_AddRefed<nsIContent> GetCurrentOption();

  /**
   * Gets the text of the currently selected item.
   * If the there are zero items then an empty string is returned
   * If there is nothing selected, then the 0th item's text is returned.
   */
  virtual void GetOptionText(int32_t aIndex, nsAString & aStr);

  virtual void CaptureMouseEvents(bool aGrabMouseEvents);
  virtual nscoord GetHeightOfARow();
  virtual int32_t GetNumberOfOptions();  
  virtual void AboutToDropDown();

  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void AboutToRollup();

  /**
   * Dispatch a DOM onchange event synchroniously.
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void FireOnChange();

  /**
   * Makes aIndex the selected option of a combobox list.
   * @note This method might destroy the frame, pres shell and other objects.
   */
  virtual void ComboboxFinish(int32_t aIndex);
  virtual void OnContentReset();

  // nsISelectControlFrame
  NS_IMETHOD AddOption(int32_t index);
  NS_IMETHOD RemoveOption(int32_t index);
  NS_IMETHOD DoneAddingChildren(bool aIsDone);

  /**
   * Gets the content (an option) by index and then set it as
   * being selected or not selected.
   */
  NS_IMETHOD OnOptionSelected(int32_t aIndex, bool aSelected);
  NS_IMETHOD OnSetSelectedIndex(int32_t aOldIndex, int32_t aNewIndex);

  /**
   * Mouse event listeners.
   * @note These methods might destroy the frame, pres shell and other objects.
   */
  nsresult MouseDown(nsIDOMEvent* aMouseEvent);
  nsresult MouseUp(nsIDOMEvent* aMouseEvent);
  nsresult MouseMove(nsIDOMEvent* aMouseEvent);
  nsresult DragMove(nsIDOMEvent* aMouseEvent);
  nsresult KeyPress(nsIDOMEvent* aKeyEvent);

  /**
   * Returns the options collection for aContent, if any.
   */
  static already_AddRefed<nsIDOMHTMLOptionsCollection>
    GetOptions(nsIContent * aContent);

  /**
   * Returns the nsIDOMHTMLOptionElement for a given index 
   * in the select's collection.
   */
  static already_AddRefed<nsIDOMHTMLOptionElement>
    GetOption(nsIDOMHTMLOptionsCollection* aOptions, int32_t aIndex);

  /**
   * Returns the nsIContent object in the collection 
   * for a given index.
   */
  static already_AddRefed<nsIContent>
    GetOptionAsContent(nsIDOMHTMLOptionsCollection* aCollection,int32_t aIndex);

  static void ComboboxFocusSet();

  // Helper
  bool IsFocused() { return this == mFocused; }

  /**
   * Function to paint the focus rect when our nsSelectsAreaFrame is painting.
   * @param aPt the offset of this frame, relative to the rendering reference
   * frame
   */
  void PaintFocus(nsRenderingContext& aRC, nsPoint aPt);
  /**
   * If this frame IsFocused(), invalidates an area that includes anything
   * that PaintFocus will or could have painted --- basically the whole
   * GetOptionsContainer, plus some extra stuff if there are no options. This
   * must be called every time mEndSelectionIndex changes.
   */
  void InvalidateFocus();

  /**
   * Function to calculate the height a row, for use with the "size" attribute.
   * Can't be const because GetNumberOfOptions() isn't const.
   */
  nscoord CalcHeightOfARow();

  /**
   * Function to ask whether we're currently in what might be the
   * first pass of a two-pass reflow.
   */
  bool MightNeedSecondPass() const {
    return mMightNeedSecondPass;
  }

  void SetSuppressScrollbarUpdate(bool aSuppress) {
    nsHTMLScrollFrame::SetSuppressScrollbarUpdate(aSuppress);
  }

  /**
   * Return whether the list is in dropdown mode.
   */
  bool IsInDropDownMode() const;

  /**
   * Return the number of displayed rows in the list.
   */
  uint32_t GetNumDisplayRows() const { return mNumDisplayRows; }

  /**
   * Return true if the drop-down list can display more rows.
   * (always false if not in drop-down mode)
   */
  bool GetDropdownCanGrow() const { return mDropdownCanGrow; }

  /**
   * Dropdowns need views
   */
  virtual bool NeedsView() { return IsInDropDownMode(); }

  /**
   * Frees statics owned by this class.
   */
  static void Shutdown();

#ifdef ACCESSIBILITY
  /**
   * Post a custom DOM event for the change, so that accessibility can
   * fire a native focus event for accessibility 
   * (Some 3rd party products need to track our focus)
   */
  void FireMenuItemActiveEvent(); // Inform assistive tech what got focused
#endif

protected:
  /**
   * Updates the selected text in a combobox and then calls FireOnChange().
   * @note This method might destroy the frame, pres shell and other objects.
   * Returns false if calling it destroyed |this|.
   */
  bool       UpdateSelection();

  /**
   * Returns whether mContent supports multiple selection.
   */
  bool       GetMultiple() const {
    return mContent->HasAttr(kNameSpaceID_None, nsGkAtoms::multiple);
  }


  /**
   * Toggles (show/hide) the combobox dropdown menu.
   * @note This method might destroy the frame, pres shell and other objects.
   */
  void       DropDownToggleKey(nsIDOMEvent* aKeyEvent);

  nsresult   IsOptionDisabled(int32_t anIndex, bool &aIsDisabled);
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  nsresult   ScrollToFrame(nsIContent * aOptElement);
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  nsresult   ScrollToIndex(int32_t anIndex);

  /**
   * When the user clicks on the comboboxframe to show the dropdown
   * listbox, they then have to move the mouse into the list. We don't
   * want to process those mouse events as selection events (i.e., to
   * scroll list items into view). So we ignore the events until
   * the mouse moves below our border-inner-edge, when
   * mItemSelectionStarted is set.
   *
   * @param aPoint relative to this frame
   */
  bool       IgnoreMouseEventForSelection(nsIDOMEvent* aEvent);

  /**
   * If the dropdown is showing and the mouse has moved below our
   * border-inner-edge, then set mItemSelectionStarted.
   */
  void       UpdateInListState(nsIDOMEvent* aEvent);
  void       AdjustIndexForDisabledOpt(int32_t aStartIndex, int32_t &anNewIndex,
                                       int32_t aNumOptions, int32_t aDoAdjustInc, int32_t aDoAdjustIncNext);

  /**
   * Resets the select back to it's original default values;
   * those values as determined by the original HTML
   */
  virtual void ResetList(bool aAllowScrolling);

  nsListControlFrame(nsIPresShell* aShell, nsIDocument* aDocument, nsStyleContext* aContext);
  virtual ~nsListControlFrame();

  // Utility methods
  nsresult GetSizeAttribute(uint32_t *aSize);
  nsIContent* GetOptionFromContent(nsIContent *aContent);

  /**
   * Sets the mSelectedIndex and mOldSelectedIndex from figuring out what 
   * item was selected using content
   * @param aPoint the event point, in listcontrolframe coordinates
   * @return NS_OK if it successfully found the selection
   */
  nsresult GetIndexFromDOMEvent(nsIDOMEvent* aMouseEvent, int32_t& aCurIndex);

  /**
   * For a given index it returns the nsIContent object 
   * from the select.
   */
  already_AddRefed<nsIContent> GetOptionContent(int32_t aIndex) const;

  /** 
   * For a given piece of content, it determines whether the 
   * content (an option) is selected or not.
   * @return true if it is, false if it is NOT.
   */
  bool     IsContentSelected(nsIContent* aContent) const;

  /**
   * For a given index is return whether the content is selected.
   */
  bool     IsContentSelectedByIndex(int32_t aIndex) const;

  bool     CheckIfAllFramesHere();
  int32_t  GetIndexFromContent(nsIContent *aContent);
  bool     IsLeftButton(nsIDOMEvent* aMouseEvent);

  // guess at a row height based on our own style.
  nscoord  CalcFallbackRowHeight(float aFontSizeInflation);

  // CalcIntrinsicHeight computes our intrinsic height (taking the "size"
  // attribute into account).  This should only be called in non-dropdown mode.
  nscoord CalcIntrinsicHeight(nscoord aHeightOfARow, int32_t aNumberOfOptions);

  // Dropped down stuff
  void     SetComboboxItem(int32_t aIndex);

  /**
   * Method to reflow ourselves as a dropdown list.  This differs from
   * reflow as a listbox because the criteria for needing a second
   * pass are different.  This will be called from Reflow() as needed.
   */
  nsresult ReflowAsDropdown(nsPresContext*           aPresContext,
                            nsHTMLReflowMetrics&     aDesiredSize,
                            const nsHTMLReflowState& aReflowState,
                            nsReflowStatus&          aStatus);

  // Selection
  bool     SetOptionsSelectedFromFrame(int32_t aStartIndex,
                                       int32_t aEndIndex,
                                       bool aValue,
                                       bool aClearAll);
  bool     ToggleOptionSelectedFromFrame(int32_t aIndex);
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  bool     SingleSelection(int32_t aClickedIndex, bool aDoToggle);
  bool     ExtendedSelection(int32_t aStartIndex, int32_t aEndIndex,
                             bool aClearAll);
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  bool     PerformSelection(int32_t aClickedIndex, bool aIsShift,
                            bool aIsControl);
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  bool     HandleListSelection(nsIDOMEvent * aDOMEvent, int32_t selectedIndex);
  void     InitSelectionRange(int32_t aClickedIndex);

public:
  nsSelectsAreaFrame* GetOptionsContainer() const {
    return static_cast<nsSelectsAreaFrame*>(GetScrolledFrame());
  }

protected:
  nscoord HeightOfARow() {
    return GetOptionsContainer()->HeightOfARow();
  }
  
  // Data Members
  int32_t      mStartSelectionIndex;
  int32_t      mEndSelectionIndex;

  nsIComboboxControlFrame *mComboboxFrame;
  uint32_t     mNumDisplayRows;
  bool mChangesSinceDragStart:1;
  bool mButtonDown:1;
  // Has the user selected a visible item since we showed the
  // dropdown?
  bool mItemSelectionStarted:1;

  bool mIsAllContentHere:1;
  bool mIsAllFramesHere:1;
  bool mHasBeenInitialized:1;
  bool mNeedToReset:1;
  bool mPostChildrenLoadedReset:1;

  //bool value for multiple discontiguous selection
  bool mControlSelectMode:1;

  // True if we're in the middle of a reflow and might need a second
  // pass.  This only happens for auto heights.
  bool mMightNeedSecondPass:1;

  /**
   * Set to aPresContext->HasPendingInterrupt() at the start of Reflow.
   * Set to false at the end of DidReflow.
   */
  bool mHasPendingInterruptAtStartOfReflow:1;

  // True if the drop-down can show more rows.  Always false if this list
  // is not in drop-down mode.
  bool mDropdownCanGrow:1;
  
  // The last computed height we reflowed at if we're a combobox dropdown.
  // XXXbz should we be using a subclass here?  Or just not worry
  // about the extra member on listboxes?
  nscoord mLastDropdownComputedHeight;

  // At the time of our last dropdown, the backstop color to draw in case we
  // are translucent.
  nscolor mLastDropdownBackstopColor;
  
  nsRefPtr<nsListEventListener> mEventListener;

  static nsListControlFrame * mFocused;
  static nsString * sIncrementalString;

#ifdef DO_REFLOW_COUNTER
  int32_t mReflowId;
#endif

private:
  // for incremental typing navigation
  static nsAString& GetIncrementalString ();
  static DOMTimeStamp gLastKeyTime;
};

#endif /* nsListControlFrame_h___ */

