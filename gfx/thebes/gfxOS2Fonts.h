/* vim: set sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_OS2_FONTS_H
#define GFX_OS2_FONTS_H

#include <cairo.h>
#include "gfxTypes.h"
#include "gfxTextRun.h"
#include "gfxMatrix.h"
#include "nsDataHashtable.h"

class gfxOS2FontEntry : public gfxFontEntry {
public:
    gfxOS2FontEntry(const nsAString& aName) : gfxFontEntry(aName) {}
    ~gfxOS2FontEntry() {}

    // no-op
    gfxFont *CreateFontInstance(const gfxFontStyle *aFontStyle, bool aNeedsBold) {
        return nullptr;
    }

    // no-op
    nsresult CopyFontTable(uint32_t aTableTag,
                           FallibleTArray<uint8_t>& aBuffer) {
        return NS_ERROR_FAILURE;
    }

    // no-op
    nsresult ReadCMAP(FontInfoData *aFontInfoData)
    {
        mCharacterMap = new gfxCharacterMap();
        return NS_OK;
    }
};

class gfxOS2Font : public gfxFont {
public:
    gfxOS2Font(gfxOS2FontEntry *aFontEntry, const gfxFontStyle *aFontStyle);
    virtual ~gfxOS2Font();

    virtual const gfxFont::Metrics& GetHorizontalMetrics() override;
    cairo_font_face_t *CairoFontFace();
    cairo_scaled_font_t *CairoScaledFont();

    // Get the glyphID of a space
    virtual uint32_t GetSpaceGlyph() override {
        if (!mMetrics)
            GetHorizontalMetrics();
        return mSpaceGlyph;
    }

    static already_AddRefed<gfxOS2Font> GetOrMakeFont(const nsAString& aName,
                                                      const gfxFontStyle *aStyle);

protected:
    virtual bool SetupCairoFont(gfxContext *aContext);

    virtual FontType GetType() const override { return FONT_TYPE_OS2; }

private:
    cairo_font_face_t *mFontFace;
    Metrics *mMetrics;
    gfxFloat mAdjustedSize;
    uint32_t mSpaceGlyph;
    int mHinting;
    bool mAntialias;
};


class gfxOS2FontGroup : public gfxFontGroup {
public:
    gfxOS2FontGroup(const mozilla::FontFamilyList& aFontFamilyList,
                    const gfxFontStyle* aStyle, gfxUserFontSet *aUserFontSet);
    virtual ~gfxOS2FontGroup();

    virtual gfxFontGroup *Copy(const gfxFontStyle *aStyle);

    // create and initialize the textRun using FreeType font
    virtual gfxTextRun *MakeTextRun(const char16_t* aString, uint32_t aLength,
                                    const Parameters* aParams, uint32_t aFlags,
                                    gfxMissingFontRecorder *aMFR);
    virtual gfxTextRun *MakeTextRun(const uint8_t* aString, uint32_t aLength,
                                    const Parameters* aParams, uint32_t aFlags,
                                    gfxMissingFontRecorder *aMFR);

    gfxOS2Font *GetOS2FontAt(int32_t i) {
#ifdef DEBUG_thebes_2
        printf("gfxOS2FontGroup[%#x]::GetOS2FontAt(%d), %#x, %#x\n",
               (unsigned)this, i, (unsigned)&mFonts, (unsigned)mFonts[i].Font());
#endif
        return static_cast<gfxOS2Font*>(mFonts[i].Font());
    }

protected:
    void InitTextRun(gfxTextRun *aTextRun, const uint8_t *aUTF8Text,
                     uint32_t aUTF8Length, uint32_t aUTF8HeaderLength, uint16_t orientation,
                     gfxMissingFontRecorder *aMFR);
    void CreateGlyphRunsFT(gfxTextRun *aTextRun, const uint8_t *aUTF8,
                           uint32_t aUTF8Length, uint16_t orientation,
                           gfxMissingFontRecorder * aMFR);

private:
    bool mEnableKerning;
};

#endif /* GFX_OS2_FONTS_H */
