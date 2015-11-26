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

#include "gfxFontconfigUtils.h"
#include "gfxPangoFonts.h"
#include "gfx2DGlue.h"

/**********************************************************************
 * class gfxOS2Platform
 **********************************************************************/
gfxFontconfigUtils *gfxOS2Platform::sFontconfigUtils = nullptr;

gfxOS2Platform::gfxOS2Platform()
{
    cairo_os2_init();

    if (!sFontconfigUtils) {
        sFontconfigUtils = gfxFontconfigUtils::GetFontconfigUtils();
    }
}

gfxOS2Platform::~gfxOS2Platform()
{
    gfxFontconfigUtils::Shutdown();
    sFontconfigUtils = nullptr;

    // Clean up cairo_os2 sruff.
    cairo_os2_surface_enable_dive(false, false);
    cairo_os2_fini();
}

already_AddRefed<gfxASurface>
gfxOS2Platform::CreateOffscreenSurface(const IntSize & aSize,
                                       gfxContentType contentType)
{
    gfxImageFormat format =
        OptimalFormatForContent(contentType);
    int stride =
        cairo_format_stride_for_width(static_cast<cairo_format_t>(format),
                                      aSize.width);

    // To avoid memory fragmentation, return a standard image surface
    // for small images (32x32x4 or 64x64x1).  Their bitmaps will be
    // be allocated from libc's heap rather than system memory.

    nsRefPtr<gfxASurface> surf;
    if (stride * aSize.height <= 4096) {
        surf = new gfxImageSurface(ThebesIntSize(aSize), format);
    } else {
        surf = new gfxOS2Surface(ThebesIntSize(aSize), format);
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
    return sFontconfigUtils->GetFontList(aLangGroup, aGenericFamily,
                                         aListOfFonts);
}

nsresult gfxOS2Platform::UpdateFontList()
{
    return sFontconfigUtils->UpdateFontList();
}

nsresult
gfxOS2Platform::GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName)
{
    return sFontconfigUtils->GetStandardFamilyName(aFontName, aFamilyName);
}

gfxFontGroup *
gfxOS2Platform::CreateFontGroup(const mozilla::FontFamilyList& aFontFamilyList,
                                const gfxFontStyle *aStyle,
                                gfxUserFontSet *aUserFontSet)
{
    return new gfxPangoFontGroup(aFontFamilyList, aStyle, aUserFontSet);
}
