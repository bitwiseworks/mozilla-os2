/* vim: set sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_OS2_PLATFORM_H
#define GFX_OS2_PLATFORM_H

#include "gfxPlatform.h"
#include "gfxFontUtils.h"
#include "nsTArray.h"

class gfxFontconfigUtils;

class gfxOS2Platform : public gfxPlatform {

public:
    gfxOS2Platform();
    virtual ~gfxOS2Platform();

    static gfxOS2Platform *GetPlatform() {
        return static_cast<gfxOS2Platform*>(gfxPlatform::GetPlatform());
    }

    virtual already_AddRefed<gfxASurface>
      CreateOffscreenSurface(const IntSize& aSize,
                             gfxImageFormat aFormat) override;

    virtual nsresult GetFontList(nsIAtom *aLangGroup,
                                 const nsACString& aGenericFamily,
                                 nsTArray<nsString>& aListOfFonts) override;

    virtual nsresult UpdateFontList() override;

    virtual gfxPlatformFontList* CreatePlatformFontList() override;

    virtual nsresult GetStandardFamilyName(const nsAString& aFontName,
                                           nsAString& aFamilyName) override;

    gfxFontGroup*
    CreateFontGroup(const mozilla::FontFamilyList& aFontFamilyList,
                    const gfxFontStyle* aStyle,
                    gfxTextPerfMetrics* aTextPerf,
                    gfxUserFontSet* aUserFontSet,
                    gfxFloat aDevToCssSize) override;

    /**
     * Look up a local platform font using the full font face name (needed to
     * support @font-face src local() )
     */
    virtual gfxFontEntry* LookupLocalFont(const nsAString& aFontName,
                                          uint16_t aWeight,
                                          int16_t aStretch,
                                          uint8_t aStyle) override;

    /**
     * Activate a platform font (needed to support @font-face src url() )
     *
     */
    virtual gfxFontEntry* MakePlatformFont(const nsAString& aFontName,
                                           uint16_t aWeight,
                                           int16_t aStretch,
                                           uint8_t aStyle,
                                           const uint8_t* aFontData,
                                           uint32_t aLength) override;

    /**
     * Check whether format is supported on a platform or not (if unclear,
     * returns true).
     */
    virtual bool IsFontFormatSupported(nsIURI *aFontURI,
                                       uint32_t aFormatFlags) override;


    static bool UseFcFontList() { return sUseFcFontList; }

    void FontsPrefsChanged(const char *aPref) override;

    // maximum number of fonts to substitute for a generic
    uint32_t MaxGenericSubstitions();

protected:
    static gfxFontconfigUtils *sFontconfigUtils;

    int8_t mMaxGenericSubstitutions;

private:

    // xxx - this will be removed once the new fontconfig platform font list
    // replaces gfxPangoFontGroup
    static bool sUseFcFontList;
};

#endif /* GFX_OS2_PLATFORM_H */
