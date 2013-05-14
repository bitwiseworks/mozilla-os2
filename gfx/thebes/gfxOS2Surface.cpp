/* vim: set sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gfxOS2Surface.h"
#include <cairo-os2.h>

// a rough approximation of the memory used
// by gfxOS2Surface and cairo_os2_surface
#define OS2_OVERHEAD  sizeof(gfxOS2Surface) + 128

/**********************************************************************
 * class gfxOS2Surface
 **********************************************************************/

gfxOS2Surface::gfxOS2Surface(const gfxIntSize& aSize,
                             gfxASurface::gfxImageFormat aFormat)
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

gfxOS2Surface::gfxOS2Surface(HDC aDC, const gfxIntSize& aSize)
    : mWnd(0), mDC(aDC), mPS(0), mSize(aSize), mSurfType(os2Print)
{
    // Create a PS using the same page size as the device.
    SIZEL sizel = { 0, 0 };
    mPS = GpiCreatePS(0, mDC, &sizel, PU_PELS | GPIA_ASSOC | GPIT_MICRO);
    NS_ASSERTION(mPS != GPI_ERROR, "Could not create PS on print DC!");

    // Create a cairo surface for the PS associated with the printer DC.
    // Since we only "print" to PDF, create a null surface that has no bitmap.
    cairo_surface_t* surf;
    surf = cairo_os2_surface_create_null_surface(mPS, mSize.width, mSize.height);
    Init(surf);

    RecordMemoryUsed(OS2_OVERHEAD);
}

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
       cairo_os2_surface_paint_window((cairo_os2_surface_t*)CairoSurface(),
                                       (aPS ? aPS : mPS), aRect, aCount);
}

int gfxOS2Surface::Resize(const gfxIntSize& aSize)
{
    if (mSurfType != os2Window)
        return 0;

    int status = cairo_os2_surface_set_size((cairo_os2_surface_t*)CairoSurface(),
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
    // Creating an HPS on-the-fly should never be needed because GetPS()
    // is only called for printing surfaces & mPS should only be null for
    // window surfaces.  It would be a bug if Cairo had an HPS but Thebes
    // didn't, but we'll check anyway to avoid leakage.  As a last resort,
    // if this is a window surface we'll create one & hang on to it.
    if (!mPS) {
        cairo_os2_surface_get_hps((cairo_os2_surface_t*)CairoSurface(), &mPS);
        if (!mPS && mSurfType == os2Window && mWnd) {
            mPS = WinGetPS(mWnd);
            cairo_os2_surface_set_hps((cairo_os2_surface_t*)CairoSurface(), mPS);
        }
    }

    return mPS;
}

//static
bool gfxOS2Surface::EnableDIVE(PRBool aEnable, PRBool aHidePointer)
{
    // enable/disable DIVE (direct access to the video framebuffer)
    return cairo_os2_surface_enable_dive(aEnable, aHidePointer);
}
