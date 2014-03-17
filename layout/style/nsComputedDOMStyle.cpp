/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set tw=78 expandtab softtabstop=2 ts=2 sw=2: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* DOM object returned from element.getComputedStyle() */

#include "mozilla/Util.h"

#include "nsComputedDOMStyle.h"

#include "nsError.h"
#include "nsDOMString.h"
#include "nsIDOMCSS2Properties.h"
#include "nsIDOMElement.h"
#include "nsIDOMCSSPrimitiveValue.h"
#include "nsStyleContext.h"
#include "nsIScrollableFrame.h"
#include "nsContentUtils.h"
#include "prprf.h"

#include "nsCSSProps.h"
#include "nsCSSKeywords.h"
#include "nsDOMCSSRect.h"
#include "nsGkAtoms.h"
#include "nsHTMLReflowState.h"
#include "nsThemeConstants.h"
#include "nsStyleUtil.h"
#include "nsStyleStructInlines.h"

#include "nsPresContext.h"
#include "nsIDocument.h"

#include "nsCSSPseudoElements.h"
#include "nsStyleSet.h"
#include "imgIRequest.h"
#include "nsLayoutUtils.h"
#include "nsFrameManager.h"
#include "prlog.h"
#include "nsCSSKeywords.h"
#include "nsStyleCoord.h"
#include "nsDisplayList.h"
#include "nsDOMCSSDeclaration.h"
#include "mozilla/dom/Element.h"
#include "nsGenericElement.h"
#include "CSSCalc.h"
#include "nsWrapperCacheInlines.h"

using namespace mozilla;
using namespace mozilla::dom;

#if defined(DEBUG_bzbarsky) || defined(DEBUG_caillon)
#define DEBUG_ComputedDOMStyle
#endif

/*
 * This is the implementation of the readonly CSSStyleDeclaration that is
 * returned by the getComputedStyle() function.
 */

static nsComputedDOMStyle *sCachedComputedDOMStyle;

already_AddRefed<nsComputedDOMStyle>
NS_NewComputedDOMStyle(dom::Element* aElement, const nsAString& aPseudoElt,
                       nsIPresShell* aPresShell)
{
  nsRefPtr<nsComputedDOMStyle> computedStyle;
  if (sCachedComputedDOMStyle) {
    // There's an unused nsComputedDOMStyle cached, use it.
    // But before we use it, re-initialize the object.

    // Oh yeah baby, placement new!
    computedStyle = new (sCachedComputedDOMStyle)
      nsComputedDOMStyle(aElement, aPseudoElt, aPresShell);

    sCachedComputedDOMStyle = nullptr;
  } else {
    // No nsComputedDOMStyle cached, create a new one.

    computedStyle = new nsComputedDOMStyle(aElement, aPseudoElt, aPresShell);
  }

  return computedStyle.forget();
}

static nsIFrame*
GetContainingBlockFor(nsIFrame* aFrame) {
  if (!aFrame) {
    return nullptr;
  }
  return aFrame->GetContainingBlock();
}

nsComputedDOMStyle::nsComputedDOMStyle(dom::Element* aElement,
                                       const nsAString& aPseudoElt,
                                       nsIPresShell* aPresShell)
  : mDocumentWeak(nullptr), mOuterFrame(nullptr),
    mInnerFrame(nullptr), mPresShell(nullptr),
    mExposeVisitedStyle(false)
{
  MOZ_ASSERT(aElement && aPresShell);

  mDocumentWeak = do_GetWeakReference(aPresShell->GetDocument());

  mContent = aElement;

  if (!DOMStringIsNull(aPseudoElt) && !aPseudoElt.IsEmpty() &&
      aPseudoElt.First() == PRUnichar(':')) {
    // deal with two-colon forms of aPseudoElt
    nsAString::const_iterator start, end;
    aPseudoElt.BeginReading(start);
    aPseudoElt.EndReading(end);
    NS_ASSERTION(start != end, "aPseudoElt is not empty!");
    ++start;
    bool haveTwoColons = true;
    if (start == end || *start != PRUnichar(':')) {
      --start;
      haveTwoColons = false;
    }
    mPseudo = do_GetAtom(Substring(start, end));
    MOZ_ASSERT(mPseudo);

    // There aren't any non-CSS2 pseudo-elements with a single ':'
    if (!haveTwoColons &&
        !nsCSSPseudoElements::IsCSS2PseudoElement(mPseudo)) {
      // XXXbz I'd really rather we threw an exception or something, but
      // the DOM spec sucks.
      mPseudo = nullptr;
    }
  }

  MOZ_ASSERT(aPresShell->GetPresContext());
}


nsComputedDOMStyle::~nsComputedDOMStyle()
{
}

void
nsComputedDOMStyle::Shutdown()
{
  // We want to de-allocate without calling the dtor since we
  // already did that manually in doDestroyComputedDOMStyle(),
  // so cast our cached object to something that doesn't know
  // about our dtor.
  delete reinterpret_cast<char*>(sCachedComputedDOMStyle);
  sCachedComputedDOMStyle = nullptr;
}


NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_1(nsComputedDOMStyle, mContent)

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(nsComputedDOMStyle)
  return tmp->IsBlack();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(nsComputedDOMStyle)
  return tmp->IsBlack();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(nsComputedDOMStyle)
  return tmp->IsBlack();
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

// QueryInterface implementation for nsComputedDOMStyle
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsComputedDOMStyle)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
NS_INTERFACE_MAP_END_INHERITING(nsDOMCSSDeclaration)


static void doDestroyComputedDOMStyle(nsComputedDOMStyle *aComputedStyle)
{
  if (!sCachedComputedDOMStyle) {
    // The cache is empty, store aComputedStyle in the cache.

    sCachedComputedDOMStyle = aComputedStyle;
    sCachedComputedDOMStyle->~nsComputedDOMStyle();
  } else {
    // The cache is full, delete aComputedStyle

    delete aComputedStyle;
  }
}

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsComputedDOMStyle)
NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_DESTROY(nsComputedDOMStyle,
                                              doDestroyComputedDOMStyle(this))


NS_IMETHODIMP
nsComputedDOMStyle::GetPropertyValue(const nsCSSProperty aPropID,
                                     nsAString& aValue)
{
  // This is mostly to avoid code duplication with GetPropertyCSSValue(); if
  // perf ever becomes an issue here (doubtful), we can look into changing
  // this.
  return GetPropertyValue(
    NS_ConvertASCIItoUTF16(nsCSSProps::GetStringValue(aPropID)),
    aValue);
}

NS_IMETHODIMP
nsComputedDOMStyle::SetPropertyValue(const nsCSSProperty aPropID,
                                     const nsAString& aValue)
{
  return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
}


NS_IMETHODIMP
nsComputedDOMStyle::GetCssText(nsAString& aCssText)
{
  aCssText.Truncate();

  return NS_OK;
}


NS_IMETHODIMP
nsComputedDOMStyle::SetCssText(const nsAString& aCssText)
{
  return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
}


NS_IMETHODIMP
nsComputedDOMStyle::GetLength(uint32_t* aLength)
{
  NS_PRECONDITION(aLength, "Null aLength!  Prepare to die!");

  (void)GetQueryablePropertyMap(aLength);

  return NS_OK;
}


NS_IMETHODIMP
nsComputedDOMStyle::GetParentRule(nsIDOMCSSRule** aParentRule)
{
  *aParentRule = nullptr;

  return NS_OK;
}


NS_IMETHODIMP
nsComputedDOMStyle::GetPropertyValue(const nsAString& aPropertyName,
                                     nsAString& aReturn)
{
  nsCOMPtr<nsIDOMCSSValue> val;

  aReturn.Truncate();

  nsresult rv = GetPropertyCSSValue(aPropertyName, getter_AddRefs(val));
  NS_ENSURE_SUCCESS(rv, rv);

  if (val) {
    rv = val->GetCssText(aReturn);
  }

  return rv;
}

/* static */
already_AddRefed<nsStyleContext>
nsComputedDOMStyle::GetStyleContextForElement(Element* aElement,
                                              nsIAtom* aPseudo,
                                              nsIPresShell* aPresShell)
{
  // If the content has a pres shell, we must use it.  Otherwise we'd
  // potentially mix rule trees by using the wrong pres shell's style
  // set.  Using the pres shell from the content also means that any
  // content that's actually *in* a document will get the style from the
  // correct document.
  nsCOMPtr<nsIPresShell> presShell = GetPresShellForContent(aElement);
  if (!presShell) {
    presShell = aPresShell;
    if (!presShell)
      return nullptr;
  }

  presShell->FlushPendingNotifications(Flush_Style);

  return GetStyleContextForElementNoFlush(aElement, aPseudo, presShell);
}

/* static */
already_AddRefed<nsStyleContext>
nsComputedDOMStyle::GetStyleContextForElementNoFlush(Element* aElement,
                                                     nsIAtom* aPseudo,
                                                     nsIPresShell* aPresShell)
{
  NS_ABORT_IF_FALSE(aElement, "NULL element");
  // If the content has a pres shell, we must use it.  Otherwise we'd
  // potentially mix rule trees by using the wrong pres shell's style
  // set.  Using the pres shell from the content also means that any
  // content that's actually *in* a document will get the style from the
  // correct document.
  nsIPresShell *presShell = GetPresShellForContent(aElement);
  if (!presShell) {
    presShell = aPresShell;
    if (!presShell)
      return nullptr;
  }

  if (!aPseudo) {
    nsIFrame* frame = aElement->GetPrimaryFrame();
    if (frame) {
      nsStyleContext* result =
        nsLayoutUtils::GetStyleFrame(frame)->GetStyleContext();
      // Don't use the style context if it was influenced by
      // pseudo-elements, since then it's not the primary style
      // for this element.
      if (!result->HasPseudoElementData()) {
        // this function returns an addrefed style context
        result->AddRef();
        return result;
      }
    }
  }

  // No frame has been created or we have a pseudo, so resolve the
  // style ourselves
  nsRefPtr<nsStyleContext> parentContext;
  nsIContent* parent = aPseudo ? aElement : aElement->GetParent();
  // Don't resolve parent context for document fragments.
  if (parent && parent->IsElement())
    parentContext = GetStyleContextForElementNoFlush(parent->AsElement(),
                                                     nullptr, presShell);

  nsPresContext *presContext = presShell->GetPresContext();
  if (!presContext)
    return nullptr;

  nsStyleSet *styleSet = presShell->StyleSet();

  if (aPseudo) {
    nsCSSPseudoElements::Type type = nsCSSPseudoElements::GetPseudoType(aPseudo);
    if (type >= nsCSSPseudoElements::ePseudo_PseudoElementCount) {
      return nullptr;
    }
    return styleSet->ResolvePseudoElementStyle(aElement, type, parentContext);
  }

  return styleSet->ResolveStyleFor(aElement, parentContext);
}

/* static */
nsIPresShell*
nsComputedDOMStyle::GetPresShellForContent(nsIContent* aContent)
{
  nsIDocument* currentDoc = aContent->GetCurrentDoc();
  if (!currentDoc)
    return nullptr;

  return currentDoc->GetShell();
}

// nsDOMCSSDeclaration abstract methods which should never be called
// on a nsComputedDOMStyle object, but must be defined to avoid
// compile errors.
css::Declaration*
nsComputedDOMStyle::GetCSSDeclaration(bool)
{
  NS_RUNTIMEABORT("called nsComputedDOMStyle::GetCSSDeclaration");
  return nullptr;
}

nsresult
nsComputedDOMStyle::SetCSSDeclaration(css::Declaration*)
{
  NS_RUNTIMEABORT("called nsComputedDOMStyle::SetCSSDeclaration");
  return NS_ERROR_FAILURE;
}

nsIDocument*
nsComputedDOMStyle::DocToUpdate()
{
  NS_RUNTIMEABORT("called nsComputedDOMStyle::DocToUpdate");
  return nullptr;
}

void
nsComputedDOMStyle::GetCSSParsingEnvironment(CSSParsingEnvironment& aCSSParseEnv)
{
  NS_RUNTIMEABORT("called nsComputedDOMStyle::GetCSSParsingEnvironment");
  // Just in case NS_RUNTIMEABORT ever stops killing us for some reason
  aCSSParseEnv.mPrincipal = nullptr;
}

NS_IMETHODIMP
nsComputedDOMStyle::GetPropertyCSSValue(const nsAString& aPropertyName,
                                        nsIDOMCSSValue** aReturn)
{
  NS_ASSERTION(!mStyleContextHolder, "bad state");

  *aReturn = nullptr;

  nsCOMPtr<nsIDocument> document = do_QueryReferent(mDocumentWeak);
  NS_ENSURE_TRUE(document, NS_ERROR_NOT_AVAILABLE);
  document->FlushPendingLinkUpdates();

  nsCSSProperty prop = nsCSSProps::LookupProperty(aPropertyName,
                                                  nsCSSProps::eEnabled);

  // We don't (for now, anyway, though it may make sense to change it
  // for all aliases, including those in nsCSSPropAliasList) want
  // aliases to be enumerable (via GetLength and IndexedGetter), so
  // handle them here rather than adding entries to
  // GetQueryablePropertyMap.
  if (prop != eCSSProperty_UNKNOWN &&
      nsCSSProps::PropHasFlags(prop, CSS_PROPERTY_IS_ALIAS)) {
    const nsCSSProperty* subprops = nsCSSProps::SubpropertyEntryFor(prop);
    NS_ABORT_IF_FALSE(subprops[1] == eCSSProperty_UNKNOWN,
                      "must have list of length 1");
    prop = subprops[0];
  }

  const ComputedStyleMapEntry* propEntry = nullptr;
  {
    uint32_t length = 0;
    const ComputedStyleMapEntry* propMap = GetQueryablePropertyMap(&length);
    for (uint32_t i = 0; i < length; ++i) {
      if (prop == propMap[i].mProperty) {
        propEntry = &propMap[i];
        break;
      }
    }
  }
  if (!propEntry) {
#ifdef DEBUG_ComputedDOMStyle
    NS_WARNING(PromiseFlatCString(NS_ConvertUTF16toUTF8(aPropertyName) +
                                  NS_LITERAL_CSTRING(" is not queryable!")).get());
#endif

    // NOTE:  For branches, we should flush here for compatibility!
    return NS_OK;
  }

  // Flush _before_ getting the presshell, since that could create a new
  // presshell.  Also note that we want to flush the style on the document
  // we're computing style in, not on the document mContent is in -- the two
  // may be different.
  document->FlushPendingNotifications(
    propEntry->mNeedsLayoutFlush ? Flush_Layout : Flush_Style);
#ifdef DEBUG
  mFlushedPendingReflows = propEntry->mNeedsLayoutFlush;
#endif

  mPresShell = document->GetShell();
  NS_ENSURE_TRUE(mPresShell && mPresShell->GetPresContext(),
                 NS_ERROR_NOT_AVAILABLE);

  if (!mPseudo) {
    mOuterFrame = mContent->GetPrimaryFrame();
    mInnerFrame = mOuterFrame;
    if (mOuterFrame) {
      nsIAtom* type = mOuterFrame->GetType();
      if (type == nsGkAtoms::tableOuterFrame) {
        // If the frame is an outer table frame then we should get the style
        // from the inner table frame.
        mInnerFrame = mOuterFrame->GetFirstPrincipalChild();
        NS_ASSERTION(mInnerFrame, "Outer table must have an inner");
        NS_ASSERTION(!mInnerFrame->GetNextSibling(),
                     "Outer table frames should have just one child, "
                     "the inner table");
      }

      mStyleContextHolder = mInnerFrame->GetStyleContext();
      NS_ASSERTION(mStyleContextHolder, "Frame without style context?");
    }
  }

  if (!mStyleContextHolder || mStyleContextHolder->HasPseudoElementData()) {
#ifdef DEBUG
    if (mStyleContextHolder) {
      // We want to check that going through this path because of
      // HasPseudoElementData is rare, because it slows us down a good
      // bit.  So check that we're really inside something associated
      // with a pseudo-element that contains elements.
      nsStyleContext *topWithPseudoElementData = mStyleContextHolder;
      while (topWithPseudoElementData->GetParent()->HasPseudoElementData()) {
        topWithPseudoElementData = topWithPseudoElementData->GetParent();
      }
      NS_ASSERTION(nsCSSPseudoElements::PseudoElementContainsElements(
                     topWithPseudoElementData->GetPseudo()),
                   "we should be in a pseudo-element that is expected to "
                   "contain elements");
    }
#endif
    // Need to resolve a style context
    mStyleContextHolder =
      nsComputedDOMStyle::GetStyleContextForElement(mContent->AsElement(),
                                                    mPseudo,
                                                    mPresShell);
    NS_ENSURE_TRUE(mStyleContextHolder, NS_ERROR_OUT_OF_MEMORY);
    NS_ASSERTION(mPseudo || !mStyleContextHolder->HasPseudoElementData(),
                 "should not have pseudo-element data");
  }

  // mExposeVisitedStyle is set to true only by testing APIs that
  // require UniversalXPConnect.
  NS_ABORT_IF_FALSE(!mExposeVisitedStyle ||
                    nsContentUtils::CallerHasUniversalXPConnect(),
                    "mExposeVisitedStyle set incorrectly");
  if (mExposeVisitedStyle && mStyleContextHolder->RelevantLinkVisited()) {
    nsStyleContext *styleIfVisited = mStyleContextHolder->GetStyleIfVisited();
    if (styleIfVisited) {
      mStyleContextHolder = styleIfVisited;
    }
  }

  // Call our pointer-to-member-function.
  *aReturn = (this->*(propEntry->mGetter))();
  NS_IF_ADDREF(*aReturn); // property getter gives us an object with refcount of 0

  mOuterFrame = nullptr;
  mInnerFrame = nullptr;
  mPresShell = nullptr;

  // Release the current style context for it should be re-resolved
  // whenever a frame is not available.
  mStyleContextHolder = nullptr;

  return NS_OK;
}


NS_IMETHODIMP
nsComputedDOMStyle::RemoveProperty(const nsAString& aPropertyName,
                                   nsAString& aReturn)
{
  return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
}


NS_IMETHODIMP
nsComputedDOMStyle::GetPropertyPriority(const nsAString& aPropertyName,
                                        nsAString& aReturn)
{
  aReturn.Truncate();

  return NS_OK;
}


NS_IMETHODIMP
nsComputedDOMStyle::SetProperty(const nsAString& aPropertyName,
                                const nsAString& aValue,
                                const nsAString& aPriority)
{
  return NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR;
}


NS_IMETHODIMP
nsComputedDOMStyle::Item(uint32_t aIndex, nsAString& aReturn)
{
  return nsDOMCSSDeclaration::Item(aIndex, aReturn);
}

void
nsComputedDOMStyle::IndexedGetter(uint32_t aIndex, bool& aFound,
                                  nsAString& aPropName)
{
  uint32_t length = 0;
  const ComputedStyleMapEntry* propMap = GetQueryablePropertyMap(&length);
  aFound = aIndex < length;
  if (aFound) {
    CopyASCIItoUTF16(nsCSSProps::GetStringValue(propMap[aIndex].mProperty),
                     aPropName);
  }
}

// Property getters...

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBinding()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  const nsStyleDisplay* display = GetStyleDisplay();

  if (display->mBinding) {
    val->SetURI(display->mBinding->GetURI());
  } else {
    val->SetIdent(eCSSKeyword_none);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetClear()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(GetStyleDisplay()->mBreakType,
                                               nsCSSProps::kClearKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetCssFloat()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(GetStyleDisplay()->mFloats,
                                               nsCSSProps::kFloatKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBottom()
{
  return GetOffsetWidthFor(NS_SIDE_BOTTOM);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetStackSizing()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(GetStyleXUL()->mStretchStack ? eCSSKeyword_stretch_to_fit :
                eCSSKeyword_ignore);
  return val;
}

void
nsComputedDOMStyle::SetToRGBAColor(nsROCSSPrimitiveValue* aValue,
                                   nscolor aColor)
{
  if (NS_GET_A(aColor) == 0) {
    aValue->SetIdent(eCSSKeyword_transparent);
    return;
  }

  nsROCSSPrimitiveValue *red   = GetROCSSPrimitiveValue();
  nsROCSSPrimitiveValue *green = GetROCSSPrimitiveValue();
  nsROCSSPrimitiveValue *blue  = GetROCSSPrimitiveValue();
  nsROCSSPrimitiveValue *alpha  = GetROCSSPrimitiveValue();

  uint8_t a = NS_GET_A(aColor);
  nsDOMCSSRGBColor *rgbColor =
    new nsDOMCSSRGBColor(red, green, blue, alpha, a < 255);

  red->SetNumber(NS_GET_R(aColor));
  green->SetNumber(NS_GET_G(aColor));
  blue->SetNumber(NS_GET_B(aColor));
  alpha->SetNumber(nsStyleUtil::ColorComponentToFloat(a));

  aValue->SetColor(rgbColor);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetColor()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  SetToRGBAColor(val, GetStyleColor()->mColor);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOpacity()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStyleDisplay()->mOpacity);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetColumnCount()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  const nsStyleColumn* column = GetStyleColumn();

  if (column->mColumnCount == NS_STYLE_COLUMN_COUNT_AUTO) {
    val->SetIdent(eCSSKeyword_auto);
  } else {
    val->SetNumber(column->mColumnCount);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetColumnWidth()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  // XXX fix the auto case. When we actually have a column frame, I think
  // we should return the computed column width.
  SetValueToCoord(val, GetStyleColumn()->mColumnWidth, true);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetColumnGap()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  const nsStyleColumn* column = GetStyleColumn();
  if (column->mColumnGap.GetUnit() == eStyleUnit_Normal) {
    val->SetAppUnits(GetStyleFont()->mFont.size);
  } else {
    SetValueToCoord(val, GetStyleColumn()->mColumnGap, true);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetColumnRuleWidth()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetAppUnits(GetStyleColumn()->GetComputedColumnRuleWidth());
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetColumnRuleStyle()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleColumn()->mColumnRuleStyle,
                                   nsCSSProps::kBorderStyleKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetColumnRuleColor()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  const nsStyleColumn* column = GetStyleColumn();
  nscolor ruleColor;
  if (column->mColumnRuleColorIsForeground) {
    ruleColor = GetStyleColor()->mColor;
  } else {
    ruleColor = column->mColumnRuleColor;
  }

  SetToRGBAColor(val, ruleColor);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetContent()
{
  const nsStyleContent *content = GetStyleContent();

  if (content->ContentCount() == 0) {
    nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
    val->SetIdent(eCSSKeyword_none);
    return val;
  }

  if (content->ContentCount() == 1 &&
      content->ContentAt(0).mType == eStyleContentType_AltContent) {
    nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
    val->SetIdent(eCSSKeyword__moz_alt_content);
    return val;
  }

  nsDOMCSSValueList *valueList = GetROCSSValueList(false);

  for (uint32_t i = 0, i_end = content->ContentCount(); i < i_end; ++i) {
    nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(val);

    const nsStyleContentData &data = content->ContentAt(i);
    switch (data.mType) {
      case eStyleContentType_String:
        {
          nsString str;
          nsStyleUtil::AppendEscapedCSSString(
            nsDependentString(data.mContent.mString), str);
          val->SetString(str);
        }
        break;
      case eStyleContentType_Image:
        {
          nsCOMPtr<nsIURI> uri;
          if (data.mContent.mImage) {
            data.mContent.mImage->GetURI(getter_AddRefs(uri));
          }
          val->SetURI(uri);
        }
        break;
      case eStyleContentType_Attr:
        {
          nsAutoString str;
          nsStyleUtil::AppendEscapedCSSIdent(
            nsDependentString(data.mContent.mString), str);
          val->SetString(str, nsIDOMCSSPrimitiveValue::CSS_ATTR);
        }
        break;
      case eStyleContentType_Counter:
      case eStyleContentType_Counters:
        {
          /* FIXME: counters should really use an object */
          nsAutoString str;
          if (data.mType == eStyleContentType_Counter) {
            str.AppendLiteral("counter(");
          }
          else {
            str.AppendLiteral("counters(");
          }
          // WRITE ME
          nsCSSValue::Array *a = data.mContent.mCounters;

          nsStyleUtil::AppendEscapedCSSIdent(
            nsDependentString(a->Item(0).GetStringBufferValue()), str);
          int32_t typeItem = 1;
          if (data.mType == eStyleContentType_Counters) {
            typeItem = 2;
            str.AppendLiteral(", ");
            nsStyleUtil::AppendEscapedCSSString(
              nsDependentString(a->Item(1).GetStringBufferValue()), str);
          }
          NS_ABORT_IF_FALSE(eCSSUnit_None != a->Item(typeItem).GetUnit(),
                            "'none' should be handled  as enumerated value");
          int32_t type = a->Item(typeItem).GetIntValue();
          if (type != NS_STYLE_LIST_STYLE_DECIMAL) {
            str.AppendLiteral(", ");
            AppendASCIItoUTF16(
              nsCSSProps::ValueToKeyword(type, nsCSSProps::kListStyleKTable),
              str);
          }

          str.Append(PRUnichar(')'));
          val->SetString(str, nsIDOMCSSPrimitiveValue::CSS_COUNTER);
        }
        break;
      case eStyleContentType_OpenQuote:
        val->SetIdent(eCSSKeyword_open_quote);
        break;
      case eStyleContentType_CloseQuote:
        val->SetIdent(eCSSKeyword_close_quote);
        break;
      case eStyleContentType_NoOpenQuote:
        val->SetIdent(eCSSKeyword_no_open_quote);
        break;
      case eStyleContentType_NoCloseQuote:
        val->SetIdent(eCSSKeyword_no_close_quote);
        break;
      case eStyleContentType_AltContent:
      default:
        NS_NOTREACHED("unexpected type");
        break;
    }
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetCounterIncrement()
{
  const nsStyleContent *content = GetStyleContent();

  if (content->CounterIncrementCount() == 0) {
    nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
    val->SetIdent(eCSSKeyword_none);
    return val;
  }

  nsDOMCSSValueList *valueList = GetROCSSValueList(false);

  for (uint32_t i = 0, i_end = content->CounterIncrementCount(); i < i_end; ++i) {
    nsROCSSPrimitiveValue* name = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(name);

    nsROCSSPrimitiveValue* value = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(value);

    const nsStyleCounterData *data = content->GetCounterIncrementAt(i);
    nsAutoString escaped;
    nsStyleUtil::AppendEscapedCSSIdent(data->mCounter, escaped);
    name->SetString(escaped);
    value->SetNumber(data->mValue); // XXX This should really be integer
  }

  return valueList;
}

/* Convert the stored representation into a list of two values and then hand
 * it back.
 */
nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTransformOrigin()
{
  /* We need to build up a list of two values.  We'll call them
   * width and height.
   */

  /* Store things as a value list */
  nsDOMCSSValueList* valueList = GetROCSSValueList(false);

  /* Now, get the values. */
  const nsStyleDisplay* display = GetStyleDisplay();

  nsROCSSPrimitiveValue* width = GetROCSSPrimitiveValue();
  SetValueToCoord(width, display->mTransformOrigin[0], false,
                  &nsComputedDOMStyle::GetFrameBoundsWidthForTransform);
  valueList->AppendCSSValue(width);

  nsROCSSPrimitiveValue* height = GetROCSSPrimitiveValue();
  SetValueToCoord(height, display->mTransformOrigin[1], false,
                  &nsComputedDOMStyle::GetFrameBoundsHeightForTransform);
  valueList->AppendCSSValue(height);

  if (display->mTransformOrigin[2].GetUnit() != eStyleUnit_Coord ||
      display->mTransformOrigin[2].GetCoordValue() != 0) {
    nsROCSSPrimitiveValue* depth = GetROCSSPrimitiveValue();
    SetValueToCoord(depth, display->mTransformOrigin[2], false,
                    nullptr);
    valueList->AppendCSSValue(depth);
  }

  return valueList;
}

/* Convert the stored representation into a list of two values and then hand
 * it back.
 */
nsIDOMCSSValue*
nsComputedDOMStyle::DoGetPerspectiveOrigin()
{
  /* We need to build up a list of two values.  We'll call them
   * width and height.
   */

  /* Store things as a value list */
  nsDOMCSSValueList* valueList = GetROCSSValueList(false);

  /* Now, get the values. */
  const nsStyleDisplay* display = GetStyleDisplay();

  nsROCSSPrimitiveValue* width = GetROCSSPrimitiveValue();
  SetValueToCoord(width, display->mPerspectiveOrigin[0], false,
                  &nsComputedDOMStyle::GetFrameBoundsWidthForTransform);
  valueList->AppendCSSValue(width);

  nsROCSSPrimitiveValue* height = GetROCSSPrimitiveValue();
  SetValueToCoord(height, display->mPerspectiveOrigin[1], false,
                  &nsComputedDOMStyle::GetFrameBoundsHeightForTransform);
  valueList->AppendCSSValue(height);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetPerspective()
{
    nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
    if (GetStyleDisplay()->mChildPerspective.GetUnit() == eStyleUnit_Coord &&
        GetStyleDisplay()->mChildPerspective.GetCoordValue() == 0.0) {
        val->SetIdent(eCSSKeyword_none);
    } else {
        SetValueToCoord(val, GetStyleDisplay()->mChildPerspective, false);
    }
    return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBackfaceVisibility()
{
    nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
    val->SetIdent(
        nsCSSProps::ValueToKeywordEnum(GetStyleDisplay()->mBackfaceVisibility,
                                       nsCSSProps::kBackfaceVisibilityKTable));
    return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTransformStyle()
{
    nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
    val->SetIdent(
        nsCSSProps::ValueToKeywordEnum(GetStyleDisplay()->mTransformStyle,
                                       nsCSSProps::kTransformStyleKTable));
    return val;
}

/* If the property is "none", hand back "none" wrapped in a value.
 * Otherwise, compute the aggregate transform matrix and hands it back in a
 * "matrix" wrapper.
 */
nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTransform()
{
  /* First, get the display data.  We'll need it. */
  const nsStyleDisplay* display = GetStyleDisplay();

  /* If there are no transforms, then we should construct a single-element
   * entry and hand it back.
   */
  if (!display->mSpecifiedTransform) {
    nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

    /* Set it to "none." */
    val->SetIdent(eCSSKeyword_none);
    return val;
  }

  /* Otherwise, we need to compute the current value of the transform matrix,
   * store it in a string, and hand it back to the caller.
   */

  /* Use the inner frame for width and height.  If we fail, assume zero.
   * TODO: There is no good way for us to represent the case where there's no
   * frame, which is problematic.  The reason is that when we have percentage
   * transforms, there are a total of four stored matrix entries that influence
   * the transform based on the size of the element.  However, this poses a
   * problem, because only two of these values can be explicitly referenced
   * using the named transforms.  Until a real solution is found, we'll just
   * use this approach.
   */
  nsRect bounds =
    (mInnerFrame ? nsDisplayTransform::GetFrameBoundsForTransform(mInnerFrame) :
     nsRect(0, 0, 0, 0));

   bool dummy;
   gfx3DMatrix matrix =
     nsStyleTransformMatrix::ReadTransforms(display->mSpecifiedTransform,
                                            mStyleContextHolder,
                                            mStyleContextHolder->PresContext(),
                                            dummy,
                                            bounds,
                                            float(nsDeviceContext::AppUnitsPerCSSPixel()));

  bool is3D = !matrix.Is2D();

  nsAutoString resultString(NS_LITERAL_STRING("matrix"));
  if (is3D) {
    resultString.Append(NS_LITERAL_STRING("3d"));
  }

  resultString.Append(NS_LITERAL_STRING("("));
  resultString.AppendFloat(matrix._11);
  resultString.Append(NS_LITERAL_STRING(", "));
  resultString.AppendFloat(matrix._12);
  resultString.Append(NS_LITERAL_STRING(", "));
  if (is3D) {
    resultString.AppendFloat(matrix._13);
    resultString.Append(NS_LITERAL_STRING(", "));
    resultString.AppendFloat(matrix._14);
    resultString.Append(NS_LITERAL_STRING(", "));
  }
  resultString.AppendFloat(matrix._21);
  resultString.Append(NS_LITERAL_STRING(", "));
  resultString.AppendFloat(matrix._22);
  resultString.Append(NS_LITERAL_STRING(", "));
  if (is3D) {
    resultString.AppendFloat(matrix._23);
    resultString.Append(NS_LITERAL_STRING(", "));
    resultString.AppendFloat(matrix._24);
    resultString.Append(NS_LITERAL_STRING(", "));
    resultString.AppendFloat(matrix._31);
    resultString.Append(NS_LITERAL_STRING(", "));
    resultString.AppendFloat(matrix._32);
    resultString.Append(NS_LITERAL_STRING(", "));
    resultString.AppendFloat(matrix._33);
    resultString.Append(NS_LITERAL_STRING(", "));
    resultString.AppendFloat(matrix._34);
    resultString.Append(NS_LITERAL_STRING(", "));
  }
  resultString.AppendFloat(matrix._41);
  resultString.Append(NS_LITERAL_STRING(", "));
  resultString.AppendFloat(matrix._42);
  if (is3D) {
    resultString.Append(NS_LITERAL_STRING(", "));
    resultString.AppendFloat(matrix._43);
    resultString.Append(NS_LITERAL_STRING(", "));
    resultString.AppendFloat(matrix._44);
  }
  resultString.Append(NS_LITERAL_STRING(")"));

  /* Create a value to hold our result. */
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  val->SetString(resultString);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetCounterReset()
{
  const nsStyleContent *content = GetStyleContent();

  if (content->CounterResetCount() == 0) {
    nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
    val->SetIdent(eCSSKeyword_none);
    return val;
  }

  nsDOMCSSValueList *valueList = GetROCSSValueList(false);

  for (uint32_t i = 0, i_end = content->CounterResetCount(); i < i_end; ++i) {
    nsROCSSPrimitiveValue* name = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(name);

    nsROCSSPrimitiveValue* value = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(value);

    const nsStyleCounterData *data = content->GetCounterResetAt(i);
    nsAutoString escaped;
    nsStyleUtil::AppendEscapedCSSIdent(data->mCounter, escaped);
    name->SetString(escaped);
    value->SetNumber(data->mValue); // XXX This should really be integer
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetQuotes()
{
  const nsStyleQuotes *quotes = GetStyleQuotes();

  if (quotes->QuotesCount() == 0) {
    nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
    val->SetIdent(eCSSKeyword_none);
    return val;
  }

  nsDOMCSSValueList *valueList = GetROCSSValueList(false);

  for (uint32_t i = 0, i_end = quotes->QuotesCount(); i < i_end; ++i) {
    nsROCSSPrimitiveValue* openVal = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(openVal);

    nsROCSSPrimitiveValue* closeVal = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(closeVal);

    nsString s;
    nsStyleUtil::AppendEscapedCSSString(*quotes->OpenQuoteAt(i), s);
    openVal->SetString(s);
    s.Truncate();
    nsStyleUtil::AppendEscapedCSSString(*quotes->CloseQuoteAt(i), s);
    closeVal->SetString(s);
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFontFamily()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleFont* font = GetStyleFont();

  nsCOMPtr<nsIDocument> doc = do_QueryReferent(mDocumentWeak);
  NS_ASSERTION(doc, "document is required");
  nsIPresShell* presShell = doc->GetShell();
  NS_ASSERTION(presShell, "pres shell is required");
  nsPresContext *presContext = presShell->GetPresContext();
  NS_ASSERTION(presContext, "pres context is required");

  const nsString& fontName = font->mFont.name;
  if (font->mGenericID == kGenericFont_NONE && !font->mFont.systemFont) {
    const nsFont* defaultFont =
      presContext->GetDefaultFont(kPresContext_DefaultVariableFont_ID,
                                  font->mLanguage);

    int32_t lendiff = fontName.Length() - defaultFont->name.Length();
    if (lendiff > 0) {
      val->SetString(Substring(fontName, 0, lendiff-1)); // -1 removes comma
    } else {
      val->SetString(fontName);
    }
  } else {
    val->SetString(fontName);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFontSize()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  // Note: GetStyleFont()->mSize is the 'computed size';
  // GetStyleFont()->mFont.size is the 'actual size'
  val->SetAppUnits(GetStyleFont()->mSize);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFontSizeAdjust()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  const nsStyleFont *font = GetStyleFont();

  if (font->mFont.sizeAdjust) {
    val->SetNumber(font->mFont.sizeAdjust);
  } else {
    val->SetIdent(eCSSKeyword_none);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFontStretch()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  val->SetIdent(nsCSSProps::ValueToKeywordEnum(GetStyleFont()->mFont.stretch,
                                               nsCSSProps::kFontStretchKTable));

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFontStyle()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(GetStyleFont()->mFont.style,
                                               nsCSSProps::kFontStyleKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFontWeight()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleFont* font = GetStyleFont();

  uint16_t weight = font->mFont.weight;
  if (weight % 100 == 0) {
    val->SetNumber(font->mFont.weight);
  } else if (weight % 100 > 50) {
    // FIXME: This doesn't represent the full range of computed values,
    // but at least it's legal CSS.
    val->SetIdent(eCSSKeyword_lighter);
  } else {
    // FIXME: This doesn't represent the full range of computed values,
    // but at least it's legal CSS.
    val->SetIdent(eCSSKeyword_bolder);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFontVariant()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleFont()->mFont.variant,
                                   nsCSSProps::kFontVariantKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFontFeatureSettings()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleFont* font = GetStyleFont();
  if (font->mFont.fontFeatureSettings.IsEmpty()) {
    val->SetIdent(eCSSKeyword_normal);
  } else {
    nsAutoString result;
    nsStyleUtil::AppendFontFeatureSettings(font->mFont.fontFeatureSettings,
                                           result);
    val->SetString(result);
  }
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFontLanguageOverride()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleFont* font = GetStyleFont();
  if (font->mFont.languageOverride.IsEmpty()) {
    val->SetIdent(eCSSKeyword_normal);
  } else {
    nsString str;
    nsStyleUtil::AppendEscapedCSSString(font->mFont.languageOverride, str);
    val->SetString(str);
  }
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetBackgroundList(uint8_t nsStyleBackground::Layer::* aMember,
                                      uint32_t nsStyleBackground::* aCount,
                                      const int32_t aTable[])
{
  const nsStyleBackground* bg = GetStyleBackground();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  for (uint32_t i = 0, i_end = bg->*aCount; i < i_end; ++i) {
    nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(val);
    val->SetIdent(nsCSSProps::ValueToKeywordEnum(bg->mLayers[i].*aMember,
                                                 aTable));
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBackgroundAttachment()
{
  return GetBackgroundList(&nsStyleBackground::Layer::mAttachment,
                           &nsStyleBackground::mAttachmentCount,
                           nsCSSProps::kBackgroundAttachmentKTable);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBackgroundClip()
{
  return GetBackgroundList(&nsStyleBackground::Layer::mClip,
                           &nsStyleBackground::mClipCount,
                           nsCSSProps::kBackgroundOriginKTable);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBackgroundColor()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  SetToRGBAColor(val, GetStyleBackground()->mBackgroundColor);
  return val;
}


static void
SetValueToCalc(const nsStyleCoord::Calc *aCalc, nsROCSSPrimitiveValue *aValue)
{
  nsRefPtr<nsROCSSPrimitiveValue> val = new nsROCSSPrimitiveValue();
  nsAutoString tmp, result;

  result.AppendLiteral("calc(");

  val->SetAppUnits(aCalc->mLength);
  val->GetCssText(tmp);
  result.Append(tmp);

  if (aCalc->mHasPercent) {
    result.AppendLiteral(" + ");

    val->SetPercent(aCalc->mPercent);
    val->GetCssText(tmp);
    result.Append(tmp);
  }

  result.AppendLiteral(")");

  aValue->SetString(result); // not really SetString
}

static void
AppendCSSGradientLength(const nsStyleCoord& aValue,
                        nsROCSSPrimitiveValue* aPrimitive,
                        nsAString& aString)
{
  nsAutoString tokenString;
  if (aValue.IsCalcUnit())
    SetValueToCalc(aValue.GetCalcValue(), aPrimitive);
  else if (aValue.GetUnit() == eStyleUnit_Coord)
    aPrimitive->SetAppUnits(aValue.GetCoordValue());
  else
    aPrimitive->SetPercent(aValue.GetPercentValue());
  aPrimitive->GetCssText(tokenString);
  aString.Append(tokenString);
}

static void
AppendCSSGradientToBoxPosition(const nsStyleGradient* aGradient,
                               nsAString& aString,
                               bool& aNeedSep)
{
  float xValue = aGradient->mBgPosX.GetPercentValue();
  float yValue = aGradient->mBgPosY.GetPercentValue();

  if (yValue == 1.0f && xValue == 0.5f) {
    // omit "to bottom"
    return;
  }
  NS_ASSERTION(yValue != 0.5f || xValue != 0.5f, "invalid box position");

  aString.AppendLiteral("to");

  if (yValue == 0.0f) {
    aString.AppendLiteral(" top");
  } else if (yValue == 1.0f) {
    aString.AppendLiteral(" bottom");
  } else if (yValue != 0.5f) { // do not write "center" keyword
    NS_NOTREACHED("invalid box position");
  }

  if (xValue == 0.0f) {
    aString.AppendLiteral(" left");
  } else if (xValue == 1.0f) {
    aString.AppendLiteral(" right");
  } else if (xValue != 0.5f) { // do not write "center" keyword
    NS_NOTREACHED("invalid box position");
  }

  aNeedSep = true;
}

void
nsComputedDOMStyle::GetCSSGradientString(const nsStyleGradient* aGradient,
                                         nsAString& aString)
{
  if (!aGradient->mLegacySyntax) {
    aString.Truncate();
  } else {
    aString.AssignLiteral("-moz-");
  }
  if (aGradient->mRepeating) {
    aString.AppendLiteral("repeating-");
  }
  bool isRadial = aGradient->mShape != NS_STYLE_GRADIENT_SHAPE_LINEAR;
  if (isRadial) {
    aString.AppendLiteral("radial-gradient(");
  } else {
    aString.AppendLiteral("linear-gradient(");
  }

  bool needSep = false;
  nsAutoString tokenString;
  nsROCSSPrimitiveValue *tmpVal = GetROCSSPrimitiveValue();

  if (isRadial && !aGradient->mLegacySyntax) {
    if (aGradient->mSize != NS_STYLE_GRADIENT_SIZE_EXPLICIT_SIZE) {
      if (aGradient->mShape == NS_STYLE_GRADIENT_SHAPE_CIRCULAR) {
        aString.AppendLiteral("circle");
        needSep = true;
      }
      if (aGradient->mSize != NS_STYLE_GRADIENT_SIZE_FARTHEST_CORNER) {
        if (needSep) {
          aString.AppendLiteral(" ");
        }
        AppendASCIItoUTF16(nsCSSProps::
                           ValueToKeyword(aGradient->mSize,
                                          nsCSSProps::kRadialGradientSizeKTable),
                           aString);
        needSep = true;
      }
    } else {
      AppendCSSGradientLength(aGradient->mRadiusX, tmpVal, aString);
      if (aGradient->mShape != NS_STYLE_GRADIENT_SHAPE_CIRCULAR) {
        aString.AppendLiteral(" ");
        AppendCSSGradientLength(aGradient->mRadiusY, tmpVal, aString);
      }
      needSep = true;
    }
  }
  if (aGradient->mBgPosX.GetUnit() != eStyleUnit_None) {
    MOZ_ASSERT(aGradient->mBgPosY.GetUnit() != eStyleUnit_None);
    if (!isRadial && !aGradient->mLegacySyntax) {
      AppendCSSGradientToBoxPosition(aGradient, aString, needSep);
    } else if (aGradient->mBgPosX.GetUnit() != eStyleUnit_Percent ||
               aGradient->mBgPosX.GetPercentValue() != 0.5f ||
               aGradient->mBgPosY.GetUnit() != eStyleUnit_Percent ||
               aGradient->mBgPosY.GetPercentValue() != (isRadial ? 0.5f : 1.0f)) {
      if (isRadial && !aGradient->mLegacySyntax) {
        if (needSep) {
          aString.AppendLiteral(" ");
        }
        aString.AppendLiteral("at ");
        needSep = false;
      }
      AppendCSSGradientLength(aGradient->mBgPosX, tmpVal, aString);
      if (aGradient->mBgPosY.GetUnit() != eStyleUnit_None) {
        aString.AppendLiteral(" ");
        AppendCSSGradientLength(aGradient->mBgPosY, tmpVal, aString);
      }
      needSep = true;
    }
  }
  if (aGradient->mAngle.GetUnit() != eStyleUnit_None) {
    MOZ_ASSERT(!isRadial || aGradient->mLegacySyntax);
    if (needSep) {
      aString.AppendLiteral(" ");
    }
    tmpVal->SetNumber(aGradient->mAngle.GetAngleValue());
    tmpVal->GetCssText(tokenString);
    aString.Append(tokenString);
    switch (aGradient->mAngle.GetUnit()) {
    case eStyleUnit_Degree: aString.AppendLiteral("deg"); break;
    case eStyleUnit_Grad: aString.AppendLiteral("grad"); break;
    case eStyleUnit_Radian: aString.AppendLiteral("rad"); break;
    case eStyleUnit_Turn: aString.AppendLiteral("turn"); break;
    default: NS_NOTREACHED("unrecognized angle unit");
    }
    needSep = true;
  }

  if (isRadial && aGradient->mLegacySyntax &&
      (aGradient->mShape == NS_STYLE_GRADIENT_SHAPE_CIRCULAR ||
       aGradient->mSize != NS_STYLE_GRADIENT_SIZE_FARTHEST_CORNER)) {
    MOZ_ASSERT(aGradient->mSize != NS_STYLE_GRADIENT_SIZE_EXPLICIT_SIZE);
    if (needSep) {
      aString.AppendLiteral(", ");
      needSep = false;
    }
    if (aGradient->mShape == NS_STYLE_GRADIENT_SHAPE_CIRCULAR) {
      aString.AppendLiteral("circle");
      needSep = true;
    }
    if (aGradient->mSize != NS_STYLE_GRADIENT_SIZE_FARTHEST_CORNER) {
      if (needSep) {
        aString.AppendLiteral(" ");
      }
      AppendASCIItoUTF16(nsCSSProps::
                         ValueToKeyword(aGradient->mSize,
                                        nsCSSProps::kRadialGradientSizeKTable),
                         aString);
    }
    needSep = true;
  }


  // color stops
  for (uint32_t i = 0; i < aGradient->mStops.Length(); ++i) {
    if (needSep) {
      aString.AppendLiteral(", ");
    }
    SetToRGBAColor(tmpVal, aGradient->mStops[i].mColor);
    tmpVal->GetCssText(tokenString);
    aString.Append(tokenString);

    if (aGradient->mStops[i].mLocation.GetUnit() != eStyleUnit_None) {
      aString.AppendLiteral(" ");
      AppendCSSGradientLength(aGradient->mStops[i].mLocation, tmpVal, aString);
    }
    needSep = true;
  }

  delete tmpVal;
  aString.AppendLiteral(")");
}

// -moz-image-rect(<uri>, <top>, <right>, <bottom>, <left>)
void
nsComputedDOMStyle::GetImageRectString(nsIURI* aURI,
                                       const nsStyleSides& aCropRect,
                                       nsString& aString)
{
  nsDOMCSSValueList* valueList = GetROCSSValueList(true);

  // <uri>
  nsROCSSPrimitiveValue *valURI = GetROCSSPrimitiveValue();
  valueList->AppendCSSValue(valURI);
  valURI->SetURI(aURI);

  // <top>, <right>, <bottom>, <left>
  NS_FOR_CSS_SIDES(side) {
    nsROCSSPrimitiveValue *valSide = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(valSide);
    SetValueToCoord(valSide, aCropRect.Get(side), false);
  }

  nsAutoString argumentString;
  valueList->GetCssText(argumentString);
  delete valueList;

  aString = NS_LITERAL_STRING("-moz-image-rect(") +
            argumentString +
            NS_LITERAL_STRING(")");
}

void
nsComputedDOMStyle::SetValueToStyleImage(const nsStyleImage& aStyleImage,
                                         nsROCSSPrimitiveValue* aValue)
{
  switch (aStyleImage.GetType()) {
    case eStyleImageType_Image:
    {
      imgIRequest *req = aStyleImage.GetImageData();
      nsCOMPtr<nsIURI> uri;
      req->GetURI(getter_AddRefs(uri));

      const nsStyleSides* cropRect = aStyleImage.GetCropRect();
      if (cropRect) {
        nsAutoString imageRectString;
        GetImageRectString(uri, *cropRect, imageRectString);
        aValue->SetString(imageRectString);
      } else {
        aValue->SetURI(uri);
      }
      break;
    }
    case eStyleImageType_Gradient:
    {
      nsAutoString gradientString;
      GetCSSGradientString(aStyleImage.GetGradientData(),
                           gradientString);
      aValue->SetString(gradientString);
      break;
    }
    case eStyleImageType_Element:
    {
      nsAutoString elementId;
      nsStyleUtil::AppendEscapedCSSIdent(
        nsDependentString(aStyleImage.GetElementId()), elementId);
      nsAutoString elementString = NS_LITERAL_STRING("-moz-element(#") +
                                   elementId +
                                   NS_LITERAL_STRING(")");
      aValue->SetString(elementString);
      break;
    }
    case eStyleImageType_Null:
      aValue->SetIdent(eCSSKeyword_none);
      break;
    default:
      NS_NOTREACHED("unexpected image type");
      break;
  }
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBackgroundImage()
{
  const nsStyleBackground* bg = GetStyleBackground();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  for (uint32_t i = 0, i_end = bg->mImageCount; i < i_end; ++i) {
    nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(val);

    const nsStyleImage& image = bg->mLayers[i].mImage;
    SetValueToStyleImage(image, val);
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBackgroundInlinePolicy()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(
                  GetStyleBackground()->mBackgroundInlinePolicy,
                  nsCSSProps::kBackgroundInlinePolicyKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBackgroundOrigin()
{
  return GetBackgroundList(&nsStyleBackground::Layer::mOrigin,
                           &nsStyleBackground::mOriginCount,
                           nsCSSProps::kBackgroundOriginKTable);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBackgroundPosition()
{
  const nsStyleBackground* bg = GetStyleBackground();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  for (uint32_t i = 0, i_end = bg->mPositionCount; i < i_end; ++i) {
    nsDOMCSSValueList *itemList = GetROCSSValueList(false);
    valueList->AppendCSSValue(itemList);

    nsROCSSPrimitiveValue *valX = GetROCSSPrimitiveValue();
    itemList->AppendCSSValue(valX);

    nsROCSSPrimitiveValue *valY = GetROCSSPrimitiveValue();
    itemList->AppendCSSValue(valY);

    const nsStyleBackground::Position &pos = bg->mLayers[i].mPosition;

    if (!pos.mXPosition.mHasPercent) {
      NS_ABORT_IF_FALSE(pos.mXPosition.mPercent == 0.0f,
                        "Shouldn't have mPercent!");
      valX->SetAppUnits(pos.mXPosition.mLength);
    } else if (pos.mXPosition.mLength == 0) {
      valX->SetPercent(pos.mXPosition.mPercent);
    } else {
      SetValueToCalc(&pos.mXPosition, valX);
    }

    if (!pos.mYPosition.mHasPercent) {
      NS_ABORT_IF_FALSE(pos.mYPosition.mPercent == 0.0f,
                        "Shouldn't have mPercent!");
      valY->SetAppUnits(pos.mYPosition.mLength);
    } else if (pos.mYPosition.mLength == 0) {
      valY->SetPercent(pos.mYPosition.mPercent);
    } else {
      SetValueToCalc(&pos.mYPosition, valY);
    }
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBackgroundRepeat()
{
  const nsStyleBackground* bg = GetStyleBackground();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  for (uint32_t i = 0, i_end = bg->mRepeatCount; i < i_end; ++i) {
    nsDOMCSSValueList *itemList = GetROCSSValueList(false);
    valueList->AppendCSSValue(itemList);

    nsROCSSPrimitiveValue *valX = GetROCSSPrimitiveValue();
    itemList->AppendCSSValue(valX);

    const uint8_t& xRepeat = bg->mLayers[i].mRepeat.mXRepeat;
    const uint8_t& yRepeat = bg->mLayers[i].mRepeat.mYRepeat;

    bool hasContraction = true;
    unsigned contraction;
    if (xRepeat == yRepeat) {
      contraction = xRepeat;
    } else if (xRepeat == NS_STYLE_BG_REPEAT_REPEAT &&
               yRepeat == NS_STYLE_BG_REPEAT_NO_REPEAT) {
      contraction = NS_STYLE_BG_REPEAT_REPEAT_X;
    } else if (xRepeat == NS_STYLE_BG_REPEAT_NO_REPEAT &&
               yRepeat == NS_STYLE_BG_REPEAT_REPEAT) {
      contraction = NS_STYLE_BG_REPEAT_REPEAT_Y;
    } else {
      hasContraction = false;
    }

    if (hasContraction) {
      valX->SetIdent(nsCSSProps::ValueToKeywordEnum(contraction,
                                         nsCSSProps::kBackgroundRepeatKTable));
    } else {
      nsROCSSPrimitiveValue *valY = GetROCSSPrimitiveValue();
      itemList->AppendCSSValue(valY);
      
      valX->SetIdent(nsCSSProps::ValueToKeywordEnum(xRepeat,
                                          nsCSSProps::kBackgroundRepeatKTable));
      valY->SetIdent(nsCSSProps::ValueToKeywordEnum(yRepeat,
                                          nsCSSProps::kBackgroundRepeatKTable));
    }
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBackgroundSize()
{
  const nsStyleBackground* bg = GetStyleBackground();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  for (uint32_t i = 0, i_end = bg->mSizeCount; i < i_end; ++i) {
    const nsStyleBackground::Size &size = bg->mLayers[i].mSize;

    switch (size.mWidthType) {
      case nsStyleBackground::Size::eContain:
      case nsStyleBackground::Size::eCover: {
        NS_ABORT_IF_FALSE(size.mWidthType == size.mHeightType,
                          "unsynced types");
        nsCSSKeyword keyword = size.mWidthType == nsStyleBackground::Size::eContain
                             ? eCSSKeyword_contain
                             : eCSSKeyword_cover;
        nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
        valueList->AppendCSSValue(val);
        val->SetIdent(keyword);
        break;
      }
      default: {
        nsDOMCSSValueList *itemList = GetROCSSValueList(false);
        valueList->AppendCSSValue(itemList);

        nsROCSSPrimitiveValue* valX = GetROCSSPrimitiveValue();
        itemList->AppendCSSValue(valX);
        nsROCSSPrimitiveValue* valY = GetROCSSPrimitiveValue();
        itemList->AppendCSSValue(valY);

        if (size.mWidthType == nsStyleBackground::Size::eAuto) {
          valX->SetIdent(eCSSKeyword_auto);
        } else {
          NS_ABORT_IF_FALSE(size.mWidthType ==
                              nsStyleBackground::Size::eLengthPercentage,
                            "bad mWidthType");
          if (!size.mWidth.mHasPercent &&
              // negative values must have come from calc()
              size.mWidth.mLength >= 0) {
            NS_ABORT_IF_FALSE(size.mWidth.mPercent == 0.0f,
                              "Shouldn't have mPercent");
            valX->SetAppUnits(size.mWidth.mLength);
          } else if (size.mWidth.mLength == 0 &&
                     // negative values must have come from calc()
                     size.mWidth.mPercent >= 0.0f) {
            valX->SetPercent(size.mWidth.mPercent);
          } else {
            SetValueToCalc(&size.mWidth, valX);
          }
        }

        if (size.mHeightType == nsStyleBackground::Size::eAuto) {
          valY->SetIdent(eCSSKeyword_auto);
        } else {
          NS_ABORT_IF_FALSE(size.mHeightType ==
                              nsStyleBackground::Size::eLengthPercentage,
                            "bad mHeightType");
          if (!size.mHeight.mHasPercent &&
              // negative values must have come from calc()
              size.mHeight.mLength >= 0) {
            NS_ABORT_IF_FALSE(size.mHeight.mPercent == 0.0f,
                              "Shouldn't have mPercent");
            valY->SetAppUnits(size.mHeight.mLength);
          } else if (size.mHeight.mLength == 0 &&
                     // negative values must have come from calc()
                     size.mHeight.mPercent >= 0.0f) {
            valY->SetPercent(size.mHeight.mPercent);
          } else {
            SetValueToCalc(&size.mHeight, valY);
          }
        }
        break;
      }
    }
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetPaddingTop()
{
  return GetPaddingWidthFor(NS_SIDE_TOP);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetPaddingBottom()
{
  return GetPaddingWidthFor(NS_SIDE_BOTTOM);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetPaddingLeft()
{
  return GetPaddingWidthFor(NS_SIDE_LEFT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetPaddingRight()
{
  return GetPaddingWidthFor(NS_SIDE_RIGHT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderCollapse()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleTableBorder()->mBorderCollapse,
                                   nsCSSProps::kBorderCollapseKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderSpacing()
{
  nsDOMCSSValueList *valueList = GetROCSSValueList(false);

  nsROCSSPrimitiveValue* xSpacing = GetROCSSPrimitiveValue();
  valueList->AppendCSSValue(xSpacing);

  nsROCSSPrimitiveValue* ySpacing = GetROCSSPrimitiveValue();
  valueList->AppendCSSValue(ySpacing);

  const nsStyleTableBorder *border = GetStyleTableBorder();
  xSpacing->SetAppUnits(border->mBorderSpacingX);
  ySpacing->SetAppUnits(border->mBorderSpacingY);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetCaptionSide()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleTableBorder()->mCaptionSide,
                                   nsCSSProps::kCaptionSideKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetEmptyCells()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleTableBorder()->mEmptyCells,
                                   nsCSSProps::kEmptyCellsKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTableLayout()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleTable()->mLayoutStrategy,
                                   nsCSSProps::kTableLayoutKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderTopStyle()
{
  return GetBorderStyleFor(NS_SIDE_TOP);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderBottomStyle()
{
  return GetBorderStyleFor(NS_SIDE_BOTTOM);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderLeftStyle()
{
  return GetBorderStyleFor(NS_SIDE_LEFT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderRightStyle()
{
  return GetBorderStyleFor(NS_SIDE_RIGHT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderBottomColors()
{
  return GetBorderColorsFor(NS_SIDE_BOTTOM);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderLeftColors()
{
  return GetBorderColorsFor(NS_SIDE_LEFT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderRightColors()
{
  return GetBorderColorsFor(NS_SIDE_RIGHT);
}


nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderTopColors()
{
  return GetBorderColorsFor(NS_SIDE_TOP);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderBottomLeftRadius()
{
  return GetEllipseRadii(GetStyleBorder()->mBorderRadius,
                         NS_CORNER_BOTTOM_LEFT, true);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderBottomRightRadius()
{
  return GetEllipseRadii(GetStyleBorder()->mBorderRadius,
                         NS_CORNER_BOTTOM_RIGHT, true);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderTopLeftRadius()
{
  return GetEllipseRadii(GetStyleBorder()->mBorderRadius,
                         NS_CORNER_TOP_LEFT, true);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderTopRightRadius()
{
  return GetEllipseRadii(GetStyleBorder()->mBorderRadius,
                         NS_CORNER_TOP_RIGHT, true);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderTopWidth()
{
  return GetBorderWidthFor(NS_SIDE_TOP);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderBottomWidth()
{
  return GetBorderWidthFor(NS_SIDE_BOTTOM);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderLeftWidth()
{
  return GetBorderWidthFor(NS_SIDE_LEFT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderRightWidth()
{
  return GetBorderWidthFor(NS_SIDE_RIGHT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderTopColor()
{
  return GetBorderColorFor(NS_SIDE_TOP);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderBottomColor()
{
  return GetBorderColorFor(NS_SIDE_BOTTOM);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderLeftColor()
{
  return GetBorderColorFor(NS_SIDE_LEFT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderRightColor()
{
  return GetBorderColorFor(NS_SIDE_RIGHT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMarginTopWidth()
{
  return GetMarginWidthFor(NS_SIDE_TOP);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMarginBottomWidth()
{
  return GetMarginWidthFor(NS_SIDE_BOTTOM);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMarginLeftWidth()
{
  return GetMarginWidthFor(NS_SIDE_LEFT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMarginRightWidth()
{
  return GetMarginWidthFor(NS_SIDE_RIGHT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMarkerOffset()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  SetValueToCoord(val, GetStyleContent()->mMarkerOffset, false);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOrient()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleDisplay()->mOrient,
                                   nsCSSProps::kOrientKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOutlineWidth()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleOutline* outline = GetStyleOutline();

  nscoord width;
  if (outline->GetOutlineStyle() == NS_STYLE_BORDER_STYLE_NONE) {
    NS_ASSERTION(outline->GetOutlineWidth(width) && width == 0,
                 "unexpected width");
    width = 0;
  } else {
#ifdef DEBUG
    bool res =
#endif
      outline->GetOutlineWidth(width);
    NS_ASSERTION(res, "percent outline doesn't exist");
  }
  val->SetAppUnits(width);

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOutlineStyle()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleOutline()->GetOutlineStyle(),
                                   nsCSSProps::kOutlineStyleKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOutlineOffset()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetAppUnits(GetStyleOutline()->mOutlineOffset);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOutlineRadiusBottomLeft()
{
  return GetEllipseRadii(GetStyleOutline()->mOutlineRadius,
                         NS_CORNER_BOTTOM_LEFT, false);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOutlineRadiusBottomRight()
{
  return GetEllipseRadii(GetStyleOutline()->mOutlineRadius,
                         NS_CORNER_BOTTOM_RIGHT, false);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOutlineRadiusTopLeft()
{
  return GetEllipseRadii(GetStyleOutline()->mOutlineRadius,
                         NS_CORNER_TOP_LEFT, false);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOutlineRadiusTopRight()
{
  return GetEllipseRadii(GetStyleOutline()->mOutlineRadius,
                         NS_CORNER_TOP_RIGHT, false);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOutlineColor()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  nscolor color;
  if (!GetStyleOutline()->GetOutlineColor(color))
    color = GetStyleColor()->mColor;

  SetToRGBAColor(val, color);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetEllipseRadii(const nsStyleCorners& aRadius,
                                    uint8_t aFullCorner,
                                    bool aIsBorder) // else outline
{
  nsStyleCoord radiusX, radiusY;
  if (mInnerFrame && aIsBorder) {
    nscoord radii[8];
    mInnerFrame->GetBorderRadii(radii);
    radiusX.SetCoordValue(radii[NS_FULL_TO_HALF_CORNER(aFullCorner, false)]);
    radiusY.SetCoordValue(radii[NS_FULL_TO_HALF_CORNER(aFullCorner, true)]);
  } else {
    radiusX = aRadius.Get(NS_FULL_TO_HALF_CORNER(aFullCorner, false));
    radiusY = aRadius.Get(NS_FULL_TO_HALF_CORNER(aFullCorner, true));

    if (mInnerFrame) {
      // We need to convert to absolute coordinates before doing the
      // equality check below.
      nscoord v;

      v = StyleCoordToNSCoord(radiusX,
                              &nsComputedDOMStyle::GetFrameBorderRectWidth,
                              0, true);
      radiusX.SetCoordValue(v);

      v = StyleCoordToNSCoord(radiusY,
                              &nsComputedDOMStyle::GetFrameBorderRectHeight,
                              0, true);
      radiusY.SetCoordValue(v);
    }
  }

  // for compatibility, return a single value if X and Y are equal
  if (radiusX == radiusY) {
    nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

    SetValueToCoord(val, radiusX, true);

    return val;
  }

  nsDOMCSSValueList *valueList = GetROCSSValueList(false);

  nsROCSSPrimitiveValue *valX = GetROCSSPrimitiveValue();
  valueList->AppendCSSValue(valX);

  nsROCSSPrimitiveValue *valY = GetROCSSPrimitiveValue();
  valueList->AppendCSSValue(valY);

  SetValueToCoord(valX, radiusX, true);
  SetValueToCoord(valY, radiusY, true);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetCSSShadowArray(nsCSSShadowArray* aArray,
                                      const nscolor& aDefaultColor,
                                      bool aIsBoxShadow)
{
  if (!aArray) {
    nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
    val->SetIdent(eCSSKeyword_none);
    return val;
  }

  static nscoord nsCSSShadowItem::* const shadowValuesNoSpread[] = {
    &nsCSSShadowItem::mXOffset,
    &nsCSSShadowItem::mYOffset,
    &nsCSSShadowItem::mRadius
  };

  static nscoord nsCSSShadowItem::* const shadowValuesWithSpread[] = {
    &nsCSSShadowItem::mXOffset,
    &nsCSSShadowItem::mYOffset,
    &nsCSSShadowItem::mRadius,
    &nsCSSShadowItem::mSpread
  };

  nscoord nsCSSShadowItem::* const * shadowValues;
  uint32_t shadowValuesLength;
  if (aIsBoxShadow) {
    shadowValues = shadowValuesWithSpread;
    shadowValuesLength = ArrayLength(shadowValuesWithSpread);
  } else {
    shadowValues = shadowValuesNoSpread;
    shadowValuesLength = ArrayLength(shadowValuesNoSpread);
  }

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  for (nsCSSShadowItem *item = aArray->ShadowAt(0),
                   *item_end = item + aArray->Length();
       item < item_end; ++item) {
    nsDOMCSSValueList *itemList = GetROCSSValueList(false);
    valueList->AppendCSSValue(itemList);

    // Color is either the specified shadow color or the foreground color
    nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
    itemList->AppendCSSValue(val);
    nscolor shadowColor;
    if (item->mHasColor) {
      shadowColor = item->mColor;
    } else {
      shadowColor = aDefaultColor;
    }
    SetToRGBAColor(val, shadowColor);

    // Set the offsets, blur radius, and spread if available
    for (uint32_t i = 0; i < shadowValuesLength; ++i) {
      val = GetROCSSPrimitiveValue();
      itemList->AppendCSSValue(val);
      val->SetAppUnits(item->*(shadowValues[i]));
    }

    if (item->mInset && aIsBoxShadow) {
      // This is an inset box-shadow
      val = GetROCSSPrimitiveValue();
      itemList->AppendCSSValue(val);
      val->SetIdent(
        nsCSSProps::ValueToKeywordEnum(NS_STYLE_BOX_SHADOW_INSET,
                                       nsCSSProps::kBoxShadowTypeKTable));
    }
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBoxShadow()
{
  return GetCSSShadowArray(GetStyleBorder()->mBoxShadow,
                           GetStyleColor()->mColor,
                           true);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetZIndex()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  SetValueToCoord(val, GetStylePosition()->mZIndex, false);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetListStyleImage()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleList* list = GetStyleList();

  if (!list->GetListStyleImage()) {
    val->SetIdent(eCSSKeyword_none);
  } else {
    nsCOMPtr<nsIURI> uri;
    if (list->GetListStyleImage()) {
      list->GetListStyleImage()->GetURI(getter_AddRefs(uri));
    }
    val->SetURI(uri);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetListStylePosition()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleList()->mListStylePosition,
                                   nsCSSProps::kListStylePositionKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetListStyleType()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleList()->mListStyleType,
                                   nsCSSProps::kListStyleKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetImageRegion()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  const nsStyleList* list = GetStyleList();

  if (list->mImageRegion.width <= 0 || list->mImageRegion.height <= 0) {
    val->SetIdent(eCSSKeyword_auto);
  } else {
    // create the cssvalues for the sides, stick them in the rect object
    nsROCSSPrimitiveValue *topVal    = GetROCSSPrimitiveValue();
    nsROCSSPrimitiveValue *rightVal  = GetROCSSPrimitiveValue();
    nsROCSSPrimitiveValue *bottomVal = GetROCSSPrimitiveValue();
    nsROCSSPrimitiveValue *leftVal   = GetROCSSPrimitiveValue();
    nsDOMCSSRect * domRect = new nsDOMCSSRect(topVal, rightVal,
                                              bottomVal, leftVal);
    topVal->SetAppUnits(list->mImageRegion.y);
    rightVal->SetAppUnits(list->mImageRegion.width + list->mImageRegion.x);
    bottomVal->SetAppUnits(list->mImageRegion.height + list->mImageRegion.y);
    leftVal->SetAppUnits(list->mImageRegion.x);
    val->SetRect(domRect);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetLineHeight()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  nscoord lineHeight;
  if (GetLineHeightCoord(lineHeight)) {
    val->SetAppUnits(lineHeight);
  } else {
    SetValueToCoord(val, GetStyleText()->mLineHeight, true,
                    nullptr, nsCSSProps::kLineHeightKTable);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetVerticalAlign()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  SetValueToCoord(val, GetStyleTextReset()->mVerticalAlign, false,
                  &nsComputedDOMStyle::GetLineHeightCoord,
                  nsCSSProps::kVerticalAlignKTable);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextAlign()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleText()->mTextAlign,
                                   nsCSSProps::kTextAlignKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextAlignLast()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleText()->mTextAlignLast,
                                   nsCSSProps::kTextAlignLastKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMozTextBlink()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleTextReset()->mTextBlink,
                                   nsCSSProps::kTextBlinkKTable));

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextDecoration()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleTextReset* textReset = GetStyleTextReset();

  // If decoration style or color wasn't initial value, the author knew the
  // text-decoration is a shorthand property in CSS 3.
  // Return NULL in such cases.
  if (textReset->GetDecorationStyle() != NS_STYLE_TEXT_DECORATION_STYLE_SOLID) {
    return nullptr;
  }

  nscolor color;
  bool isForegroundColor;
  textReset->GetDecorationColor(color, isForegroundColor);
  if (!isForegroundColor) {
    return nullptr;
  }

  // Otherwise, the web pages may have been written for CSS 2.1 or earlier,
  // i.e., text-decoration was assumed as a longhand property.  In that case,
  // we should return computed value same as CSS 2.1 for backward compatibility.

  uint8_t line = textReset->mTextDecorationLine;
  // Clear the -moz-anchor-decoration bit and the OVERRIDE_ALL bits -- we
  // don't want these to appear in the computed style.
  line &= ~(NS_STYLE_TEXT_DECORATION_LINE_PREF_ANCHORS |
            NS_STYLE_TEXT_DECORATION_LINE_OVERRIDE_ALL);
  uint8_t blink = textReset->mTextBlink;

  if (blink == NS_STYLE_TEXT_BLINK_NONE &&
      line == NS_STYLE_TEXT_DECORATION_LINE_NONE) {
    val->SetIdent(eCSSKeyword_none);
  } else {
    nsAutoString str;
    if (line != NS_STYLE_TEXT_DECORATION_LINE_NONE) {
      nsStyleUtil::AppendBitmaskCSSValue(eCSSProperty_text_decoration_line,
        line, NS_STYLE_TEXT_DECORATION_LINE_UNDERLINE,
        NS_STYLE_TEXT_DECORATION_LINE_LINE_THROUGH, str);
    }
    if (blink != NS_STYLE_TEXT_BLINK_NONE) {
      if (!str.IsEmpty()) {
        str.Append(PRUnichar(' '));
      }
      nsStyleUtil::AppendBitmaskCSSValue(eCSSProperty_text_blink, blink,
        NS_STYLE_TEXT_BLINK_BLINK, NS_STYLE_TEXT_BLINK_BLINK, str);
    }
    val->SetString(str);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextDecorationColor()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  nscolor color;
  bool isForeground;
  GetStyleTextReset()->GetDecorationColor(color, isForeground);
  if (isForeground) {
    color = GetStyleColor()->mColor;
  }

  SetToRGBAColor(val, color);

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextDecorationLine()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  int32_t intValue = GetStyleTextReset()->mTextDecorationLine;

  if (NS_STYLE_TEXT_DECORATION_LINE_NONE == intValue) {
    val->SetIdent(eCSSKeyword_none);
  } else {
    nsAutoString decorationLineString;
    // Clear the -moz-anchor-decoration bit and the OVERRIDE_ALL bits -- we
    // don't want these to appear in the computed style.
    intValue &= ~(NS_STYLE_TEXT_DECORATION_LINE_PREF_ANCHORS |
                  NS_STYLE_TEXT_DECORATION_LINE_OVERRIDE_ALL);
    nsStyleUtil::AppendBitmaskCSSValue(eCSSProperty_text_decoration_line,
      intValue, NS_STYLE_TEXT_DECORATION_LINE_UNDERLINE,
      NS_STYLE_TEXT_DECORATION_LINE_LINE_THROUGH, decorationLineString);
    val->SetString(decorationLineString);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextDecorationStyle()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleTextReset()->GetDecorationStyle(),
                                   nsCSSProps::kTextDecorationStyleKTable));

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextIndent()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  SetValueToCoord(val, GetStyleText()->mTextIndent, false,
                  &nsComputedDOMStyle::GetCBContentWidth);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextOverflow()
{
  const nsStyleTextReset *style = GetStyleTextReset();
  nsROCSSPrimitiveValue *first = GetROCSSPrimitiveValue();
  const nsStyleTextOverflowSide *side = style->mTextOverflow.GetFirstValue();
  if (side->mType == NS_STYLE_TEXT_OVERFLOW_STRING) {
    nsString str;
    nsStyleUtil::AppendEscapedCSSString(side->mString, str);
    first->SetString(str);
  } else {
    first->SetIdent(
      nsCSSProps::ValueToKeywordEnum(side->mType,
                                     nsCSSProps::kTextOverflowKTable));
  }
  side = style->mTextOverflow.GetSecondValue();
  if (!side) {
    return first;
  }
  nsROCSSPrimitiveValue *second = GetROCSSPrimitiveValue();
  if (side->mType == NS_STYLE_TEXT_OVERFLOW_STRING) {
    nsString str;
    nsStyleUtil::AppendEscapedCSSString(side->mString, str);
    second->SetString(str);
  } else {
    second->SetIdent(
      nsCSSProps::ValueToKeywordEnum(side->mType,
                                     nsCSSProps::kTextOverflowKTable));
  }

  nsDOMCSSValueList *valueList = GetROCSSValueList(false);
  valueList->AppendCSSValue(first);
  valueList->AppendCSSValue(second);
  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextShadow()
{
  return GetCSSShadowArray(GetStyleText()->mTextShadow,
                           GetStyleColor()->mColor,
                           false);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextTransform()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleText()->mTextTransform,
                                   nsCSSProps::kTextTransformKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTabSize()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStyleText()->mTabSize);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetLetterSpacing()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  SetValueToCoord(val, GetStyleText()->mLetterSpacing, false);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetWordSpacing()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetAppUnits(GetStyleText()->mWordSpacing);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetWhiteSpace()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleText()->mWhiteSpace,
                                   nsCSSProps::kWhitespaceKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetWindowShadow()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleUIReset()->mWindowShadow,
                                   nsCSSProps::kWindowShadowKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetWordBreak()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleText()->mWordBreak,
                                   nsCSSProps::kWordBreakKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetWordWrap()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleText()->mWordWrap,
                                   nsCSSProps::kWordWrapKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetHyphens()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleText()->mHyphens,
                                   nsCSSProps::kHyphensKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextSizeAdjust()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  switch (GetStyleText()->mTextSizeAdjust) {
    default:
      NS_NOTREACHED("unexpected value");
      // fall through
    case NS_STYLE_TEXT_SIZE_ADJUST_AUTO:
      val->SetIdent(eCSSKeyword_auto);
      break;
    case NS_STYLE_TEXT_SIZE_ADJUST_NONE:
      val->SetIdent(eCSSKeyword_none);
      break;
  }
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetPointerEvents()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleVisibility()->mPointerEvents,
                                   nsCSSProps::kPointerEventsKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetVisibility()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(GetStyleVisibility()->mVisible,
                                               nsCSSProps::kVisibilityKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetDirection()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleVisibility()->mDirection,
                                   nsCSSProps::kDirectionKTable));
  return val;
}

MOZ_STATIC_ASSERT(NS_STYLE_UNICODE_BIDI_NORMAL == 0,
                  "unicode-bidi style constants not as expected");

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetUnicodeBidi()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleTextReset()->mUnicodeBidi,
                                   nsCSSProps::kUnicodeBidiKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetCursor()
{
  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  const nsStyleUserInterface *ui = GetStyleUserInterface();

  for (nsCursorImage *item = ui->mCursorArray,
         *item_end = ui->mCursorArray + ui->mCursorArrayLength;
       item < item_end; ++item) {
    nsDOMCSSValueList *itemList = GetROCSSValueList(false);
    valueList->AppendCSSValue(itemList);

    nsCOMPtr<nsIURI> uri;
    item->GetImage()->GetURI(getter_AddRefs(uri));

    nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
    itemList->AppendCSSValue(val);
    val->SetURI(uri);

    if (item->mHaveHotspot) {
      nsROCSSPrimitiveValue *valX = GetROCSSPrimitiveValue();
      itemList->AppendCSSValue(valX);
      nsROCSSPrimitiveValue *valY = GetROCSSPrimitiveValue();
      itemList->AppendCSSValue(valY);

      valX->SetNumber(item->mHotspotX);
      valY->SetNumber(item->mHotspotY);
    }
  }

  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(ui->mCursor,
                                               nsCSSProps::kCursorKTable));
  valueList->AppendCSSValue(val);
  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetAppearance()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(GetStyleDisplay()->mAppearance,
                                               nsCSSProps::kAppearanceKTable));
  return val;
}


nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBoxAlign()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(GetStyleXUL()->mBoxAlign,
                                               nsCSSProps::kBoxAlignKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBoxDirection()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleXUL()->mBoxDirection,
                                   nsCSSProps::kBoxDirectionKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBoxFlex()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStyleXUL()->mBoxFlex);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBoxOrdinalGroup()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStyleXUL()->mBoxOrdinal);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBoxOrient()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleXUL()->mBoxOrient,
                                   nsCSSProps::kBoxOrientKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBoxPack()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(GetStyleXUL()->mBoxPack,
                                               nsCSSProps::kBoxPackKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBoxSizing()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStylePosition()->mBoxSizing,
                                   nsCSSProps::kBoxSizingKTable));
  return val;
}

/* Border image properties */

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderImageSource()
{
  const nsStyleBorder* border = GetStyleBorder();

  imgIRequest* imgSrc = border->GetBorderImage();
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  if (imgSrc) {
    nsCOMPtr<nsIURI> uri;
    imgSrc->GetURI(getter_AddRefs(uri));
    val->SetURI(uri);
  } else {
    val->SetIdent(eCSSKeyword_none);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderImageSlice()
{
  nsDOMCSSValueList* valueList = GetROCSSValueList(false);

  const nsStyleBorder* border = GetStyleBorder();
  // Four slice numbers.
  NS_FOR_CSS_SIDES (side) {
    nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(val);
    SetValueToCoord(val, border->mBorderImageSlice.Get(side), nullptr, nullptr);
  }

  // Fill keyword.
  if (NS_STYLE_BORDER_IMAGE_SLICE_FILL == border->mBorderImageFill) {
    nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(val);
    val->SetIdent(eCSSKeyword_fill);
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderImageWidth()
{
  const nsStyleBorder* border = GetStyleBorder();
  nsDOMCSSValueList* valueList = GetROCSSValueList(false);
  NS_FOR_CSS_SIDES (side) {
    nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(val);
    SetValueToCoord(val, border->mBorderImageWidth.Get(side),
                    nullptr, nullptr);
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderImageOutset()
{
  nsDOMCSSValueList *valueList = GetROCSSValueList(false);

  const nsStyleBorder* border = GetStyleBorder();
  // four slice numbers
  NS_FOR_CSS_SIDES (side) {
    nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(val);
    SetValueToCoord(val, border->mBorderImageOutset.Get(side),
                    nullptr, nullptr);
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetBorderImageRepeat()
{
  nsDOMCSSValueList* valueList = GetROCSSValueList(false);

  const nsStyleBorder* border = GetStyleBorder();

  // horizontal repeat
  nsROCSSPrimitiveValue* valX = GetROCSSPrimitiveValue();
  valueList->AppendCSSValue(valX);
  valX->SetIdent(
    nsCSSProps::ValueToKeywordEnum(border->mBorderImageRepeatH,
                                   nsCSSProps::kBorderImageRepeatKTable));

  // vertical repeat
  nsROCSSPrimitiveValue* valY = GetROCSSPrimitiveValue();
  valueList->AppendCSSValue(valY);
  valY->SetIdent(
    nsCSSProps::ValueToKeywordEnum(border->mBorderImageRepeatV,
                                   nsCSSProps::kBorderImageRepeatKTable));
  return valueList;
}

#ifdef MOZ_FLEXBOX
nsIDOMCSSValue*
nsComputedDOMStyle::DoGetAlignItems()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStylePosition()->mAlignItems,
                                   nsCSSProps::kAlignItemsKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetAlignSelf()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  uint8_t computedAlignSelf = GetStylePosition()->mAlignSelf;

  if (computedAlignSelf == NS_STYLE_ALIGN_SELF_AUTO) {
    // "align-self: auto" needs to compute to parent's align-items value.
    nsStyleContext* parentStyleContext = mStyleContextHolder->GetParent();
    if (parentStyleContext) {
      computedAlignSelf =
        parentStyleContext->GetStylePosition()->mAlignItems;
    } else {
      // No parent --> use default.
      computedAlignSelf = NS_STYLE_ALIGN_ITEMS_INITIAL_VALUE;
    }
  }

  NS_ABORT_IF_FALSE(computedAlignSelf != NS_STYLE_ALIGN_SELF_AUTO,
                    "Should have swapped out 'auto' for something non-auto");
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(computedAlignSelf,
                                   nsCSSProps::kAlignSelfKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFlexBasis()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  // XXXdholbert We could make this more automagic and resolve percentages
  // if we wanted, by passing in a PercentageBaseGetter instead of nullptr
  // below.  Logic would go like this:
  //   if (i'm a flex item) {
  //     if (my flex container is horizontal) {
  //       percentageBaseGetter = &nsComputedDOMStyle::GetCBContentWidth;
  //     } else {
  //       percentageBaseGetter = &nsComputedDOMStyle::GetCBContentHeight;
  //     }
  //   }

  SetValueToCoord(val, GetStylePosition()->mFlexBasis, true,
                  nullptr, nsCSSProps::kWidthKTable);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFlexDirection()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStylePosition()->mFlexDirection,
                                   nsCSSProps::kFlexDirectionKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFlexGrow()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStylePosition()->mFlexGrow);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFlexShrink()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStylePosition()->mFlexShrink);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOrder()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStylePosition()->mOrder);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetJustifyContent()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStylePosition()->mJustifyContent,
                                   nsCSSProps::kJustifyContentKTable));
  return val;
}
#endif // MOZ_FLEXBOX

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFloatEdge()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleBorder()->mFloatEdge,
                                   nsCSSProps::kFloatEdgeKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetForceBrokenImageIcon()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStyleUIReset()->mForceBrokenImageIcon);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetIMEMode()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleUIReset()->mIMEMode,
                                   nsCSSProps::kIMEModeKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetUserFocus()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleUserInterface()->mUserFocus,
                                   nsCSSProps::kUserFocusKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetUserInput()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleUserInterface()->mUserInput,
                                   nsCSSProps::kUserInputKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetUserModify()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleUserInterface()->mUserModify,
                                   nsCSSProps::kUserModifyKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetUserSelect()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleUIReset()->mUserSelect,
                                   nsCSSProps::kUserSelectKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetDisplay()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(GetStyleDisplay()->mDisplay,
                                               nsCSSProps::kDisplayKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetPosition()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(GetStyleDisplay()->mPosition,
                                               nsCSSProps::kPositionKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetClip()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  const nsStyleDisplay* display = GetStyleDisplay();

  if (display->mClipFlags == NS_STYLE_CLIP_AUTO) {
    val->SetIdent(eCSSKeyword_auto);
  } else {
    // create the cssvalues for the sides, stick them in the rect object
    nsROCSSPrimitiveValue *topVal    = GetROCSSPrimitiveValue();
    nsROCSSPrimitiveValue *rightVal  = GetROCSSPrimitiveValue();
    nsROCSSPrimitiveValue *bottomVal = GetROCSSPrimitiveValue();
    nsROCSSPrimitiveValue *leftVal   = GetROCSSPrimitiveValue();
    nsDOMCSSRect * domRect = new nsDOMCSSRect(topVal, rightVal,
                                              bottomVal, leftVal);
    if (display->mClipFlags & NS_STYLE_CLIP_TOP_AUTO) {
      topVal->SetIdent(eCSSKeyword_auto);
    } else {
      topVal->SetAppUnits(display->mClip.y);
    }

    if (display->mClipFlags & NS_STYLE_CLIP_RIGHT_AUTO) {
      rightVal->SetIdent(eCSSKeyword_auto);
    } else {
      rightVal->SetAppUnits(display->mClip.width + display->mClip.x);
    }

    if (display->mClipFlags & NS_STYLE_CLIP_BOTTOM_AUTO) {
      bottomVal->SetIdent(eCSSKeyword_auto);
    } else {
      bottomVal->SetAppUnits(display->mClip.height + display->mClip.y);
    }

    if (display->mClipFlags & NS_STYLE_CLIP_LEFT_AUTO) {
      leftVal->SetIdent(eCSSKeyword_auto);
    } else {
      leftVal->SetAppUnits(display->mClip.x);
    }
    val->SetRect(domRect);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOverflow()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  if (display->mOverflowX != display->mOverflowY) {
    // No value to return.  We can't express this combination of
    // values as a shorthand.
    return nullptr;
  }

  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(display->mOverflowX,
                                               nsCSSProps::kOverflowKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOverflowX()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleDisplay()->mOverflowX,
                                   nsCSSProps::kOverflowSubKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetOverflowY()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleDisplay()->mOverflowY,
                                   nsCSSProps::kOverflowSubKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetResize()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(GetStyleDisplay()->mResize,
                                               nsCSSProps::kResizeKTable));
  return val;
}


nsIDOMCSSValue*
nsComputedDOMStyle::DoGetPageBreakAfter()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  const nsStyleDisplay *display = GetStyleDisplay();

  if (display->mBreakAfter) {
    val->SetIdent(eCSSKeyword_always);
  } else {
    val->SetIdent(eCSSKeyword_auto);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetPageBreakBefore()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  const nsStyleDisplay *display = GetStyleDisplay();

  if (display->mBreakBefore) {
    val->SetIdent(eCSSKeyword_always);
  } else {
    val->SetIdent(eCSSKeyword_auto);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetHeight()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  bool calcHeight = false;

  if (mInnerFrame) {
    calcHeight = true;

    const nsStyleDisplay* displayData = GetStyleDisplay();
    if (displayData->mDisplay == NS_STYLE_DISPLAY_INLINE &&
        !(mInnerFrame->IsFrameOfType(nsIFrame::eReplaced))) {
      calcHeight = false;
    }
  }

  if (calcHeight) {
    AssertFlushedPendingReflows();

    val->SetAppUnits(mInnerFrame->GetContentRect().height);
  } else {
    const nsStylePosition *positionData = GetStylePosition();

    nscoord minHeight =
      StyleCoordToNSCoord(positionData->mMinHeight,
                          &nsComputedDOMStyle::GetCBContentHeight, 0, true);

    nscoord maxHeight =
      StyleCoordToNSCoord(positionData->mMaxHeight,
                          &nsComputedDOMStyle::GetCBContentHeight,
                          nscoord_MAX, true);

    SetValueToCoord(val, positionData->mHeight, true, nullptr, nullptr,
                    minHeight, maxHeight);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetWidth()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  bool calcWidth = false;

  if (mInnerFrame) {
    calcWidth = true;

    const nsStyleDisplay *displayData = GetStyleDisplay();
    if (displayData->mDisplay == NS_STYLE_DISPLAY_INLINE &&
        !(mInnerFrame->IsFrameOfType(nsIFrame::eReplaced))) {
      calcWidth = false;
    }
  }

  if (calcWidth) {
    AssertFlushedPendingReflows();

    val->SetAppUnits(mInnerFrame->GetContentRect().width);
  } else {
    const nsStylePosition *positionData = GetStylePosition();

    nscoord minWidth =
      StyleCoordToNSCoord(positionData->mMinWidth,
                          &nsComputedDOMStyle::GetCBContentWidth, 0, true);

    nscoord maxWidth =
      StyleCoordToNSCoord(positionData->mMaxWidth,
                          &nsComputedDOMStyle::GetCBContentWidth,
                          nscoord_MAX, true);

    SetValueToCoord(val, positionData->mWidth, true, nullptr,
                    nsCSSProps::kWidthKTable, minWidth, maxWidth);
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMaxHeight()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  SetValueToCoord(val, GetStylePosition()->mMaxHeight, true,
                  &nsComputedDOMStyle::GetCBContentHeight);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMaxWidth()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  SetValueToCoord(val, GetStylePosition()->mMaxWidth, true,
                  &nsComputedDOMStyle::GetCBContentWidth,
                  nsCSSProps::kWidthKTable);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMinHeight()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  SetValueToCoord(val, GetStylePosition()->mMinHeight, true,
                  &nsComputedDOMStyle::GetCBContentHeight);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMinWidth()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  SetValueToCoord(val, GetStylePosition()->mMinWidth, true,
                  &nsComputedDOMStyle::GetCBContentWidth,
                  nsCSSProps::kWidthKTable);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetLeft()
{
  return GetOffsetWidthFor(NS_SIDE_LEFT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetRight()
{
  return GetOffsetWidthFor(NS_SIDE_RIGHT);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTop()
{
  return GetOffsetWidthFor(NS_SIDE_TOP);
}

nsROCSSPrimitiveValue*
nsComputedDOMStyle::GetROCSSPrimitiveValue()
{
  nsROCSSPrimitiveValue *primitiveValue = new nsROCSSPrimitiveValue();

  NS_ASSERTION(primitiveValue != 0, "ran out of memory");

  return primitiveValue;
}

nsDOMCSSValueList*
nsComputedDOMStyle::GetROCSSValueList(bool aCommaDelimited)
{
  nsDOMCSSValueList *valueList = new nsDOMCSSValueList(aCommaDelimited,
                                                       true);
  NS_ASSERTION(valueList != 0, "ran out of memory");

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetOffsetWidthFor(mozilla::css::Side aSide)
{
  const nsStyleDisplay* display = GetStyleDisplay();

  AssertFlushedPendingReflows();

  uint8_t position = display->mPosition;
  if (!mOuterFrame) {
    // GetRelativeOffset and GetAbsoluteOffset don't handle elements
    // without frames in any sensible way.  GetStaticOffset, however,
    // is perfect for that case.
    position = NS_STYLE_POSITION_STATIC;
  }

  switch (position) {
    case NS_STYLE_POSITION_STATIC:
      return GetStaticOffset(aSide);
    case NS_STYLE_POSITION_RELATIVE:
      return GetRelativeOffset(aSide);
    case NS_STYLE_POSITION_ABSOLUTE:
    case NS_STYLE_POSITION_FIXED:
      return GetAbsoluteOffset(aSide);
    default:
      NS_ERROR("Invalid position");
      return nullptr;
  }
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetAbsoluteOffset(mozilla::css::Side aSide)
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  nsIFrame* container = GetContainingBlockFor(mOuterFrame);
  if (container) {
    nsMargin margin = mOuterFrame->GetUsedMargin();
    nsMargin border = container->GetUsedBorder();
    nsMargin scrollbarSizes(0, 0, 0, 0);
    nsRect rect = mOuterFrame->GetRect();
    nsRect containerRect = container->GetRect();

    if (container->GetType() == nsGkAtoms::viewportFrame) {
      // For absolutely positioned frames scrollbars are taken into
      // account by virtue of getting a containing block that does
      // _not_ include the scrollbars.  For fixed positioned frames,
      // the containing block is the viewport, which _does_ include
      // scrollbars.  We have to do some extra work.
      // the first child in the default frame list is what we want
      nsIFrame* scrollingChild = container->GetFirstPrincipalChild();
      nsIScrollableFrame *scrollFrame = do_QueryFrame(scrollingChild);
      if (scrollFrame) {
        scrollbarSizes = scrollFrame->GetActualScrollbarSizes();
      }
    }

    nscoord offset = 0;
    switch (aSide) {
      case NS_SIDE_TOP:
        offset = rect.y - margin.top - border.top - scrollbarSizes.top;

        break;
      case NS_SIDE_RIGHT:
        offset = containerRect.width - rect.width -
          rect.x - margin.right - border.right - scrollbarSizes.right;

        break;
      case NS_SIDE_BOTTOM:
        offset = containerRect.height - rect.height -
          rect.y - margin.bottom - border.bottom - scrollbarSizes.bottom;

        break;
      case NS_SIDE_LEFT:
        offset = rect.x - margin.left - border.left - scrollbarSizes.left;

        break;
      default:
        NS_ERROR("Invalid side");
        break;
    }
    val->SetAppUnits(offset);
  } else {
    // XXX no frame.  This property makes no sense
    val->SetAppUnits(0);
  }

  return val;
}

MOZ_STATIC_ASSERT(NS_SIDE_TOP == 0 && NS_SIDE_RIGHT == 1 &&
                  NS_SIDE_BOTTOM == 2 && NS_SIDE_LEFT == 3,
                  "box side constants not as expected for NS_OPPOSITE_SIDE");
#define NS_OPPOSITE_SIDE(s_) mozilla::css::Side(((s_) + 2) & 3)

nsIDOMCSSValue*
nsComputedDOMStyle::GetRelativeOffset(mozilla::css::Side aSide)
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();

  const nsStylePosition* positionData = GetStylePosition();
  int32_t sign = 1;
  nsStyleCoord coord = positionData->mOffset.Get(aSide);

  NS_ASSERTION(coord.GetUnit() == eStyleUnit_Coord ||
               coord.GetUnit() == eStyleUnit_Percent ||
               coord.GetUnit() == eStyleUnit_Auto ||
               coord.IsCalcUnit(),
               "Unexpected unit");

  if (coord.GetUnit() == eStyleUnit_Auto) {
    coord = positionData->mOffset.Get(NS_OPPOSITE_SIDE(aSide));
    sign = -1;
  }
  PercentageBaseGetter baseGetter;
  if (aSide == NS_SIDE_LEFT || aSide == NS_SIDE_RIGHT) {
    baseGetter = &nsComputedDOMStyle::GetCBContentWidth;
  } else {
    baseGetter = &nsComputedDOMStyle::GetCBContentHeight;
  }

  val->SetAppUnits(sign * StyleCoordToNSCoord(coord, baseGetter, 0, false));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetStaticOffset(mozilla::css::Side aSide)

{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  SetValueToCoord(val, GetStylePosition()->mOffset.Get(aSide), false);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetPaddingWidthFor(mozilla::css::Side aSide)
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  if (!mInnerFrame) {
    SetValueToCoord(val, GetStylePadding()->mPadding.Get(aSide), true);
  } else {
    AssertFlushedPendingReflows();

    val->SetAppUnits(mInnerFrame->GetUsedPadding().Side(aSide));
  }

  return val;
}

bool
nsComputedDOMStyle::GetLineHeightCoord(nscoord& aCoord)
{
  AssertFlushedPendingReflows();

  nscoord blockHeight = NS_AUTOHEIGHT;
  if (GetStyleText()->mLineHeight.GetUnit() == eStyleUnit_Enumerated) {
    if (!mInnerFrame)
      return false;

    if (nsLayoutUtils::IsNonWrapperBlock(mInnerFrame)) {
      blockHeight = mInnerFrame->GetContentRect().height;
    } else {
      GetCBContentHeight(blockHeight);
    }
  }

  // lie about font size inflation since we lie about font size (since
  // the inflation only applies to text)
  aCoord = nsHTMLReflowState::CalcLineHeight(mStyleContextHolder,
                                             blockHeight, 1.0f);

  // CalcLineHeight uses font->mFont.size, but we want to use
  // font->mSize as the font size.  Adjust for that.  Also adjust for
  // the text zoom, if any.
  const nsStyleFont* font = GetStyleFont();
  float fCoord = float(aCoord) / mPresShell->GetPresContext()->TextZoom();
  if (font->mFont.size != font->mSize) {
    fCoord = fCoord * (float(font->mSize) / float(font->mFont.size));
  }
  aCoord = NSToCoordRound(fCoord);

  return true;
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetBorderColorsFor(mozilla::css::Side aSide)
{
  const nsStyleBorder *border = GetStyleBorder();

  if (border->mBorderColors) {
    nsBorderColors* borderColors = border->mBorderColors[aSide];
    if (borderColors) {
      nsDOMCSSValueList *valueList = GetROCSSValueList(false);

      do {
        nsROCSSPrimitiveValue *primitive = GetROCSSPrimitiveValue();

        SetToRGBAColor(primitive, borderColors->mColor);

        valueList->AppendCSSValue(primitive);
        borderColors = borderColors->mNext;
      } while (borderColors);

      return valueList;
    }
  }

  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(eCSSKeyword_none);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetBorderWidthFor(mozilla::css::Side aSide)
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  nscoord width;
  if (mInnerFrame) {
    AssertFlushedPendingReflows();
    width = mInnerFrame->GetUsedBorder().Side(aSide);
  } else {
    width = GetStyleBorder()->GetComputedBorderWidth(aSide);
  }
  val->SetAppUnits(width);

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetBorderColorFor(mozilla::css::Side aSide)
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  nscolor color;
  bool foreground;
  GetStyleBorder()->GetBorderColor(aSide, color, foreground);
  if (foreground) {
    color = GetStyleColor()->mColor;
  }

  SetToRGBAColor(val, color);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetMarginWidthFor(mozilla::css::Side aSide)
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  if (!mInnerFrame) {
    SetValueToCoord(val, GetStyleMargin()->mMargin.Get(aSide), false);
  } else {
    AssertFlushedPendingReflows();

    // For tables, GetUsedMargin always returns an empty margin, so we
    // should read the margin from the outer table frame instead.
    val->SetAppUnits(mOuterFrame->GetUsedMargin().Side(aSide));
    NS_ASSERTION(mOuterFrame == mInnerFrame ||
                 mInnerFrame->GetUsedMargin() == nsMargin(0, 0, 0, 0),
                 "Inner tables must have zero margins");
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetBorderStyleFor(mozilla::css::Side aSide)
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleBorder()->GetBorderStyle(aSide),
                                   nsCSSProps::kBorderStyleKTable));
  return val;
}

void
nsComputedDOMStyle::SetValueToCoord(nsROCSSPrimitiveValue* aValue,
                                    const nsStyleCoord& aCoord,
                                    bool aClampNegativeCalc,
                                    PercentageBaseGetter aPercentageBaseGetter,
                                    const int32_t aTable[],
                                    nscoord aMinAppUnits,
                                    nscoord aMaxAppUnits)
{
  NS_PRECONDITION(aValue, "Must have a value to work with");

  switch (aCoord.GetUnit()) {
    case eStyleUnit_Normal:
      aValue->SetIdent(eCSSKeyword_normal);
      break;

    case eStyleUnit_Auto:
      aValue->SetIdent(eCSSKeyword_auto);
      break;

    case eStyleUnit_Percent:
      {
        nscoord percentageBase;
        if (aPercentageBaseGetter &&
            (this->*aPercentageBaseGetter)(percentageBase)) {
          nscoord val = NSCoordSaturatingMultiply(percentageBase,
                                                  aCoord.GetPercentValue());
          aValue->SetAppUnits(NS_MAX(aMinAppUnits, NS_MIN(val, aMaxAppUnits)));
        } else {
          aValue->SetPercent(aCoord.GetPercentValue());
        }
      }
      break;

    case eStyleUnit_Factor:
      aValue->SetNumber(aCoord.GetFactorValue());
      break;

    case eStyleUnit_Coord:
      {
        nscoord val = aCoord.GetCoordValue();
        aValue->SetAppUnits(NS_MAX(aMinAppUnits, NS_MIN(val, aMaxAppUnits)));
      }
      break;

    case eStyleUnit_Integer:
      aValue->SetNumber(aCoord.GetIntValue());
      break;

    case eStyleUnit_Enumerated:
      NS_ASSERTION(aTable, "Must have table to handle this case");
      aValue->SetIdent(nsCSSProps::ValueToKeywordEnum(aCoord.GetIntValue(),
                                                      aTable));
      break;

    case eStyleUnit_None:
      aValue->SetIdent(eCSSKeyword_none);
      break;

    case eStyleUnit_Calc:
      nscoord percentageBase;
      if (!aCoord.CalcHasPercent()) {
        nscoord val = nsRuleNode::ComputeCoordPercentCalc(aCoord, 0);
        if (aClampNegativeCalc && val < 0) {
          NS_ABORT_IF_FALSE(aCoord.IsCalcUnit(),
                            "parser should have rejected value");
          val = 0;
        }
        aValue->SetAppUnits(NS_MAX(aMinAppUnits, NS_MIN(val, aMaxAppUnits)));
      } else if (aPercentageBaseGetter &&
                 (this->*aPercentageBaseGetter)(percentageBase)) {
        nscoord val =
          nsRuleNode::ComputeCoordPercentCalc(aCoord, percentageBase);
        if (aClampNegativeCalc && val < 0) {
          NS_ABORT_IF_FALSE(aCoord.IsCalcUnit(),
                            "parser should have rejected value");
          val = 0;
        }
        aValue->SetAppUnits(NS_MAX(aMinAppUnits, NS_MIN(val, aMaxAppUnits)));
      } else {
        nsStyleCoord::Calc *calc = aCoord.GetCalcValue();
        SetValueToCalc(calc, aValue);
      }
      break;
    default:
      NS_ERROR("Can't handle this unit");
      break;
  }
}

nscoord
nsComputedDOMStyle::StyleCoordToNSCoord(const nsStyleCoord& aCoord,
                                        PercentageBaseGetter aPercentageBaseGetter,
                                        nscoord aDefaultValue,
                                        bool aClampNegativeCalc)
{
  NS_PRECONDITION(aPercentageBaseGetter, "Must have a percentage base getter");
  if (aCoord.GetUnit() == eStyleUnit_Coord) {
    return aCoord.GetCoordValue();
  }
  if (aCoord.GetUnit() == eStyleUnit_Percent || aCoord.IsCalcUnit()) {
    nscoord percentageBase;
    if ((this->*aPercentageBaseGetter)(percentageBase)) {
      nscoord result =
        nsRuleNode::ComputeCoordPercentCalc(aCoord, percentageBase);
      if (aClampNegativeCalc && result < 0) {
        NS_ABORT_IF_FALSE(aCoord.IsCalcUnit(),
                          "parser should have rejected value");
        result = 0;
      }
      return result;
    }
    // Fall through to returning aDefaultValue if we have no percentage base.
  }

  return aDefaultValue;
}

bool
nsComputedDOMStyle::GetCBContentWidth(nscoord& aWidth)
{
  if (!mOuterFrame) {
    return false;
  }

  nsIFrame* container = GetContainingBlockFor(mOuterFrame);
  if (!container) {
    return false;
  }

  AssertFlushedPendingReflows();

  aWidth = container->GetContentRect().width;
  return true;
}

bool
nsComputedDOMStyle::GetCBContentHeight(nscoord& aHeight)
{
  if (!mOuterFrame) {
    return false;
  }

  nsIFrame* container = GetContainingBlockFor(mOuterFrame);
  if (!container) {
    return false;
  }

  AssertFlushedPendingReflows();

  aHeight = container->GetContentRect().height;
  return true;
}

bool
nsComputedDOMStyle::GetFrameBorderRectWidth(nscoord& aWidth)
{
  if (!mInnerFrame) {
    return false;
  }

  AssertFlushedPendingReflows();

  aWidth = mInnerFrame->GetSize().width;
  return true;
}

bool
nsComputedDOMStyle::GetFrameBorderRectHeight(nscoord& aHeight)
{
  if (!mInnerFrame) {
    return false;
  }

  AssertFlushedPendingReflows();

  aHeight = mInnerFrame->GetSize().height;
  return true;
}

bool
nsComputedDOMStyle::GetFrameBoundsWidthForTransform(nscoord& aWidth)
{
  // We need a frame to work with.
  if (!mInnerFrame) {
    return false;
  }

  AssertFlushedPendingReflows();

  aWidth = nsDisplayTransform::GetFrameBoundsForTransform(mInnerFrame).width;
  return true;
}

bool
nsComputedDOMStyle::GetFrameBoundsHeightForTransform(nscoord& aHeight)
{
  // We need a frame to work with.
  if (!mInnerFrame) {
    return false;
  }

  AssertFlushedPendingReflows();

  aHeight = nsDisplayTransform::GetFrameBoundsForTransform(mInnerFrame).height;
  return true;
}

nsIDOMCSSValue*
nsComputedDOMStyle::GetSVGPaintFor(bool aFill)
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleSVG* svg = GetStyleSVG();
  const nsStyleSVGPaint* paint = nullptr;

  if (aFill)
    paint = &svg->mFill;
  else
    paint = &svg->mStroke;

  nsAutoString paintString;

  switch (paint->mType) {
    case eStyleSVGPaintType_None:
    {
      val->SetIdent(eCSSKeyword_none);
      break;
    }
    case eStyleSVGPaintType_Color:
    {
      SetToRGBAColor(val, paint->mPaint.mColor);
      break;
    }
    case eStyleSVGPaintType_Server:
    {
      nsDOMCSSValueList *valueList = GetROCSSValueList(false);
      valueList->AppendCSSValue(val);

      nsROCSSPrimitiveValue* fallback = GetROCSSPrimitiveValue();
      valueList->AppendCSSValue(fallback);

      val->SetURI(paint->mPaint.mPaintServer);
      SetToRGBAColor(fallback, paint->mFallbackColor);
      return valueList;
    }
  }

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFill()
{
  return GetSVGPaintFor(true);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetStroke()
{
  return GetSVGPaintFor(false);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMarkerEnd()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleSVG* svg = GetStyleSVG();

  if (svg->mMarkerEnd)
    val->SetURI(svg->mMarkerEnd);
  else
    val->SetIdent(eCSSKeyword_none);

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMarkerMid()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleSVG* svg = GetStyleSVG();

  if (svg->mMarkerMid)
    val->SetURI(svg->mMarkerMid);
  else
    val->SetIdent(eCSSKeyword_none);

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMarkerStart()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleSVG* svg = GetStyleSVG();

  if (svg->mMarkerStart)
    val->SetURI(svg->mMarkerStart);
  else
    val->SetIdent(eCSSKeyword_none);

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetStrokeDasharray()
{
  const nsStyleSVG* svg = GetStyleSVG();

  if (!svg->mStrokeDasharrayLength || !svg->mStrokeDasharray) {
    nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
    val->SetIdent(eCSSKeyword_none);
    return val;
  }

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  for (uint32_t i = 0; i < svg->mStrokeDasharrayLength; i++) {
    nsROCSSPrimitiveValue* dash = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(dash);

    SetValueToCoord(dash, svg->mStrokeDasharray[i], true);
  }

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetStrokeDashoffset()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  SetValueToCoord(val, GetStyleSVG()->mStrokeDashoffset, false);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetStrokeWidth()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();
  SetValueToCoord(val, GetStyleSVG()->mStrokeWidth, true);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetVectorEffect()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(GetStyleSVGReset()->mVectorEffect,
                                               nsCSSProps::kVectorEffectKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFillOpacity()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStyleSVG()->mFillOpacity);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFloodOpacity()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStyleSVGReset()->mFloodOpacity);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetStopOpacity()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStyleSVGReset()->mStopOpacity);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetStrokeMiterlimit()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStyleSVG()->mStrokeMiterlimit);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetStrokeOpacity()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetNumber(GetStyleSVG()->mStrokeOpacity);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetClipRule()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(
                  GetStyleSVG()->mClipRule, nsCSSProps::kFillRuleKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFillRule()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(nsCSSProps::ValueToKeywordEnum(
                  GetStyleSVG()->mFillRule, nsCSSProps::kFillRuleKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetStrokeLinecap()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleSVG()->mStrokeLinecap,
                                   nsCSSProps::kStrokeLinecapKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetStrokeLinejoin()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleSVG()->mStrokeLinejoin,
                                   nsCSSProps::kStrokeLinejoinKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextAnchor()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleSVG()->mTextAnchor,
                                   nsCSSProps::kTextAnchorKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetColorInterpolation()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleSVG()->mColorInterpolation,
                                   nsCSSProps::kColorInterpolationKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetColorInterpolationFilters()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleSVG()->mColorInterpolationFilters,
                                   nsCSSProps::kColorInterpolationKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetDominantBaseline()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleSVGReset()->mDominantBaseline,
                                   nsCSSProps::kDominantBaselineKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetImageRendering()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleSVG()->mImageRendering,
                                   nsCSSProps::kImageRenderingKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetShapeRendering()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleSVG()->mShapeRendering,
                                   nsCSSProps::kShapeRenderingKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTextRendering()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  val->SetIdent(
    nsCSSProps::ValueToKeywordEnum(GetStyleSVG()->mTextRendering,
                                   nsCSSProps::kTextRenderingKTable));
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFloodColor()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  SetToRGBAColor(val, GetStyleSVGReset()->mFloodColor);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetLightingColor()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  SetToRGBAColor(val, GetStyleSVGReset()->mLightingColor);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetStopColor()
{
  nsROCSSPrimitiveValue *val = GetROCSSPrimitiveValue();
  SetToRGBAColor(val, GetStyleSVGReset()->mStopColor);
  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetClipPath()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleSVGReset* svg = GetStyleSVGReset();

  if (svg->mClipPath)
    val->SetURI(svg->mClipPath);
  else
    val->SetIdent(eCSSKeyword_none);

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetFilter()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleSVGReset* svg = GetStyleSVGReset();

  if (svg->mFilter)
    val->SetURI(svg->mFilter);
  else
    val->SetIdent(eCSSKeyword_none);

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetMask()
{
  nsROCSSPrimitiveValue* val = GetROCSSPrimitiveValue();

  const nsStyleSVGReset* svg = GetStyleSVGReset();

  if (svg->mMask)
    val->SetURI(svg->mMask);
  else
    val->SetIdent(eCSSKeyword_none);

  return val;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTransitionDelay()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  NS_ABORT_IF_FALSE(display->mTransitionDelayCount > 0,
                    "first item must be explicit");
  uint32_t i = 0;
  do {
    const nsTransition *transition = &display->mTransitions[i];
    nsROCSSPrimitiveValue* delay = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(delay);
    delay->SetTime((float)transition->GetDelay() / (float)PR_MSEC_PER_SEC);
  } while (++i < display->mTransitionDelayCount);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTransitionDuration()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  NS_ABORT_IF_FALSE(display->mTransitionDurationCount > 0,
                    "first item must be explicit");
  uint32_t i = 0;
  do {
    const nsTransition *transition = &display->mTransitions[i];
    nsROCSSPrimitiveValue* duration = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(duration);

    duration->SetTime((float)transition->GetDuration() / (float)PR_MSEC_PER_SEC);
  } while (++i < display->mTransitionDurationCount);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTransitionProperty()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  NS_ABORT_IF_FALSE(display->mTransitionPropertyCount > 0,
                    "first item must be explicit");
  uint32_t i = 0;
  do {
    const nsTransition *transition = &display->mTransitions[i];
    nsROCSSPrimitiveValue* property = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(property);
    nsCSSProperty cssprop = transition->GetProperty();
    if (cssprop == eCSSPropertyExtra_all_properties)
      property->SetIdent(eCSSKeyword_all);
    else if (cssprop == eCSSPropertyExtra_no_properties)
      property->SetIdent(eCSSKeyword_none);
    else if (cssprop == eCSSProperty_UNKNOWN)
    {
      nsAutoString escaped;
      nsStyleUtil::AppendEscapedCSSIdent(
        nsDependentAtomString(transition->GetUnknownProperty()), escaped);
      property->SetString(escaped); // really want SetIdent
    }
    else
      property->SetString(nsCSSProps::GetStringValue(cssprop));
  } while (++i < display->mTransitionPropertyCount);

  return valueList;
}

void
nsComputedDOMStyle::AppendTimingFunction(nsDOMCSSValueList *aValueList,
                                         const nsTimingFunction& aTimingFunction)
{
  nsROCSSPrimitiveValue* timingFunction = GetROCSSPrimitiveValue();
  aValueList->AppendCSSValue(timingFunction);

  nsAutoString tmp;

  if (aTimingFunction.mType == nsTimingFunction::Function) {
    // set the value from the cubic-bezier control points
    // (We could try to regenerate the keywords if we want.)
    tmp.AppendLiteral("cubic-bezier(");
    tmp.AppendFloat(aTimingFunction.mFunc.mX1);
    tmp.AppendLiteral(", ");
    tmp.AppendFloat(aTimingFunction.mFunc.mY1);
    tmp.AppendLiteral(", ");
    tmp.AppendFloat(aTimingFunction.mFunc.mX2);
    tmp.AppendLiteral(", ");
    tmp.AppendFloat(aTimingFunction.mFunc.mY2);
    tmp.AppendLiteral(")");
  } else {
    tmp.AppendLiteral("steps(");
    tmp.AppendInt(aTimingFunction.mSteps);
    if (aTimingFunction.mType == nsTimingFunction::StepStart) {
      tmp.AppendLiteral(", start)");
    } else {
      tmp.AppendLiteral(", end)");
    }
  }
  timingFunction->SetString(tmp);
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetTransitionTimingFunction()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  NS_ABORT_IF_FALSE(display->mTransitionTimingFunctionCount > 0,
                    "first item must be explicit");
  uint32_t i = 0;
  do {
    AppendTimingFunction(valueList,
                         display->mTransitions[i].GetTimingFunction());
  } while (++i < display->mTransitionTimingFunctionCount);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetAnimationName()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  NS_ABORT_IF_FALSE(display->mAnimationNameCount > 0,
                    "first item must be explicit");
  uint32_t i = 0;
  do {
    const nsAnimation *animation = &display->mAnimations[i];
    nsROCSSPrimitiveValue* property = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(property);

    const nsString& name = animation->GetName();
    if (name.IsEmpty()) {
      property->SetIdent(eCSSKeyword_none);
    } else {
      nsAutoString escaped;
      nsStyleUtil::AppendEscapedCSSIdent(animation->GetName(), escaped);
      property->SetString(escaped); // really want SetIdent
    }
  } while (++i < display->mAnimationNameCount);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetAnimationDelay()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  NS_ABORT_IF_FALSE(display->mAnimationDelayCount > 0,
                    "first item must be explicit");
  uint32_t i = 0;
  do {
    const nsAnimation *animation = &display->mAnimations[i];
    nsROCSSPrimitiveValue* delay = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(delay);
    delay->SetTime((float)animation->GetDelay() / (float)PR_MSEC_PER_SEC);
  } while (++i < display->mAnimationDelayCount);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetAnimationDuration()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  NS_ABORT_IF_FALSE(display->mAnimationDurationCount > 0,
                    "first item must be explicit");
  uint32_t i = 0;
  do {
    const nsAnimation *animation = &display->mAnimations[i];
    nsROCSSPrimitiveValue* duration = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(duration);

    duration->SetTime((float)animation->GetDuration() / (float)PR_MSEC_PER_SEC);
  } while (++i < display->mAnimationDurationCount);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetAnimationTimingFunction()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  NS_ABORT_IF_FALSE(display->mAnimationTimingFunctionCount > 0,
                    "first item must be explicit");
  uint32_t i = 0;
  do {
    AppendTimingFunction(valueList,
                         display->mAnimations[i].GetTimingFunction());
  } while (++i < display->mAnimationTimingFunctionCount);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetAnimationDirection()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  NS_ABORT_IF_FALSE(display->mAnimationDirectionCount > 0,
                    "first item must be explicit");
  uint32_t i = 0;
  do {
    const nsAnimation *animation = &display->mAnimations[i];
    nsROCSSPrimitiveValue* direction = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(direction);
    direction->SetIdent(
      nsCSSProps::ValueToKeywordEnum(animation->GetDirection(),
                                     nsCSSProps::kAnimationDirectionKTable));
  } while (++i < display->mAnimationDirectionCount);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetAnimationFillMode()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  NS_ABORT_IF_FALSE(display->mAnimationFillModeCount > 0,
                    "first item must be explicit");
  uint32_t i = 0;
  do {
    const nsAnimation *animation = &display->mAnimations[i];
    nsROCSSPrimitiveValue* fillMode = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(fillMode);
    fillMode->SetIdent(
      nsCSSProps::ValueToKeywordEnum(animation->GetFillMode(),
                                     nsCSSProps::kAnimationFillModeKTable));
  } while (++i < display->mAnimationFillModeCount);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetAnimationIterationCount()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  NS_ABORT_IF_FALSE(display->mAnimationIterationCountCount > 0,
                    "first item must be explicit");
  uint32_t i = 0;
  do {
    const nsAnimation *animation = &display->mAnimations[i];
    nsROCSSPrimitiveValue* iterationCount = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(iterationCount);

    float f = animation->GetIterationCount();
    /* Need a nasty hack here to work around an optimizer bug in gcc
       4.2 on Mac, which somehow gets confused when directly comparing
       a float to the return value of NS_IEEEPositiveInfinity when
       building 32-bit builds. */
#ifdef XP_MACOSX
    volatile
#endif
      float inf = NS_IEEEPositiveInfinity();
    if (f == inf) {
      iterationCount->SetIdent(eCSSKeyword_infinite);
    } else {
      iterationCount->SetNumber(f);
    }
  } while (++i < display->mAnimationIterationCountCount);

  return valueList;
}

nsIDOMCSSValue*
nsComputedDOMStyle::DoGetAnimationPlayState()
{
  const nsStyleDisplay* display = GetStyleDisplay();

  nsDOMCSSValueList *valueList = GetROCSSValueList(true);

  NS_ABORT_IF_FALSE(display->mAnimationPlayStateCount > 0,
                    "first item must be explicit");
  uint32_t i = 0;
  do {
    const nsAnimation *animation = &display->mAnimations[i];
    nsROCSSPrimitiveValue* playState = GetROCSSPrimitiveValue();
    valueList->AppendCSSValue(playState);
    playState->SetIdent(
      nsCSSProps::ValueToKeywordEnum(animation->GetPlayState(),
                                     nsCSSProps::kAnimationPlayStateKTable));
  } while (++i < display->mAnimationPlayStateCount);

  return valueList;
}

#define COMPUTED_STYLE_MAP_ENTRY(_prop, _method)              \
  { eCSSProperty_##_prop, &nsComputedDOMStyle::DoGet##_method, false }
#define COMPUTED_STYLE_MAP_ENTRY_LAYOUT(_prop, _method)       \
  { eCSSProperty_##_prop, &nsComputedDOMStyle::DoGet##_method, true }

const nsComputedDOMStyle::ComputedStyleMapEntry*
nsComputedDOMStyle::GetQueryablePropertyMap(uint32_t* aLength)
{
  /* ******************************************************************* *\
   * Properties below are listed in alphabetical order.                  *
   * Please keep them that way.                                          *
   *                                                                     *
   * Properties commented out with // are not yet implemented            *
   * Properties commented out with //// are shorthands and not queryable *
  \* ******************************************************************* */
  static const ComputedStyleMapEntry map[] = {
    /* ***************************** *\
     * Implementations of CSS styles *
    \* ***************************** */

    //// COMPUTED_STYLE_MAP_ENTRY(animation,                Animation),
    COMPUTED_STYLE_MAP_ENTRY(animation_delay,               AnimationDelay),
    COMPUTED_STYLE_MAP_ENTRY(animation_direction,           AnimationDirection),
    COMPUTED_STYLE_MAP_ENTRY(animation_duration,            AnimationDuration),
    COMPUTED_STYLE_MAP_ENTRY(animation_fill_mode,           AnimationFillMode),
    COMPUTED_STYLE_MAP_ENTRY(animation_iteration_count,     AnimationIterationCount),
    COMPUTED_STYLE_MAP_ENTRY(animation_name,                AnimationName),
    COMPUTED_STYLE_MAP_ENTRY(animation_play_state,          AnimationPlayState),
    COMPUTED_STYLE_MAP_ENTRY(animation_timing_function,     AnimationTimingFunction),
    COMPUTED_STYLE_MAP_ENTRY(backface_visibility,           BackfaceVisibility),
    //// COMPUTED_STYLE_MAP_ENTRY(background,               Background),
    COMPUTED_STYLE_MAP_ENTRY(background_attachment,         BackgroundAttachment),
    COMPUTED_STYLE_MAP_ENTRY(background_clip,               BackgroundClip),
    COMPUTED_STYLE_MAP_ENTRY(background_color,              BackgroundColor),
    COMPUTED_STYLE_MAP_ENTRY(background_image,              BackgroundImage),
    COMPUTED_STYLE_MAP_ENTRY(background_origin,             BackgroundOrigin),
    COMPUTED_STYLE_MAP_ENTRY(background_position,           BackgroundPosition),
    COMPUTED_STYLE_MAP_ENTRY(background_repeat,             BackgroundRepeat),
    COMPUTED_STYLE_MAP_ENTRY(background_size,               BackgroundSize),
    //// COMPUTED_STYLE_MAP_ENTRY(border,                   Border),
    //// COMPUTED_STYLE_MAP_ENTRY(border_bottom,            BorderBottom),
    COMPUTED_STYLE_MAP_ENTRY(border_bottom_color,           BorderBottomColor),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(border_bottom_left_radius, BorderBottomLeftRadius),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(border_bottom_right_radius,BorderBottomRightRadius),
    COMPUTED_STYLE_MAP_ENTRY(border_bottom_style,           BorderBottomStyle),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(border_bottom_width,    BorderBottomWidth),
    COMPUTED_STYLE_MAP_ENTRY(border_collapse,               BorderCollapse),
    //// COMPUTED_STYLE_MAP_ENTRY(border_color,             BorderColor),
    //// COMPUTED_STYLE_MAP_ENTRY(border_image,             BorderImage),
    COMPUTED_STYLE_MAP_ENTRY(border_image_outset,           BorderImageOutset),
    COMPUTED_STYLE_MAP_ENTRY(border_image_repeat,           BorderImageRepeat),
    COMPUTED_STYLE_MAP_ENTRY(border_image_slice,            BorderImageSlice),
    COMPUTED_STYLE_MAP_ENTRY(border_image_source,           BorderImageSource),
    COMPUTED_STYLE_MAP_ENTRY(border_image_width,            BorderImageWidth),
    //// COMPUTED_STYLE_MAP_ENTRY(border_left,              BorderLeft),
    COMPUTED_STYLE_MAP_ENTRY(border_left_color,             BorderLeftColor),
    COMPUTED_STYLE_MAP_ENTRY(border_left_style,             BorderLeftStyle),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(border_left_width,      BorderLeftWidth),
    //// COMPUTED_STYLE_MAP_ENTRY(border_right,             BorderRight),
    COMPUTED_STYLE_MAP_ENTRY(border_right_color,            BorderRightColor),
    COMPUTED_STYLE_MAP_ENTRY(border_right_style,            BorderRightStyle),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(border_right_width,     BorderRightWidth),
    COMPUTED_STYLE_MAP_ENTRY(border_spacing,                BorderSpacing),
    //// COMPUTED_STYLE_MAP_ENTRY(border_style,             BorderStyle),
    //// COMPUTED_STYLE_MAP_ENTRY(border_top,               BorderTop),
    COMPUTED_STYLE_MAP_ENTRY(border_top_color,              BorderTopColor),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(border_top_left_radius,    BorderTopLeftRadius),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(border_top_right_radius,   BorderTopRightRadius),
    COMPUTED_STYLE_MAP_ENTRY(border_top_style,              BorderTopStyle),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(border_top_width,       BorderTopWidth),
    //// COMPUTED_STYLE_MAP_ENTRY(border_width,             BorderWidth),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(bottom,                 Bottom),
    COMPUTED_STYLE_MAP_ENTRY(box_shadow,                    BoxShadow),
    COMPUTED_STYLE_MAP_ENTRY(caption_side,                  CaptionSide),
    COMPUTED_STYLE_MAP_ENTRY(clear,                         Clear),
    COMPUTED_STYLE_MAP_ENTRY(clip,                          Clip),
    COMPUTED_STYLE_MAP_ENTRY(color,                         Color),
    COMPUTED_STYLE_MAP_ENTRY(content,                       Content),
    COMPUTED_STYLE_MAP_ENTRY(counter_increment,             CounterIncrement),
    COMPUTED_STYLE_MAP_ENTRY(counter_reset,                 CounterReset),
    COMPUTED_STYLE_MAP_ENTRY(cursor,                        Cursor),
    COMPUTED_STYLE_MAP_ENTRY(direction,                     Direction),
    COMPUTED_STYLE_MAP_ENTRY(display,                       Display),
    COMPUTED_STYLE_MAP_ENTRY(empty_cells,                   EmptyCells),
    COMPUTED_STYLE_MAP_ENTRY(float,                         CssFloat),
    //// COMPUTED_STYLE_MAP_ENTRY(font,                     Font),
    COMPUTED_STYLE_MAP_ENTRY(font_family,                   FontFamily),
    COMPUTED_STYLE_MAP_ENTRY(font_size,                     FontSize),
    COMPUTED_STYLE_MAP_ENTRY(font_size_adjust,              FontSizeAdjust),
    COMPUTED_STYLE_MAP_ENTRY(font_stretch,                  FontStretch),
    COMPUTED_STYLE_MAP_ENTRY(font_style,                    FontStyle),
    COMPUTED_STYLE_MAP_ENTRY(font_variant,                  FontVariant),
    COMPUTED_STYLE_MAP_ENTRY(font_weight,                   FontWeight),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(height,                 Height),
    COMPUTED_STYLE_MAP_ENTRY(ime_mode,                      IMEMode),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(left,                   Left),
    COMPUTED_STYLE_MAP_ENTRY(letter_spacing,                LetterSpacing),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(line_height,            LineHeight),
    //// COMPUTED_STYLE_MAP_ENTRY(list_style,               ListStyle),
    COMPUTED_STYLE_MAP_ENTRY(list_style_image,              ListStyleImage),
    COMPUTED_STYLE_MAP_ENTRY(list_style_position,           ListStylePosition),
    COMPUTED_STYLE_MAP_ENTRY(list_style_type,               ListStyleType),
    //// COMPUTED_STYLE_MAP_ENTRY(margin,                   Margin),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(margin_bottom,          MarginBottomWidth),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(margin_left,            MarginLeftWidth),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(margin_right,           MarginRightWidth),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(margin_top,             MarginTopWidth),
    COMPUTED_STYLE_MAP_ENTRY(marker_offset,                 MarkerOffset),
    // COMPUTED_STYLE_MAP_ENTRY(marks,                      Marks),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(max_height,             MaxHeight),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(max_width,              MaxWidth),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(min_height,             MinHeight),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(min_width,              MinWidth),
    COMPUTED_STYLE_MAP_ENTRY(opacity,                       Opacity),
    // COMPUTED_STYLE_MAP_ENTRY(orphans,                    Orphans),
    //// COMPUTED_STYLE_MAP_ENTRY(outline,                  Outline),
    COMPUTED_STYLE_MAP_ENTRY(outline_color,                 OutlineColor),
    COMPUTED_STYLE_MAP_ENTRY(outline_offset,                OutlineOffset),
    COMPUTED_STYLE_MAP_ENTRY(outline_style,                 OutlineStyle),
    COMPUTED_STYLE_MAP_ENTRY(outline_width,                 OutlineWidth),
    COMPUTED_STYLE_MAP_ENTRY(overflow,                      Overflow),
    COMPUTED_STYLE_MAP_ENTRY(overflow_x,                    OverflowX),
    COMPUTED_STYLE_MAP_ENTRY(overflow_y,                    OverflowY),
    //// COMPUTED_STYLE_MAP_ENTRY(padding,                  Padding),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(padding_bottom,         PaddingBottom),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(padding_left,           PaddingLeft),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(padding_right,          PaddingRight),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(padding_top,            PaddingTop),
    // COMPUTED_STYLE_MAP_ENTRY(page,                       Page),
    COMPUTED_STYLE_MAP_ENTRY(page_break_after,              PageBreakAfter),
    COMPUTED_STYLE_MAP_ENTRY(page_break_before,             PageBreakBefore),
    // COMPUTED_STYLE_MAP_ENTRY(page_break_inside,          PageBreakInside),
    COMPUTED_STYLE_MAP_ENTRY(perspective,                   Perspective),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(perspective_origin,     PerspectiveOrigin),
    COMPUTED_STYLE_MAP_ENTRY(pointer_events,                PointerEvents),
    COMPUTED_STYLE_MAP_ENTRY(position,                      Position),
    COMPUTED_STYLE_MAP_ENTRY(quotes,                        Quotes),
    COMPUTED_STYLE_MAP_ENTRY(resize,                        Resize),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(right,                  Right),
    //// COMPUTED_STYLE_MAP_ENTRY(size,                     Size),
    COMPUTED_STYLE_MAP_ENTRY(table_layout,                  TableLayout),
    COMPUTED_STYLE_MAP_ENTRY(text_align,                    TextAlign),
    COMPUTED_STYLE_MAP_ENTRY(text_decoration,               TextDecoration),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(text_indent,            TextIndent),
    COMPUTED_STYLE_MAP_ENTRY(text_overflow,                 TextOverflow),
    COMPUTED_STYLE_MAP_ENTRY(text_shadow,                   TextShadow),
    COMPUTED_STYLE_MAP_ENTRY(text_transform,                TextTransform),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(top,                    Top),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(transform,              Transform),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(transform_origin,       TransformOrigin),
    COMPUTED_STYLE_MAP_ENTRY(transform_style,               TransformStyle),
    //// COMPUTED_STYLE_MAP_ENTRY(transition,               Transition),
    COMPUTED_STYLE_MAP_ENTRY(transition_delay,              TransitionDelay),
    COMPUTED_STYLE_MAP_ENTRY(transition_duration,           TransitionDuration),
    COMPUTED_STYLE_MAP_ENTRY(transition_property,           TransitionProperty),
    COMPUTED_STYLE_MAP_ENTRY(transition_timing_function,    TransitionTimingFunction),
    COMPUTED_STYLE_MAP_ENTRY(unicode_bidi,                  UnicodeBidi),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(vertical_align,         VerticalAlign),
    COMPUTED_STYLE_MAP_ENTRY(visibility,                    Visibility),
    COMPUTED_STYLE_MAP_ENTRY(white_space,                   WhiteSpace),
    // COMPUTED_STYLE_MAP_ENTRY(widows,                     Widows),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(width,                  Width),
    COMPUTED_STYLE_MAP_ENTRY(word_break,                    WordBreak),
    COMPUTED_STYLE_MAP_ENTRY(word_spacing,                  WordSpacing),
    COMPUTED_STYLE_MAP_ENTRY(word_wrap,                     WordWrap),
    COMPUTED_STYLE_MAP_ENTRY(z_index,                       ZIndex),

    /* ******************************* *\
     * Implementations of -moz- styles *
    \* ******************************* */

#ifdef MOZ_FLEXBOX
    COMPUTED_STYLE_MAP_ENTRY(align_items,                   AlignItems),
    COMPUTED_STYLE_MAP_ENTRY(align_self,                    AlignSelf),
#endif // MOZ_FLEXBOX
    COMPUTED_STYLE_MAP_ENTRY(appearance,                    Appearance),
    COMPUTED_STYLE_MAP_ENTRY(_moz_background_inline_policy, BackgroundInlinePolicy),
    COMPUTED_STYLE_MAP_ENTRY(binding,                       Binding),
    COMPUTED_STYLE_MAP_ENTRY(border_bottom_colors,          BorderBottomColors),
    COMPUTED_STYLE_MAP_ENTRY(border_left_colors,            BorderLeftColors),
    COMPUTED_STYLE_MAP_ENTRY(border_right_colors,           BorderRightColors),
    COMPUTED_STYLE_MAP_ENTRY(border_top_colors,             BorderTopColors),
    COMPUTED_STYLE_MAP_ENTRY(box_align,                     BoxAlign),
    COMPUTED_STYLE_MAP_ENTRY(box_direction,                 BoxDirection),
    COMPUTED_STYLE_MAP_ENTRY(box_flex,                      BoxFlex),
    COMPUTED_STYLE_MAP_ENTRY(box_ordinal_group,             BoxOrdinalGroup),
    COMPUTED_STYLE_MAP_ENTRY(box_orient,                    BoxOrient),
    COMPUTED_STYLE_MAP_ENTRY(box_pack,                      BoxPack),
    COMPUTED_STYLE_MAP_ENTRY(box_sizing,                    BoxSizing),
    COMPUTED_STYLE_MAP_ENTRY(_moz_column_count,             ColumnCount),
    COMPUTED_STYLE_MAP_ENTRY(_moz_column_gap,               ColumnGap),
    //// COMPUTED_STYLE_MAP_ENTRY(_moz_column_rule,         ColumnRule),
    COMPUTED_STYLE_MAP_ENTRY(_moz_column_rule_color,        ColumnRuleColor),
    COMPUTED_STYLE_MAP_ENTRY(_moz_column_rule_style,        ColumnRuleStyle),
    COMPUTED_STYLE_MAP_ENTRY(_moz_column_rule_width,        ColumnRuleWidth),
    COMPUTED_STYLE_MAP_ENTRY(_moz_column_width,             ColumnWidth),
#ifdef MOZ_FLEXBOX
    COMPUTED_STYLE_MAP_ENTRY(flex_basis,                    FlexBasis),
    COMPUTED_STYLE_MAP_ENTRY(flex_direction,                FlexDirection),
    COMPUTED_STYLE_MAP_ENTRY(flex_grow,                     FlexGrow),
    COMPUTED_STYLE_MAP_ENTRY(flex_shrink,                   FlexShrink),
#endif // MOZ_FLEXBOX
    COMPUTED_STYLE_MAP_ENTRY(float_edge,                    FloatEdge),
    COMPUTED_STYLE_MAP_ENTRY(font_feature_settings,         FontFeatureSettings),
    COMPUTED_STYLE_MAP_ENTRY(font_language_override,        FontLanguageOverride),
    COMPUTED_STYLE_MAP_ENTRY(force_broken_image_icon,       ForceBrokenImageIcon),
    COMPUTED_STYLE_MAP_ENTRY(hyphens,                       Hyphens),
    COMPUTED_STYLE_MAP_ENTRY(image_region,                  ImageRegion),
#ifdef MOZ_FLEXBOX
    COMPUTED_STYLE_MAP_ENTRY(justify_content,               JustifyContent),
    COMPUTED_STYLE_MAP_ENTRY(order,                         Order),
#endif // MOZ_FLEXBOX
    COMPUTED_STYLE_MAP_ENTRY(orient,                        Orient),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(_moz_outline_radius_bottomLeft, OutlineRadiusBottomLeft),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(_moz_outline_radius_bottomRight,OutlineRadiusBottomRight),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(_moz_outline_radius_topLeft,    OutlineRadiusTopLeft),
    COMPUTED_STYLE_MAP_ENTRY_LAYOUT(_moz_outline_radius_topRight,   OutlineRadiusTopRight),
    COMPUTED_STYLE_MAP_ENTRY(stack_sizing,                  StackSizing),
    COMPUTED_STYLE_MAP_ENTRY(_moz_tab_size,                 TabSize),
    COMPUTED_STYLE_MAP_ENTRY(text_align_last,               TextAlignLast),
    COMPUTED_STYLE_MAP_ENTRY(text_blink,                    MozTextBlink),
    COMPUTED_STYLE_MAP_ENTRY(text_decoration_color,         TextDecorationColor),
    COMPUTED_STYLE_MAP_ENTRY(text_decoration_line,          TextDecorationLine),
    COMPUTED_STYLE_MAP_ENTRY(text_decoration_style,         TextDecorationStyle),
    COMPUTED_STYLE_MAP_ENTRY(text_size_adjust,              TextSizeAdjust),
    COMPUTED_STYLE_MAP_ENTRY(user_focus,                    UserFocus),
    COMPUTED_STYLE_MAP_ENTRY(user_input,                    UserInput),
    COMPUTED_STYLE_MAP_ENTRY(user_modify,                   UserModify),
    COMPUTED_STYLE_MAP_ENTRY(user_select,                   UserSelect),
    COMPUTED_STYLE_MAP_ENTRY(_moz_window_shadow,            WindowShadow),

    /* ***************************** *\
     * Implementations of SVG styles *
    \* ***************************** */

    COMPUTED_STYLE_MAP_ENTRY(clip_path,                     ClipPath),
    COMPUTED_STYLE_MAP_ENTRY(clip_rule,                     ClipRule),
    COMPUTED_STYLE_MAP_ENTRY(color_interpolation,           ColorInterpolation),
    COMPUTED_STYLE_MAP_ENTRY(color_interpolation_filters,   ColorInterpolationFilters),
    COMPUTED_STYLE_MAP_ENTRY(dominant_baseline,             DominantBaseline),
    COMPUTED_STYLE_MAP_ENTRY(fill,                          Fill),
    COMPUTED_STYLE_MAP_ENTRY(fill_opacity,                  FillOpacity),
    COMPUTED_STYLE_MAP_ENTRY(fill_rule,                     FillRule),
    COMPUTED_STYLE_MAP_ENTRY(filter,                        Filter),
    COMPUTED_STYLE_MAP_ENTRY(flood_color,                   FloodColor),
    COMPUTED_STYLE_MAP_ENTRY(flood_opacity,                 FloodOpacity),
    COMPUTED_STYLE_MAP_ENTRY(image_rendering,               ImageRendering),
    COMPUTED_STYLE_MAP_ENTRY(lighting_color,                LightingColor),
    COMPUTED_STYLE_MAP_ENTRY(marker_end,                    MarkerEnd),
    COMPUTED_STYLE_MAP_ENTRY(marker_mid,                    MarkerMid),
    COMPUTED_STYLE_MAP_ENTRY(marker_start,                  MarkerStart),
    COMPUTED_STYLE_MAP_ENTRY(mask,                          Mask),
    COMPUTED_STYLE_MAP_ENTRY(shape_rendering,               ShapeRendering),
    COMPUTED_STYLE_MAP_ENTRY(stop_color,                    StopColor),
    COMPUTED_STYLE_MAP_ENTRY(stop_opacity,                  StopOpacity),
    COMPUTED_STYLE_MAP_ENTRY(stroke,                        Stroke),
    COMPUTED_STYLE_MAP_ENTRY(stroke_dasharray,              StrokeDasharray),
    COMPUTED_STYLE_MAP_ENTRY(stroke_dashoffset,             StrokeDashoffset),
    COMPUTED_STYLE_MAP_ENTRY(stroke_linecap,                StrokeLinecap),
    COMPUTED_STYLE_MAP_ENTRY(stroke_linejoin,               StrokeLinejoin),
    COMPUTED_STYLE_MAP_ENTRY(stroke_miterlimit,             StrokeMiterlimit),
    COMPUTED_STYLE_MAP_ENTRY(stroke_opacity,                StrokeOpacity),
    COMPUTED_STYLE_MAP_ENTRY(stroke_width,                  StrokeWidth),
    COMPUTED_STYLE_MAP_ENTRY(text_anchor,                   TextAnchor),
    COMPUTED_STYLE_MAP_ENTRY(text_rendering,                TextRendering),
    COMPUTED_STYLE_MAP_ENTRY(vector_effect,                 VectorEffect)

  };

  *aLength = ArrayLength(map);

  return map;
}

