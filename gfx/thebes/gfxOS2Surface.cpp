/* vim: set sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#define INCL_BASE
#define INCL_PM
#include <os2.h>
#include "gfxOS2Surface.h"
#include "gfxContext.h"
#include <cairo-os2.h>

#include <stdio.h>

// a rough approximation of the memory used
// by gfxOS2Surface and cairo_os2_surface
#define OS2_OVERHEAD  sizeof(gfxOS2Surface) + 128

/**********************************************************************
 * class gfxOS2Surface
 **********************************************************************/

gfxOS2Surface::gfxOS2Surface(const mozilla::gfx::IntSize& aSize,
                             gfxImageFormat aFormat)
    : mWnd(0),  mDC(0), mPS(0), mSize(aSize), mSurfType(os2Image)
{
    if (!CheckSurfaceSize(aSize))
        return;

    cairo_surface_t *surf =
        cairo_os2_surface_create((cairo_format_t)aFormat,
                                 mSize.width, mSize.height);
    Init(surf);

    RecordMemoryUsed(mSize.width * mSize.height * 4 + OS2_OVERHEAD);
}

gfxOS2Surface::gfxOS2Surface(HWND aWnd)
    : mWnd(aWnd), mDC(0), mPS(0), mSize(0,0), mSurfType(os2Window)
{
    RECTL rectl;
    WinQueryWindowRect(aWnd, &rectl);

    mSize.width = rectl.xRight - rectl.xLeft;
    mSize.height = rectl.yTop - rectl.yBottom;
    // If necessary fake a minimal surface area to let
    // cairo_os2_surface_create() return something.
    if (mSize.width == 0)
        mSize.width = 1;
    if (mSize.height == 0)
        mSize.height = 1;

    cairo_surface_t *surf =
        cairo_os2_surface_create_for_window(mWnd, mSize.width, mSize.height);

    Init(surf);
    RecordMemoryUsed(mSize.width * mSize.height * 4 + OS2_OVERHEAD);
}

#if 0 // This is temporariy disabled, see #171.
gfxOS2Surface::gfxOS2Surface(HDC aDC, const mozilla::gfx::IntSize& aSize, int aPreview)
    : mWnd(0), mDC(aDC), mPS(0), mSize(aSize), mSurfType(os2Print)
{
    // Create a PS using the same page size as the device.
    SIZEL sizel = { 0, 0 };
    mPS = GpiCreatePS(0, mDC, &sizel, PU_PELS | GPIA_ASSOC |
                      (aPreview ? GPIT_MICRO : GPIT_NORMAL));
    NS_ASSERTION(mPS != GPI_ERROR, "Could not create PS on print DC!");

    printf("gfxOS2Surface for print  - DC= %lx PS= %lx w= %d h= %d preview= %d\n",
           mDC, mPS, mSize.width, mSize.height, aPreview);

    // Create a cairo surface for the PS associated with the printer DC.
    // For print preview, create a null surface that can be queried but
    // generates no output.  Otherwise, create a printing surface that
    // uses GPI functions to render the output.

    cairo_surface_t* surf;
    if (aPreview)
        surf = cairo_os2_surface_create_null_surface(mPS, mSize.width, mSize.height);
    else
        surf = cairo_os2_printing_surface_create(mPS, mSize.width, mSize.height);

    Init(surf);

    // Cairo allocates temporary buffers when it converts images from
    // BGR4 to BGR3 but there's no way to determine their size.
    RecordMemoryUsed(OS2_OVERHEAD);
}
#endif

gfxOS2Surface::gfxOS2Surface(cairo_surface_t *csurf)
    : mWnd(0), mDC(0), mPS(0), mSize(0,0), mSurfType(os2Image)
{
    // don't init any member data because mPS and mDC probably
    // belong to someone else, and mSize isn't readily accessible
    Init(csurf, true);
}

gfxOS2Surface::~gfxOS2Surface()
{
    switch (mSurfType) {

        case os2Window:
            if (mPS)
                WinReleasePS(mPS);
            break;

        case os2Print:
            printf("~gfxOS2Surface for print - DC= %lx PS= %lx w= %d h= %d\n",
                   mDC, mPS, mSize.width, mSize.height);

            if (mPS)
                GpiDestroyPS(mPS);
            if (mDC)
                DevCloseDC(mDC);

        case os2Image:
        default:
            break;
     }

    RecordMemoryFreed();
}

void gfxOS2Surface::Refresh(RECTL *aRect, int aCount, HPS aPS)
{
    if (mSurfType == os2Window)
       cairo_os2_surface_paint_window(CairoSurface(),
                                      (aPS ? aPS : mPS), aRect, aCount);
}

int gfxOS2Surface::Resize(const mozilla::gfx::IntSize& aSize)
{
    if (mSurfType != os2Window)
        return 0;

    int status = cairo_os2_surface_set_size(CairoSurface(),
                                            aSize.width, aSize.height, FALSE);
    if (status == CAIRO_STATUS_SUCCESS) {
        RecordMemoryUsed((aSize.width * aSize.height * 4) -
                         (mSize.width * mSize.height * 4));
        mSize = aSize;
    }

    return status;
}

HPS gfxOS2Surface::GetPS()
{
    printf("gfxOS2Surface::GetPS - mSurfType= %d  mPS= %lx\n",
           mSurfType, mPS);

    // Creating an HPS on-the-fly should never be needed because GetPS()
    // is only called for printing surfaces & mPS should only be null for
    // window surfaces.  It would be a bug if Cairo had an HPS but Thebes
    // didn't, but we'll check anyway to avoid leakage.  As a last resort,
    // if this is a window surface we'll create one & hang on to it.
    if (!mPS) {
        cairo_os2_surface_get_hps(CairoSurface(), &mPS);
        if (!mPS && mSurfType == os2Window && mWnd) {
            mPS = WinGetPS(mWnd);
            cairo_os2_surface_set_hps(CairoSurface(), mPS);
        }
    }

    return mPS;
}

// Currently, this is the only print event we need to deal with in Thebes.
nsresult gfxOS2Surface::EndPage()
{
    printf("gfxOS2Surface::EndPage - mSurfType= %d\n", mSurfType);

    if (mSurfType == os2Print)
      cairo_surface_show_page(CairoSurface());
    else
      NS_WARNING("gfxOS2Surface::EndPage() called on non-printing surface\n");

    return NS_OK;
}

//static
bool gfxOS2Surface::EnableDIVE(bool aEnable, bool aHidePointer)
{
    // enable/disable DIVE (direct access to the video framebuffer)
    return cairo_os2_surface_enable_dive(aEnable, aHidePointer);
}
