/* vim: set sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_OS2_SURFACE_H
#define GFX_OS2_SURFACE_H

#include "gfxASurface.h"

class gfxOS2Surface : public gfxASurface {

public:
    // constructor for an os2Image surface -
    // the surface is only used by Cairo and not by PM
    gfxOS2Surface(const gfxIntSize& aSize,
                  gfxASurface::gfxImageFormat aFormat);

    // constructor for an os2Window surface
    gfxOS2Surface(HWND aWnd);

    // constructor for an os2Print surface
    gfxOS2Surface(HDC aDC, const gfxIntSize& aSize, int aPreview);

    // constructor for an as-yet-unwrapped os2Image surface
    gfxOS2Surface(cairo_surface_t *csurf);

    virtual ~gfxOS2Surface();

    virtual const gfxIntSize GetSize() const { return mSize; }

    // invoked on os2Window surfaces to update the screen -
    // it uses the HPS provided by WinBeginPaint()
    void Refresh(RECTL *aRect, int aCount, HPS aPS);

    // invoked on os2Window surfaces to adjust the cairo_os2_surface's
    // size when the window's size changes
    int Resize(const gfxIntSize& aSize);

    // invoked on os2Print surfaces to get the associated PS
    HPS GetPS();

    // invoked on os2Print surfaces to print the current page
    virtual nsresult EndPage();

    // enable/disable DIVE (direct access to the video framebuffer)
    static bool EnableDIVE(bool aEnable, bool aHidePointer);

    // returns special rendering flags for os2Print surfaces
    virtual int32_t GetDefaultContextFlags() const;

private:
    typedef enum {
        os2Null     = 0,
        os2Image    = 1,
        os2Window   = 2,
        os2Print    = 3
    } os2SurfaceType;

    HWND           mWnd;      // window associated with the surface
    HDC            mDC;       // device context
    HPS            mPS;       // presentation space associated with HDC
    gfxIntSize     mSize;     // current size of the surface
    os2SurfaceType mSurfType; // type of surface
};

#endif /* GFX_OS2_SURFACE_H */
