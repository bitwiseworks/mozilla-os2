/* vim: set sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define INCL_BASE
#define INCL_PM
#include <os2.h>
#include <cairo-os2.h>
#include "cairo-ft.h" // includes fontconfig.h, too

#include "gfxOS2Platform.h"
#include "gfxOS2Surface.h"
#include "gfxImageSurface.h"
#include "nsTArray.h"
#include "nsServiceManagerUtils.h"

#include "gfxFcPlatformFontList.h"
#include "gfxFontconfigUtils.h"
#include "gfxFontconfigFonts.h"
#include "gfx2DGlue.h"
#include "gfxUserFontSet.h"
#include "gfxPrefs.h"

#include "mozilla/Preferences.h"

#define GFX_PREF_MAX_GENERIC_SUBSTITUTIONS "gfx.font_rendering.fontconfig.max_generic_substitutions"

using namespace mozilla;

gfxFontconfigUtils *gfxOS2Platform::sFontconfigUtils = nullptr;

bool gfxOS2Platform::sUseFcFontList = false;

gfxOS2Platform::gfxOS2Platform()
{
    cairo_os2_init();

    sUseFcFontList = mozilla::Preferences::GetBool("gfx.font_rendering.fontconfig.fontlist.enabled");
    if (!sUseFcFontList && !sFontconfigUtils) {
        sFontconfigUtils = gfxFontconfigUtils::GetFontconfigUtils();
    }

    mMaxGenericSubstitutions = UNINITIALIZED_VALUE;
}

gfxOS2Platform::~gfxOS2Platform()
{
    if (!sUseFcFontList) {
        gfxFontconfigUtils::Shutdown();
        sFontconfigUtils = nullptr;
    }

    // Clean up cairo_os2 sruff.
    cairo_os2_surface_enable_dive(false, false);
    cairo_os2_fini();
}

already_AddRefed<gfxASurface>
gfxOS2Platform::CreateOffscreenSurface(const IntSize & aSize,
                                       gfxImageFormat aFormat)
{
    int stride =
        cairo_format_stride_for_width(static_cast<cairo_format_t>(aFormat),
                                      aSize.width);

    // To avoid memory fragmentation, return a standard image surface
    // for small images (32x32x4 or 64x64x1).  Their bitmaps will be
    // be allocated from libc's heap rather than system memory.

    RefPtr<gfxASurface> surf;
    if (stride * aSize.height <= 4096) {
        surf = new gfxImageSurface(aSize, aFormat);
    } else {
        surf = new gfxOS2Surface(aSize, aFormat);
    }

    return surf.forget();
}

nsresult
gfxOS2Platform::GetFontList(nsIAtom *aLangGroup,
                            const nsACString& aGenericFamily,
                            nsTArray<nsString>& aListOfFonts)
{
#ifdef DEBUG_thebes
    const char *langgroup = "(null)";
    if (aLangGroup) {
        aLangGroup->GetUTF8String(&langgroup);
    }
    char *family = ToNewCString(aGenericFamily);
    printf("gfxOS2Platform::GetFontList(%s, %s, ..)\n",
           langgroup, family);
    free(family);
#endif
    if (sUseFcFontList) {
        gfxPlatformFontList::PlatformFontList()->GetFontList(aLangGroup,
                                                             aGenericFamily,
                                                             aListOfFonts);
        return NS_OK;
    }

    return sFontconfigUtils->GetFontList(aLangGroup,
                                         aGenericFamily,
                                         aListOfFonts);
}

nsresult gfxOS2Platform::UpdateFontList()
{
    if (sUseFcFontList) {
        gfxPlatformFontList::PlatformFontList()->UpdateFontList();
        return NS_OK;
    }

    return sFontconfigUtils->UpdateFontList();
}

gfxPlatformFontList*
gfxOS2Platform::CreatePlatformFontList()
{
    gfxPlatformFontList* list = new gfxFcPlatformFontList();
    if (NS_SUCCEEDED(list->InitFontList())) {
        return list;
    }
    gfxPlatformFontList::Shutdown();
    return nullptr;
}

nsresult
gfxOS2Platform::GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName)
{
    if (sUseFcFontList) {
        gfxPlatformFontList::PlatformFontList()->
            GetStandardFamilyName(aFontName, aFamilyName);
        return NS_OK;
    }

    return sFontconfigUtils->GetStandardFamilyName(aFontName, aFamilyName);
}

gfxFontGroup *
gfxOS2Platform::CreateFontGroup(const mozilla::FontFamilyList& aFontFamilyList,
                                const gfxFontStyle* aStyle,
                                gfxTextPerfMetrics* aTextPerf,
                                gfxUserFontSet* aUserFontSet,
                                gfxFloat aDevToCssSize)
{
    if (sUseFcFontList) {
        return new gfxFontGroup(aFontFamilyList, aStyle, aTextPerf,
                                aUserFontSet, aDevToCssSize);
    }

    return new gfxPangoFontGroup(aFontFamilyList, aStyle,
                                 aUserFontSet, aDevToCssSize);
}

gfxFontEntry*
gfxOS2Platform::LookupLocalFont(const nsAString& aFontName,
                                uint16_t aWeight,
                                int16_t aStretch,
                                uint8_t aStyle)
{
    if (sUseFcFontList) {
        gfxPlatformFontList* pfl = gfxPlatformFontList::PlatformFontList();
        return pfl->LookupLocalFont(aFontName, aWeight, aStretch,
                                    aStyle);
    }

    return gfxPangoFontGroup::NewFontEntry(aFontName, aWeight,
                                           aStretch, aStyle);
}

gfxFontEntry*
gfxOS2Platform::MakePlatformFont(const nsAString& aFontName,
                                 uint16_t aWeight,
                                 int16_t aStretch,
                                 uint8_t aStyle,
                                 const uint8_t* aFontData,
                                 uint32_t aLength)
{
    if (sUseFcFontList) {
        gfxPlatformFontList* pfl = gfxPlatformFontList::PlatformFontList();
        return pfl->MakePlatformFont(aFontName, aWeight, aStretch,
                                     aStyle, aFontData, aLength);
    }

    // passing ownership of the font data to the new font entry
    return gfxPangoFontGroup::NewFontEntry(aFontName, aWeight,
                                           aStretch, aStyle,
                                           aFontData, aLength);
}

bool
gfxOS2Platform::IsFontFormatSupported(nsIURI *aFontURI, uint32_t aFormatFlags)
{
    // check for strange format flags
    NS_ASSERTION(!(aFormatFlags & gfxUserFontSet::FLAG_FORMAT_NOT_USED),
                 "strange font format hint set");

    // accept supported formats
    // Pango doesn't apply features from AAT TrueType extensions.
    // Assume that if this is the only SFNT format specified,
    // then AAT extensions are required for complex script support.
    if (aFormatFlags & gfxUserFontSet::FLAG_FORMATS_COMMON) {
        return true;
    }

    // reject all other formats, known and unknown
    if (aFormatFlags != 0) {
        return false;
    }

    // no format hint set, need to look at data
    return true;
}

void gfxOS2Platform::FontsPrefsChanged(const char *aPref)
{
    // only checking for generic substitions, pass other changes up
    if (strcmp(GFX_PREF_MAX_GENERIC_SUBSTITUTIONS, aPref)) {
        gfxPlatform::FontsPrefsChanged(aPref);
        return;
    }

    mMaxGenericSubstitutions = UNINITIALIZED_VALUE;
    if (sUseFcFontList) {
        gfxFcPlatformFontList* pfl = gfxFcPlatformFontList::PlatformFontList();
        pfl->ClearGenericMappings();
        FlushFontAndWordCaches();
    }
}

uint32_t gfxOS2Platform::MaxGenericSubstitions()
{
    if (mMaxGenericSubstitutions == UNINITIALIZED_VALUE) {
        mMaxGenericSubstitutions =
            Preferences::GetInt(GFX_PREF_MAX_GENERIC_SUBSTITUTIONS, 3);
        if (mMaxGenericSubstitutions < 0) {
            mMaxGenericSubstitutions = 3;
        }
    }

    return uint32_t(mMaxGenericSubstitutions);
}
