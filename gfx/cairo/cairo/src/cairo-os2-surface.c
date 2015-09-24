/* vim: set sw=4 sts=4 et cin: */
/* cairo - a vector graphics library with display and print output
 *
 * Copyright (c) 2005-2006 netlabs.org
 * Copyright (c) 2010-2011 Rich Walsh
 *
 * This library is free software; you can redistribute it and/or
 * modify it either under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (the "LGPL") or, at your option, under the terms of the Mozilla
 * Public License Version 1.1 (the "MPL"). If you do not alter this
 * notice, a recipient may use your version of this file under either
 * the MPL or the LGPL.
 *
 * You should have received a copy of the LGPL along with this library
 * in the file COPYING-LGPL-2.1; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA
 * You should have received a copy of the MPL along with this library
 * in the file COPYING-MPL-1.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
 * OF ANY KIND, either express or implied. See the LGPL or the MPL for
 * the specific language governing rights and limitations.
 *
 * The Original Code is the cairo graphics library.
 *
 * The Initial Developer of the Original Code is
 *     Doodle <doodle@scenergy.dfmk.hu>
 *
 * Contributor(s):
 *     Peter Weilbacher <mozilla@Weilbacher.org>
 *     Rich Walsh <rich@e-vertise.com>
 */


/**
 * cairo_os2_surface:
 *
 * #cairo_os2_surface is a thin wrapper around #cairo_image_surface that
 * supports output to displays and native bitmaps.  It provides OS/2-specific
 * front-end functions and manages memory used by surface bitmaps.  Calls
 * to most backend functions are forwarded to the underlying image surface.
 **/

#define INCL_BASE
#define INCL_PM
#include <os2.h>

#include <dive.h>
#include "cairo-os2-private.h"
#include "cairo-error-private.h"


/**
 *  Forward references to static helper functions
 **/

static cairo_status_t
_cairo_os2_surface_init_image (cairo_os2_surface_t *surface);
static unsigned char *
_cairo_os2_surface_alloc (unsigned int size);
static void
_cairo_os2_surface_free (void *buffer);
static cairo_status_t
_cairo_os2_surface_paint_32bpp (cairo_os2_surface_t *surface,
                                HPS                  hps,
                                RECTL               *src,
                                RECTL               *dst,
                                int                  count);
static cairo_status_t
_cairo_os2_surface_paint_24bpp (cairo_os2_surface_t *surface,
                                HPS                  hps,
                                RECTL               *src,
                                RECTL               *dst,
                                int                  count);
static cairo_status_t
_cairo_os2_surface_paint_dive (cairo_os2_surface_t *surface,
                               HPS                  hps,
                               RECTL               *rect,
                               int                  count);
static void
_cairo_os2_surface_dive_error (void);


/**
 * Module-level data
 **/

static cairo_bool_t   display_use24bpp = FALSE;
static int            dive_status = 0;
static cairo_bool_t   dive_hideptr = TRUE;
static HDIVE          dive_handle = 0;
static int            dive_height;
static int            dive_stride;
static unsigned char *dive_scrnbuf;


/**
 * Support for dynamically loading dive.dll
 **/

#ifdef OS2_DYNAMIC_DIVE
static cairo_bool_t      dive_loaded = FALSE;

static DiveQuery_t       pDiveQueryCaps = 0;
static DiveOpen_t        pDiveOpen = 0;
static DiveClose_t       pDiveClose = 0;
static DiveAcquire_t     pDiveAcquireFrameBuffer = 0;
static DiveDeacquire_t   pDiveDeacquireFrameBuffer = 0;

#define DiveQueryCaps             pDiveQueryCaps
#define DiveOpen                  pDiveOpen
#define DiveClose                 pDiveClose
#define DiveAcquireFrameBuffer    pDiveAcquireFrameBuffer
#define DiveDeacquireFrameBuffer  pDiveDeacquireFrameBuffer

static cairo_bool_t
_cairo_os2_surface_load_dive (void);
#endif


/**
 * Forward reference to the backend structure
 **/

static const
cairo_surface_backend_t cairo_os2_surface_backend;


/**
 *
 * Surface Create functions
 *
 **/

/**
 * cairo_os2_surface_create:
 * @format: a standard cairo_format_t value
 * @width: width of the surface in pixels
 * @height: height of the surface in pixels
 *
 * A generic image surface creation function.
 *
 * Returns: a cairo_os2_surface_t if successful, a nil surface if not.
 *
 * Since: 1.12
 **/

cairo_public cairo_surface_t *
cairo_os2_surface_create (cairo_format_t      format,
                          int                 width,
                          int                 height)
{
    cairo_status_t       status;
    cairo_os2_surface_t *surface;

    if (width < 0 || height < 0)
        return _cairo_surface_create_in_error (CAIRO_STATUS_INVALID_SIZE);

    if (!CAIRO_FORMAT_VALID(format))
        return _cairo_surface_create_in_error (CAIRO_STATUS_INVALID_FORMAT);

    /*
     * Create an OS/2 wrapper for an image surface
     * whose bitmap will be allocated from system memory.
     */
    surface = malloc (sizeof (cairo_os2_surface_t));
    if (unlikely (surface == NULL))
        return _cairo_surface_create_in_error (CAIRO_STATUS_NO_MEMORY);

    memset (surface, 0, sizeof(cairo_os2_surface_t));
    surface->format  = format;
    surface->content = _cairo_content_from_format (surface->format);
    surface->subtype = CAIRO_OS2_SUBTYPE_IMAGE;
    surface->width   = width;
    surface->height  = height;
    surface->stride  = cairo_format_stride_for_width (format, width);

    /*
     * allocate memory for a bitmap then create an image surface.
     */
    status = _cairo_os2_surface_init_image (surface);
    if (_cairo_status_is_error(status)) {
        if (surface->image)
            cairo_surface_destroy (surface->image);
        if (surface->data)
            _cairo_os2_surface_free (surface->data);
        free (surface);
        return _cairo_surface_create_in_error (status);
    }

    _cairo_surface_init (&surface->base,
                         &cairo_os2_surface_backend,
                         NULL, /* device */
                         surface->content);

    return &surface->base;
}


/**
 * cairo_os2_surface_create_for_window:
 * @hwnd: window associated with this surface
 * @width: width of the surface in pixels
 * @height: height of the surface in pixels
 *
 * This creates a destination surface that maps to the entire area of
 * a native window.  Clients can call cairo_os2_surface_set_size() when
 * the window's size changes, and cairo_os2_surface_paint_window()
 * when they want to update the display with the surface's content.
 *
 * If @hwnd is not supplied with this call, it must be set using
 * cairo_os2_surface_set_hwnd() before attempting to display this surface.
 *
 * Returns: a cairo_os2_surface_t if successful, a nil surface if not.
 *
 * Since: 1.12
 **/

cairo_public cairo_surface_t *
cairo_os2_surface_create_for_window (HWND  hwnd,
                                     int   width,
                                     int   height)
{
    cairo_os2_surface_t *surface;

    surface = (cairo_os2_surface_t *)
              cairo_os2_surface_create (CAIRO_FORMAT_ARGB32, width, height);

    if (!_cairo_status_is_error (surface->base.status))
        surface->hwnd = hwnd;

    return &surface->base;
}


/**
 * cairo_os2_surface_create_null_surface:
 * @hps: presentation space handle, typically associated with a
 *   printer's device context
 * @width: width of the surface in pixels
 * @height: height of the surface in pixels
 *
 * This creates a a null surface that has no image surface associated
 * with it.  It is created when the client needs a surface that can be
 * queried for its attributes but won't actually be used.
 *
 * Returns: a cairo_os2_surface_t if successful, a nil surface if not.
 *
 * Since: 1.12
 **/

cairo_public cairo_surface_t *
cairo_os2_surface_create_null_surface (HPS  hps,
                                       int  width,
                                       int  height)
{
    cairo_os2_surface_t *surface;

    if (width < 0 || height < 0)
        return _cairo_surface_create_in_error (CAIRO_STATUS_INVALID_SIZE);

    surface = malloc (sizeof (cairo_os2_surface_t));
    if (unlikely (surface == NULL))
        return _cairo_surface_create_in_error (CAIRO_STATUS_NO_MEMORY);

    memset (surface, 0, sizeof(cairo_os2_surface_t));
    surface->format  = CAIRO_FORMAT_ARGB32;
    surface->content = _cairo_content_from_format (surface->format);
    surface->subtype = CAIRO_OS2_SUBTYPE_NULL;
    surface->width   = width;
    surface->height  = height;
    surface->hps     = hps;

    _cairo_surface_init (&surface->base,
                         &cairo_os2_surface_backend,
                         NULL, /* device */
                         surface->content);

    return &surface->base;
}


/**
 *
 * OS/2-specific functions
 *
 **/

/**
 * cairo_os2_surface_paint_window:
 * @surface: the cairo_os2_surface to display
 * @hps: presentation space handle associated with the target window
 * @rect: an array of native RECTL structure identifying the bottom-left
 *   and top-right corners of the areas to be displayed
 * @count: the number RECTLs in @rect's array
 *
 * This function paints the specified areas of a window using the supplied
 * surface. It is intended for surfaces whose dimensions have a 1-to-1
 * mapping to the dimensions of a native window (such as those created by
 * cairo_os2_surface_create_for_window()).  As such, each rectangle identifies
 * the coordinates for both the source bitmap and the destination window.  The
 * window's handle must already be associated with this surface, using either
 * cairo_os2_surface_create_for_window() or cairo_os2_surface_set_hwnd().
 *
 * It attempts to use the most efficient method available to move the image
 * to the screen.  If an attempt fails, it falls back to the next method.
 * If all attempts fail, the surface is marked as being in an error state.
 *
 * If @rect is %NULL, the entire window will be repainted.  @hps is
 * typically the presentation space handle returned by WinBeginPaint().
 * If it is null, cairo_os2_surface_paint_window() will use the HPS
 * that was previously associated with this surface.  If that too is null,
 * it will allocate (and later destroy) an HPS for the window handle
 * associated with this surface.
 *
 * Returns: %CAIRO_STATUS_SUCCESS if successful,
 *          %CAIRO_STATUS_WRITE_ERROR if a permanent fatal error occurs,
 *          or one of several errors if arguments are missing or invalid.
 *
 * Since: 1.12
 **/

cairo_public cairo_status_t
cairo_os2_surface_paint_window (cairo_os2_surface_t *surface,
                                HPS                  hps,
                                RECTL               *rect,
                                int                  count)
{
    cairo_status_t  status = CAIRO_STATUS_SUCCESS;
    HPS             hpsTemp = 0;
    RECTL           rectTemp;

    if (unlikely (!surface || (rect && !count)))
        return _cairo_error (CAIRO_STATUS_NULL_POINTER);

    if (cairo_surface_get_type (&surface->base) != CAIRO_SURFACE_TYPE_OS2 ||
        surface->subtype != CAIRO_OS2_SUBTYPE_IMAGE ||
        !surface->hwnd)
        return _cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);

    /* If an HPS wasn't provided, see if we can get one. */
    if (!hps) {
        hps = surface->hps;
        if (!hps) {
            if (surface->hwnd) {
                hpsTemp = WinGetPS(surface->hwnd);
                hps = hpsTemp;
            }
            /* No HPS & no way to get one, so exit */
            if (!hps)
                return _cairo_error (CAIRO_STATUS_NULL_POINTER);
        }
    }

    /* If rect is null, paint the entire window. */
    if (!rect) {
        rectTemp.xLeft = 0;
        rectTemp.xRight = surface->width;
        rectTemp.yTop = surface->height;
        rectTemp.yBottom = 0;
        rect = &rectTemp;
        count = 1;
    }

    if (dive_handle) {
        if (_cairo_os2_surface_paint_dive (surface, hps, rect, count) ==
                                                        CAIRO_STATUS_SUCCESS)
            goto done;
    }
    if (!display_use24bpp) {
        if (_cairo_os2_surface_paint_32bpp (surface, hps, rect, rect, count) ==
                                                        CAIRO_STATUS_SUCCESS)
            goto done;
        display_use24bpp = TRUE;
    }
    status = _cairo_os2_surface_paint_24bpp (surface, hps, rect, rect, count);
    if (status == CAIRO_STATUS_SUCCESS)
        goto done;

    _cairo_surface_set_error (&surface->base, status);

done:
    if (hpsTemp)
        WinReleasePS (hpsTemp);

    return status;
}


/**
 * cairo_os2_surface_paint_bitmap:
 * @surface: the cairo_os2_surface to display
 * @hps: presentation space handle associated with the target device
 * @src: an array of native RECTL structure identifying those areas of
 *   the source surface which will be painted onto the destination
 * @dst: an array of native RECTL structure that identify where on the
 *   destination surface the corresponding @src rectangles will be painted
 * @count: the number of RECTLs in @src and @dst arrays
 * @use24bpp: if set, the surface's 32bpp bitmap is copied to a 24bpp
 *    bitmap before blitting to @hps
 *
 * This function paints the specified areas of a presentation space using
 * the supplied surface.  The target of the operation can be a PM bitmap
 * which the caller has previously created and selected into @hps, or it
 * can be an @hps associated with a window or printer.  Only GPI functions
 * will be used for blitting - DIVE is not available.
 *
 * Neither the origins nor dimensions of the @src and @dst rectangles need
 * be the same:  translation and scaling will occur as required.  Either
 * @src or @dst can be %NULL if there is a 1-to-1 mapping between the source
 * and destination's coordinates.  If both @src and @dst are %NULL, the
 * entire surface's contents will be painted into the presentation space.
 *
 * If the driver for the device context associated with @hps is known to
 * be unable to use 32-bit bitmap data as input (e.g. most native printer
 * drivers), the @use24bpp flag should be set.  Its use is very expensive
 * and should be avoided unless absolutely necessary.
 *
 * If cairo_os2_surface_set_hps() was used to associate an HPS with this
 * surface using, @hps may be %NULL;  otherwise, it must be supplied.
 *
 * Returns: %CAIRO_STATUS_SUCCESS if successful,
 *          %CAIRO_STATUS_WRITE_ERROR if the bit-blit fails,
 *          or one of several errors if arguments are missing or invalid.
 *
 * Since: 1.12
 **/

cairo_public cairo_status_t
cairo_os2_surface_paint_bitmap (cairo_os2_surface_t *surface,
                                HPS                  hps,
                                RECTL               *src,
                                RECTL               *dst,
                                int                  count,
                                BOOL                 use24bpp)
{
    cairo_status_t  status = CAIRO_STATUS_SUCCESS;
    RECTL           rectTemp;

    if (unlikely (!surface || ((src || dst) && !count)))
        return _cairo_error (CAIRO_STATUS_NULL_POINTER);

    if (cairo_surface_get_type (&surface->base) != CAIRO_SURFACE_TYPE_OS2 ||
        surface->subtype != CAIRO_OS2_SUBTYPE_IMAGE)
        return _cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);

    if (!hps) {
        if (!surface->hps)
            return _cairo_error (CAIRO_STATUS_NULL_POINTER);
        hps = surface->hps;
    }

    /* If either src or dst is null, assume a 1 to 1 mapping of
     * coordinates;  if both are null, copy all of src to dst.
     */
    if (!src) {
        if (!dst) {
            rectTemp.xLeft = 0;
            rectTemp.xRight = surface->width;
            rectTemp.yTop = surface->height;
            rectTemp.yBottom = 0;
            src = dst = &rectTemp;
            count = 1;
        }
        else
            src = dst;
    }
    else
        if (!dst)
            dst = src;

    /* Perform either a 32-bit or 24-bit blit, depending on @use24bpp. */
    if (use24bpp)
        status = _cairo_os2_surface_paint_24bpp (surface, hps, src, dst, count);
    else
        status = _cairo_os2_surface_paint_32bpp (surface, hps, src, dst, count);

    return status;
}


/**
 * cairo_os2_surface_get_hps:
 * @surface: the cairo_os2_surface to query
 * @hps: presentation space handle associated with this surface
 *
 * Retrieves the HPS associated with this surface, if any.
 *
 * Returns: %CAIRO_STATUS_SUCCESS if successful,
 *          or one of several errors if arguments are missing or invalid.
 *
 * Since: 1.12
 **/

cairo_public cairo_status_t
cairo_os2_surface_get_hps (cairo_os2_surface_t *surface,
                           HPS                 *hps)
{
    if (unlikely (!surface || !hps))
        return _cairo_error (CAIRO_STATUS_NULL_POINTER);

    if (cairo_surface_get_type (&surface->base) != CAIRO_SURFACE_TYPE_OS2 &&
        cairo_surface_get_type (&surface->base) != CAIRO_SURFACE_TYPE_OS2_PRINTING)
        return _cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);

    *hps = surface->hps;

    return CAIRO_STATUS_SUCCESS;
}


/**
 * cairo_os2_surface_set_hps:
 * @surface: a cairo_os2_surface
 * @hps: presentation space handle to associate with this surface
 *
 * Associates an HPS with this surface or dissociates it if @hps is %NULL.
 * The caller retains ownership of @hps and must destroy it when it is no
 * longer needed.
 *
 * Returns: %CAIRO_STATUS_SUCCESS if successful,
 *          or one of several errors if arguments are missing or invalid.
 *
 * Since: 1.12
 **/

cairo_public cairo_status_t
cairo_os2_surface_set_hps (cairo_os2_surface_t *surface,
                           HPS                  hps)
{
    if (unlikely (!surface))
        return _cairo_error (CAIRO_STATUS_NULL_POINTER);

    if (cairo_surface_get_type (&surface->base) != CAIRO_SURFACE_TYPE_OS2 &&
        cairo_surface_get_type (&surface->base) != CAIRO_SURFACE_TYPE_OS2_PRINTING)
        return _cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);

    surface->hps = hps;

    return CAIRO_STATUS_SUCCESS;
}


/**
 * cairo_os2_surface_set_hwnd:
 * @surface: a cairo_os2_surface
 * @hwnd: window handle to associate with this surface
 *
 * Associates an HWND with this surface or dissociates it if @hwnd is %NULL.
 * The caller retains ownership of @hwnd.  If the surface was created without
 * supplying an HWND, this function must be called before attempting to use
 * cairo_os2_surface_paint_window().
 *
 * Returns: %CAIRO_STATUS_SUCCESS if successful,
 *          or one of several errors if arguments are missing or invalid.
 *
 * Since: 1.12
 **/

cairo_public cairo_status_t
cairo_os2_surface_set_hwnd (cairo_os2_surface_t *surface,
                            HWND                 hwnd)
{
    if (unlikely (!surface))
        return _cairo_error (CAIRO_STATUS_NULL_POINTER);

    if (cairo_surface_get_type (&surface->base) != CAIRO_SURFACE_TYPE_OS2)
        return _cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);

    surface->hwnd = hwnd;

    return CAIRO_STATUS_SUCCESS;
}


/**
 * cairo_os2_surface_set_size:
 * @surface: the cairo_os2_surface to resize
 * @width: the surface's new width
 * @height: the surface's new height
 * @copy: boolean indicating whether existing bitmap data should
 *   be copied to the resized surface
 *
 * This function resizes a cairo_os2_surface and its underlying image
 * surface. It is intended for surfaces whose dimensions have a 1-to-1
 * mapping to the dimensions of a native window (such as those created
 * by cairo_os2_surface_create_for_window()).
 *
 * If @copy is non-zero, bitmap data will be copied from the original
 * image surface to its replacement.  The bitmaps will be aligned at
 * their top-left corners.
 *
 * Returns: %CAIRO_STATUS_SUCCESS if successful,
 *          or one of several errors if arguments are missing or invalid.
 *
 * Since: 1.12
 **/

cairo_public cairo_status_t
cairo_os2_surface_set_size (cairo_os2_surface_t *surface,
                            int                  width,
                            int                  height,
                            int                  copy)
{
    cairo_surface_t     *old_image;
    cairo_os2_subtype_t  old_subtype;
    int                  old_width;
    int                  old_height;
    int                  old_stride;
    unsigned char       *old_data;
    cairo_status_t       status = CAIRO_STATUS_SUCCESS;
    unsigned long        rc;

    if (unlikely (!surface))
        return _cairo_error (CAIRO_STATUS_NULL_POINTER);

    if (cairo_surface_get_type (&surface->base) != CAIRO_SURFACE_TYPE_OS2)
        return _cairo_error (CAIRO_STATUS_SURFACE_TYPE_MISMATCH);

    if (width < 0 || height < 0)
        return _cairo_error (CAIRO_STATUS_INVALID_SIZE);

    old_image   = surface->image;
    old_width   = surface->width;
    old_height  = surface->height;
    old_stride  = surface->stride;
    old_data    = surface->data;

    surface->image  = 0;
    surface->width  = width;
    surface->height = height;
    surface->stride = cairo_format_stride_for_width (surface->format, width);
    surface->data   = 0;

    if (surface->subtype != CAIRO_OS2_SUBTYPE_NULL)
        status = _cairo_os2_surface_init_image (surface);

    if (_cairo_status_is_error(status)) {
        if (surface->image)
            cairo_surface_destroy (surface->image);
        if (surface->data)
            _cairo_os2_surface_free (surface->data);
        surface->image  = old_image;
        surface->width  = old_width;
        surface->height = old_height;
        surface->stride = old_stride;
        surface->data   = old_data;
        return status;
    }

    if (copy) {
        unsigned char *src = old_data;
        unsigned char *dst = surface->data;
        int count  = (old_stride < surface->stride) ? old_stride : surface->stride;
        int height = (old_height < surface->height) ? old_height : surface->height;

        while (height) {
            memcpy(dst, src, count);
            src += old_stride;
            dst += surface->stride;
            height--;
        }
    }

    if (old_image)
        cairo_surface_destroy (old_image);
    if (old_data)
        _cairo_os2_surface_free (old_data);

    return status;
}


/**
 * cairo_os2_surface_enable_dive:
 *
 * @enable: boolean indicating whether DIVE should be enabled or disabled
 * @hide_pointer: boolean indicating whether the mouse pointer should be
 *   removed from the screen while writing to the frame buffer.
 *
 * This function enables or disables DIVE.  When enabled, image data is
 * written directly to the video framebuffer, bypassing OS/2's graphics
 * subsystem.  Only 32-bit (BGR4) and 24-bit (BGR3) color modes are
 * supported.  If any other video mode is in effect, or if any error
 * occurs while initializing or using DIVE, it will be disabled for
 * the remainder of the process.
 *
 * The @hide_pointer argument avoids screen corruption when DIVE is used
 * with less-capable video drivers that use a software-generated pointer.
 *
 * Returns: %TRUE if the desired state (enabled or disabled) can be
 *  achieved or is already in effect, and FALSE otherwise.
 *
 * Since: 1.12
 **/

cairo_public cairo_bool_t
cairo_os2_surface_enable_dive (cairo_bool_t enable,
                               cairo_bool_t hide_pointer)
{
    unsigned int  rc;
    unsigned long action;
    HMODULE       hmod;
    unsigned int  dive_fmts[64];
    DIVE_CAPS     dive_caps;
    char          msg[8];

    /* If DIVE is disabled due to an earlier error, return false. */
    if (dive_status == -1)
        return FALSE;

    /* Close DIVE if it is currently enabled and reset all values. */
    if (!enable) {
        if (dive_handle)
            DiveClose (dive_handle);
        dive_handle = 0;
        dive_scrnbuf = 0;
        dive_hideptr = TRUE;
        dive_status = 0;
        return TRUE;
    }

    /* If DIVE is already enabled, do nothing. */
    if (dive_status)
        return TRUE;

    /* Assume the worst. */
    dive_status = -1;

#ifdef OS2_DYNAMIC_DIVE
    if (!dive_loaded) {
        dive_loaded = TRUE;
        if (!_cairo_os2_surface_load_dive ()) {
            printf ("_init_dive:  _load_dive() failed\n");
            return FALSE;
        }
        printf ("_init_dive:  _load_dive() succeeded\n");
    }
#endif

    /* This is for testing 24bpp GPI mode. */
    if (getenv("MOZ_USE24BPP")) {
        display_use24bpp = TRUE;
        printf ("_init_dive:  forcing 24-bit GPI mode\n");
        return FALSE;
    }

    memset (&dive_caps, 0, sizeof(dive_caps));
    memset (dive_fmts, 0, sizeof(dive_fmts));
    dive_caps.ulStructLen = sizeof(dive_caps);
    dive_caps.ulFormatLength = sizeof(dive_fmts);
    dive_caps.pFormatData = dive_fmts;

    /* Get the driver's DIVE capabilities. */
    rc = DiveQueryCaps (&dive_caps, DIVE_BUFFER_SCREEN);
    if (rc) {
        printf ("_init_dive:  DiveQueryCaps - rc= %x\n", rc);
        return FALSE;
    }

    /* Only 32-bit (BGR4) and 24-bit (BGR3) color modes
     * are supported.  If the mode is anything else, exit.
     */
    memcpy(msg, &dive_caps.fccColorEncoding, 4);
    msg[4] = 0;
    if (dive_caps.fccColorEncoding != FOURCC_BGR4) {
        if (dive_caps.fccColorEncoding == FOURCC_BGR3)
            display_use24bpp = TRUE;
        else {
            printf ("_init_dive:  incompatible screen format - %s\n", msg);
            return FALSE;
        }
    }

    dive_height = dive_caps.ulVerticalResolution;
    dive_stride = dive_caps.ulScanLineBytes;

    /* Open a DIVE instance and get the address of the frame buffer. */
    rc = DiveOpen(&dive_handle, FALSE, (void*)&dive_scrnbuf);
    if (rc) {
        printf ("_init_dive:  DiveOpen - rc= %x\n", rc);
        return FALSE;
    }
    printf ("_init_dive:  hDive= %lx  scrn= %p  format= %s\n",
            dive_handle, (void*)dive_scrnbuf, msg);

    /* Success. */
    dive_hideptr = hide_pointer;
    dive_status = 1;

    return TRUE;
}


/**
 *
 * Backend functions
 *
 **/

cairo_surface_t *
_cairo_os2_surface_create_similar (void             *abstract_other,
                                   cairo_content_t   content,
                                   int               width,
                                   int               height)
{
    cairo_os2_surface_t *other = abstract_other;

    if (unlikely (other->base.status))
        return _cairo_surface_create_in_error (other->base.status);

    if (unlikely (!CAIRO_CONTENT_VALID (content)))
        return _cairo_surface_create_in_error (CAIRO_STATUS_INVALID_CONTENT);

    if (other->subtype == CAIRO_OS2_SUBTYPE_IMAGE)
        return cairo_os2_surface_create (_cairo_format_from_content (content),
                                         width, height);

    if (other->subtype == CAIRO_OS2_SUBTYPE_NULL)
        return cairo_os2_surface_create_null_surface (other->hps, width, height);

    return _cairo_surface_create_in_error (CAIRO_STATUS_INVALID_FORMAT);
}


static cairo_status_t
_cairo_os2_surface_finish (void *abstract_surface)
{
    cairo_os2_surface_t *surface = abstract_surface;
    if (surface->image)
        cairo_surface_destroy (surface->image);
    if (surface->data)
        _cairo_os2_surface_free (surface->data);

    return CAIRO_STATUS_SUCCESS;
}


static cairo_status_t
_cairo_os2_surface_acquire_source_image (void                    *abstract_surface,
                                         cairo_image_surface_t  **image_out,
                                         void                   **image_extra)
{
    cairo_os2_surface_t *surface = abstract_surface;

    if (!surface->image)
        return CAIRO_STATUS_NULL_POINTER;

    return _cairo_surface_acquire_source_image (surface->image,
                                                image_out, image_extra);
}


static void
_cairo_os2_surface_release_source_image (void                   *abstract_surface,
                                         cairo_image_surface_t  *image,
                                         void                   *image_extra)
{
}


static cairo_status_t
_cairo_os2_surface_acquire_dest_image (void                    *abstract_surface,
                                       cairo_rectangle_int_t   *interest_rect,
                                       cairo_image_surface_t  **image_out,
                                       cairo_rectangle_int_t   *image_rect_out,
                                       void                   **image_extra)
{
    cairo_os2_surface_t *surface = abstract_surface;

    if (!surface->image)
        return CAIRO_STATUS_NULL_POINTER;

    return _cairo_surface_acquire_dest_image (surface->image,
                                              interest_rect, image_out,
                                              image_rect_out, image_extra);
}


static void
_cairo_os2_surface_release_dest_image (void                   *abstract_surface,
                                       cairo_rectangle_int_t  *interest_rect,
                                       cairo_image_surface_t  *image,
                                       cairo_rectangle_int_t  *image_rect,
                                       void                   *image_extra)
{
}


static cairo_int_status_t
_cairo_os2_surface_composite (cairo_operator_t        op,
                              const cairo_pattern_t  *src_pattern,
                              const cairo_pattern_t  *mask_pattern,
                              void                   *abstract_dst,
                              int                     src_x,
                              int                     src_y,
                              int                     mask_x,
                              int                     mask_y,
                              int                     dst_x,
                              int                     dst_y,
                              unsigned int            width,
                              unsigned int            height,
                              cairo_region_t         *clip_region)
{
    cairo_os2_surface_t *surface = abstract_dst;

    if (!surface->image)
        return CAIRO_STATUS_SUCCESS;

    return _cairo_surface_composite (op, src_pattern, mask_pattern,
                                     surface->image, src_x, src_y,
                                     mask_x, mask_y, dst_x, dst_y,
                                     width, height, clip_region);
}


static cairo_int_status_t
_cairo_os2_surface_fill_rectangles (void                   *abstract_surface,
                                    cairo_operator_t        op,
                                    const cairo_color_t    *color,
                                    cairo_rectangle_int_t  *rects,
                                    int                     num_rects)
{
    cairo_os2_surface_t *surface = abstract_surface;

    if (!surface->image)
        return CAIRO_STATUS_SUCCESS;

    return _cairo_surface_fill_rectangles (surface->image, op,
                                           color, rects, num_rects);
}


static cairo_int_status_t
_cairo_os2_surface_composite_trapezoids (cairo_operator_t        op,
                                         const cairo_pattern_t  *pattern,
                                         void                   *abstract_dst,
                                         cairo_antialias_t       antialias,
                                         int                     src_x,
                                         int                     src_y,
                                         int                     dst_x,
                                         int                     dst_y,
                                         unsigned int            width,
                                         unsigned int            height,
                                         cairo_trapezoid_t      *traps,
                                         int                     num_traps,
                                         cairo_region_t         *clip_region)
{
    cairo_os2_surface_t *surface = abstract_dst;

    if (!surface->image)
        return CAIRO_STATUS_SUCCESS;

    return _cairo_surface_composite_trapezoids (op, pattern, surface->image,
                                                antialias, src_x, src_y,
                                                dst_x, dst_y, width, height,
                                                traps, num_traps, clip_region);
}


static cairo_span_renderer_t *
_cairo_os2_surface_create_span_renderer (cairo_operator_t        op,
                                         const cairo_pattern_t  *pattern,
                                         void                   *abstract_dst,
                                         cairo_antialias_t       antialias,
                                         const cairo_composite_rectangles_t *rects,
                                         cairo_region_t         *clip_region)
{
    cairo_os2_surface_t *surface = abstract_dst;

    if (!surface->image)
        return _cairo_span_renderer_create_in_error (CAIRO_STATUS_NULL_POINTER);

    return _cairo_surface_create_span_renderer (op, pattern, surface->image,
                                                antialias, rects, clip_region);
}


static cairo_bool_t
_cairo_os2_surface_check_span_renderer (cairo_operator_t        op,
                                        const cairo_pattern_t  *pattern,
                                        void                   *abstract_dst,
                                        cairo_antialias_t       antialias)
{
    cairo_os2_surface_t *surface = abstract_dst;

    if (!surface->image)
        return FALSE;

    return _cairo_surface_check_span_renderer (op, pattern,
                                               surface->image,
                                               antialias);
}


static cairo_bool_t
_cairo_os2_surface_get_extents (void                   *abstract_surface,
                                cairo_rectangle_int_t  *rectangle)
{
    cairo_os2_surface_t *surface = abstract_surface;

    rectangle->x = 0;
    rectangle->y = 0;
    rectangle->width  = surface->width;
    rectangle->height = surface->height;

    return TRUE;
}


static void
_cairo_os2_surface_get_font_options (void                  *abstract_surface,
                                     cairo_font_options_t  *options)
{
    cairo_os2_surface_t *surface = abstract_surface;

    if (surface->image)
        cairo_surface_get_font_options (surface->image, options);
    else
        _cairo_font_options_init_default (options);
}


static const cairo_surface_backend_t cairo_os2_surface_backend = {
    CAIRO_SURFACE_TYPE_OS2,
    _cairo_os2_surface_create_similar,
    _cairo_os2_surface_finish,
    _cairo_os2_surface_acquire_source_image,
    _cairo_os2_surface_release_source_image,
    _cairo_os2_surface_acquire_dest_image,
    _cairo_os2_surface_release_dest_image,
    NULL, /* clone_similar */
    _cairo_os2_surface_composite,
    _cairo_os2_surface_fill_rectangles,
    _cairo_os2_surface_composite_trapezoids,
    _cairo_os2_surface_create_span_renderer,
    _cairo_os2_surface_check_span_renderer,
    NULL, /* copy_page */
    NULL, /* show_page */
    _cairo_os2_surface_get_extents,
    NULL, /* old_show_glyphs */
    _cairo_os2_surface_get_font_options,
    NULL, /* flush */
    NULL, /* mark_dirty_rectangle */
    NULL, /* scaled_font_fini */
    NULL, /* scaled_glyph_fini */
    NULL, /* paint */
    NULL, /* mask */
    NULL, /* stroke */
    NULL, /* fill */
    NULL, /* show_glyphs */
    NULL, /* snapshot */
    NULL, /* is_similar */
    NULL, /* fill_stroke */
    NULL, /* create_solid_pattern_surface */
    NULL, /* can_repaint_solid_pattern_surface */
    NULL, /* has_show_text_glyphs */
    NULL  /* show_text_glyphs */
};


/**
 *
 *  Static helper functions
 *
 **/

/**
 * _cairo_os2_surface_init_image:
 * @surface: the cairo_os2_surface to associate with an cairo_image_surface
 *
 * Creates the image surface that underlies a cairo_os2_surface.
 * If the surface's area is not empty, it allocates the memory the image
 * surface will use for its bitmap.  The image surface is always created,
 * regardless of whether its area is empty and is left in its default state
 * of transparent black.
 *
 * Returns: %CAIRO_STATUS_SUCCESS if successful,
 *          %CAIRO_STATUS_NO_MEMORY if an image buffer can't be allocated,
 *          or one of several errors if image_surface creation fails.
 *
 * Since: 1.12
 **/

static cairo_status_t
_cairo_os2_surface_init_image (cairo_os2_surface_t *surface)
{
    cairo_status_t status;
    int            size = surface->stride * surface->height;

    if (size) {
        surface->data = _cairo_os2_surface_alloc (size);
        if (!surface->data)
            return CAIRO_STATUS_NO_MEMORY;
    }

    surface->image = cairo_image_surface_create_for_data (surface->data,
                                                          surface->format,
                                                          surface->width,
                                                          surface->height,
                                                          surface->stride);

    status = cairo_surface_status (surface->image);
    if (_cairo_status_is_error(status))
        return status;

    return status;
}


/**
 * _cairo_os2_surface_alloc:
 * @size: the number of bytes to allocate
 *
 * Allocates the memory passed to cairo_image_surface_create_for_data()
 * that will be used for its bitmap.
 *
 * Returns: a pointer to the newly allocated buffer if successful,
 *          %NULL otherwise.
 *
 * Since: 1.12
 **/

static unsigned char *
_cairo_os2_surface_alloc (unsigned int size)
{
    void  *buffer;

#ifdef OS2_USE_PLATFORM_ALLOC
# ifdef OS2_HIGH_MEMORY
    if (!DosAllocMem (&buffer, size,
                      OBJ_ANY | PAG_READ | PAG_WRITE | PAG_COMMIT))
        return buffer;
# endif
    if (DosAllocMem (&buffer, size,
                     PAG_READ | PAG_WRITE | PAG_COMMIT))
        buffer = NULL;

    return buffer;
#else
    buffer = malloc (size);
    if (buffer)
        memset (buffer, 0, size);

    return buffer;
#endif
}


/**
 * _cairo_os2_surface_free:
 * @buffer: the allocation to free
 *
 * Frees bitmap-data memory allocated by _cairo_os2_surface_alloc.
 *
 * Returns: no return value,
 *
 * Since: 1.12
 **/

static void
_cairo_os2_surface_free (void *buffer)
{
#ifdef OS2_USE_PLATFORM_ALLOC
    DosFreeMem (buffer);
#else
    free (buffer);
#endif
}


/**
 * _cairo_os2_surface_paint_32bpp:
 * @surface: the cairo_os2_surface to display
 * @hps: presentation space handle associated with the target device
 * @src: an array of native RECTL structure identifying those areas of
 *   the source surface which will be painted onto the destination
 * @dst: an array of native RECTL structure that identify where on the
 *   destination surface the corresponding @src rectangles will be painted
 * @count: the number of RECTLs in @src and @dst arrays
 *
 * Uses standard GPI functions and structures to paint selected portions
 * of the surface onto the presentation space identified by @hps.  If
 * the @src and @dst rectangles are not the same size, GpiDrawBits() will
 * expand or compress the image as needed.  Y-inversion is temporarily
 * enabled for the presentation space so that Cairo's top-line-first
 * bitmaps will be handled correctly.
 *
 * This function assumes the driver for the device context associated
 * with @hps is able to handle 32-bit bitmap data as input and can
 * convert it to an appropriate format/color-depth as needed.  If not,
 * the function returns %CAIRO_STATUS_WRITE_ERROR to signal that the
 * 24-bit version of this function should be used instead.
 *
 * Returns: %CAIRO_STATUS_SUCCESS if successful,
 *          %CAIRO_STATUS_WRITE_ERROR if the bit-blit fails.
 *
 * Since: 1.12
 **/

static cairo_status_t
_cairo_os2_surface_paint_32bpp (cairo_os2_surface_t *surface,
                                HPS                  hps,
                                RECTL               *src,
                                RECTL               *dst,
                                int                  count)
{
    cairo_status_t status = CAIRO_STATUS_SUCCESS;
    int            inversion;
    int            ndx;
    BITMAPINFO2    bmi;

    /* Set up the bitmap header for 32bit depth. */
    memset(&bmi, 0, sizeof (BITMAPINFO2));
    bmi.cbFix = sizeof (BITMAPINFO2) - sizeof (bmi.argbColor[0]);
    bmi.cx = surface->width;
    bmi.cy = surface->height;
    bmi.cPlanes = 1;
    bmi.cBitCount = 32;

    /* Enable Y Inversion for the HPS, so  GpiDrawBits will work with
     * Cairo's top-line-first bitmap (vs OS/2's bottom-line-first bitmaps)
     */
    inversion = GpiQueryYInversion (hps);
    GpiEnableYInversion (hps, surface->height - 1);

    for (ndx = 0; ndx < count; ndx++) {
        POINTL  aPtl[4];

        /* Change y values from OS/2's coordinate system to Cairo's;
         * swap top & bottom to accommodate Y-inversion setting;
         * and make the destination coordinates non-inclusive.
         */
        aPtl[0].x = dst[ndx].xLeft;
        aPtl[0].y = surface->height - dst[ndx].yTop;
        aPtl[1].x = dst[ndx].xRight - 1;
        aPtl[1].y = surface->height - dst[ndx].yBottom - 1;

        aPtl[2].x = src[ndx].xLeft;
        aPtl[2].y = surface->height - src[ndx].yTop;
        aPtl[3].x = src[ndx].xRight;
        aPtl[3].y = surface->height - src[ndx].yBottom;

        /* paint */
        if (GpiDrawBits (hps, surface->data, &bmi,
                         4, aPtl, ROP_SRCCOPY, BBO_IGNORE) != GPI_OK) {
            printf ("%p  GpiDrawBits for 32bpp\n", (void*)surface);
            status = _cairo_error (CAIRO_STATUS_WRITE_ERROR);
            break;
        }
    }

    /* Restore Y inversion */
    GpiEnableYInversion (hps, inversion);

    return status;
}


/**
 * _cairo_os2_surface_paint_24bpp:
 * @surface: the cairo_os2_surface to display
 * @hps: presentation space handle associated with the target device
 * @src: an array of native RECTL structure identifying those areas of
 *   the source surface which will be painted onto the destination
 * @dst: an array of native RECTL structure that identify where on the
 *   destination surface the corresponding @src rectangles will be painted
 * @count: the number of RECTLs in @src and @dst arrays
 *
 * This is a fallback function used when the driver for the device context
 * associated with @hps is unable to handle 32-bit bitmaps as input.  For
 * each rectangle to be displayed, it copies that part of the surface to a
 * temporary buffer, dropping the high-order byte to create a 24-bit bitmap.
 *
 * It temporarily enables Y-inversion for the presentation space so that
 * Cairo's top-line-first bitmaps will be handled correctly.
 *
 * Returns: %CAIRO_STATUS_SUCCESS if successful,
 *          %CAIRO_STATUS_WRITE_ERROR if the bit-blit fails,
 *          CAIRO_STATUS_NO_MEMORY if a buffer allocation fails.
 *
 * Since: 1.12
 **/

static cairo_status_t
_cairo_os2_surface_paint_24bpp (cairo_os2_surface_t *surface,
                                HPS                  hps,
                                RECTL               *src,
                                RECTL               *dst,
                                int                  count)
{
    cairo_status_t status = CAIRO_STATUS_SUCCESS;
    int            inversion;
    int            ndx;
    int            bufSize = 0;
    unsigned char *pchBuffer = 0;
    BITMAPINFO2    bmi;

    /* Set up the bitmap header for 24bit depth. */
    memset(&bmi, 0, sizeof (BITMAPINFO2));
    bmi.cbFix = sizeof (BITMAPINFO2) - sizeof (bmi.argbColor[0]);
    bmi.cPlanes = 1;
    bmi.cBitCount = 24;

    /* Enable Y Inversion for the HPS, so  GpiDrawBits will work with
     * Cairo's top-line-first bitmap (vs OS/2's bottom-line-first bitmaps)
     */
    inversion = GpiQueryYInversion (hps);
    GpiEnableYInversion (hps, surface->height - 1);

    for (ndx = 0; ndx < count; ndx++) {

        int             tgt_stride;
        int             tgt_pad;
        int             src_advance;
        int             x_ctr;
        int             y_ctr;
        unsigned char  *pchTarget;
        unsigned int   *pulSource;
        POINTL          aPtl[4];

        /* For the destination, change y values from OS/2's coordinate
         * system to Cairo's;  swap top & bottom to accommodate the
         * Y-inversion setting;  and make its coordinates non-inclusive.
         */
        aPtl[0].x = dst[ndx].xLeft;
        aPtl[0].y = surface->height - dst[ndx].yTop;
        aPtl[1].x = dst[ndx].xRight - 1;
        aPtl[1].y = surface->height - dst[ndx].yBottom - 1;

        /* For the source, calc its size and map its coordinates to
         * the origin of the buffer that will contain the 24-bit data.
         */
        bmi.cx = src[ndx].xRight - src[ndx].xLeft;
        bmi.cy = src[ndx].yTop - src[ndx].yBottom;
        aPtl[2].x = 0;
        aPtl[2].y = 0;
        aPtl[3].x = bmi.cx;
        aPtl[3].y = bmi.cy;

        /* The start of each row has to be DWORD aligned.  For the 24-bit
         * target bitmap, calculate the of number aligned bytes per row
         * and the number of padding bytes at the end of each row.  For
         * the 32-bit source bitmap, calc the number of bytes to advance
         * to the starting position on its next row.
         */
        tgt_stride = (((bmi.cx * bmi.cBitCount) + 31) / 32) * 4;
        tgt_pad = tgt_stride - bmi.cx * 3;
        src_advance = surface->stride - bmi.cx * 4;

        /* If the existing temporary bitmap buffer is large enough,
         * reuse it;  otherwise, allocate a new one.  If the rows
         * don't need padding, it has to be 1 byte larger than the
         * size of the bitmap or else the high-order byte from the
         * last source row will end up in unallocated memory.
         */
        x_ctr = tgt_stride * bmi.cy + (tgt_pad ? 0 : 1);
        if (x_ctr > bufSize) {
            bufSize = x_ctr;
            if (pchBuffer)
                _cairo_os2_surface_free (pchBuffer);
            pchBuffer = _cairo_os2_surface_alloc (bufSize);
            if (!pchBuffer) {
                status = _cairo_error (CAIRO_STATUS_NO_MEMORY);
                printf ("%p  _cairo_os2_surface_alloc for 24bpp\n", (void*)surface);
                break;
            }
        }

        /* Calc the starting address in the source buffer. */
        pulSource = (unsigned int*) (surface->data + (src[ndx].xLeft * 4) +
                       ((surface->height - src[ndx].yTop) * surface->stride));

        pchTarget = pchBuffer;

        /* Copy 4 bytes from the source but advance the target ptr only
         * 3 bytes, so the high-order alpha byte will be overwritten by
         * the next copy. At the end of each row, skip over the padding.
         */
        for (y_ctr = bmi.cy; y_ctr; y_ctr--) {
            for (x_ctr = bmi.cx; x_ctr; x_ctr--) {
                *((unsigned int*)pchTarget) = *pulSource++;
                pchTarget += 3;
            }
            pchTarget += tgt_pad;
            pulSource  = (unsigned int*)((char*)pulSource + src_advance);
        }

        /* paint */
        if (GpiDrawBits (hps, pchBuffer, &bmi,
                         4, aPtl, ROP_SRCCOPY, BBO_IGNORE) != GPI_OK) {
            printf ("%p  GpiDrawBits for 24bpp\n", (void*)surface);
            status = _cairo_error (CAIRO_STATUS_WRITE_ERROR);
            break;
        }
    }

    /* Free the temp buffer */
    if (pchBuffer)
        _cairo_os2_surface_free (pchBuffer);

    /* Restore Y inversion */
    GpiEnableYInversion (hps, inversion);

    return status;
}


/**
 * _cairo_os2_surface_paint_dive:
 * @surface: the cairo_os2_surface to display
 * @hps: presentation space handle associated with the target window
 * @rect: an array of native RECTL structure identifying the bottom-left
 *   and top-right corners of the areas to be displayed
 * @count: the number RECTLs in @rect's array
 *
 * This function uses DIVE to write those portions of the surface identified
 * by @rect directly to the screen buffer.  It only supports 32- or 24-bit
 * full-color video modes.  To avoid painting overlapping or child windows,
 * it must perform all of the visible-region calculations that GPI normally
 * handles.  Despite this, it provides a significant improvement in speed
 * compared to using GPI functions.
 *
 * Returns: %CAIRO_STATUS_SUCCESS if successful,
 *          %CAIRO_STATUS_WRITE_ERROR if any native call fails.
 *
 * Since: 1.12
 **/

static cairo_status_t
_cairo_os2_surface_paint_dive (cairo_os2_surface_t *surface,
                               HPS                  hps,
                               RECTL               *rect,
                               int                  count)
{
    cairo_status_t status = CAIRO_STATUS_WRITE_ERROR;
    int            rc;
    int            ndx;
    HRGN           rgnVisible = 0;
    HRGN           rgnPaint = 0;
    RGNRECT        rgnrect;
    RECTL          rectWnd;
    RECTL         *rectPtr;
    RECTL         *rectPaint = 0;

    /* Get the visible region for this window;  if not visible, exit. */
    rgnVisible = GpiCreateRegion (hps, 0, 0);
    if (!rgnVisible) {
        printf ("%p  GpiCreateRegion for visible\n", (void*)surface);
        goto done;
    }
    rc = WinQueryVisibleRegion (surface->hwnd, rgnVisible);
    if (rc == RGN_ERROR) {
        printf ("%p  WinQueryVisibleRegion\n", (void*)surface);
        goto done;
    }
    if (rc == RGN_NULL) {
        status = CAIRO_STATUS_SUCCESS;
        goto done;
    }

    /* Create a region from the update rectangles, then AND it against the
     * visible region to produce the series of rects that will be painted.
     */
    rgnPaint = GpiCreateRegion (hps, count, rect);
    if (!rgnPaint) {
        printf ("%p  GpiCreateRegion for paint\n", (void*)surface);
        goto done;
    }
    rc = GpiCombineRegion (hps, rgnVisible, rgnPaint, rgnVisible, CRGN_AND);
    if (rc == RGN_ERROR) {
        printf ("%p  GpiCombineRegion\n", (void*)surface);
        goto done;
    }
    if (rc == RGN_NULL) {
        status = CAIRO_STATUS_SUCCESS;
        goto done;
    }

    /* Determine the number of rectangles to paint,
     * allocate memory to store them, then fetch them.
     */
    rgnrect.ircStart = 1;
    rgnrect.crc = (unsigned int)-1;
    rgnrect.crcReturned = 0;
    rgnrect.ulDirection = RECTDIR_LFRT_TOPBOT;

    if (!GpiQueryRegionRects (hps, rgnVisible, 0, &rgnrect, 0)) {
        printf ("%p  GpiQueryRegionRects for cnt\n", (void*)surface);
        goto done;
    }
    rectPaint = (RECTL*)malloc (rgnrect.crcReturned * sizeof(RECTL));
    if (!rectPaint) {
        printf ("%p  malloc for rectPaint\n", (void*)surface);
        goto done;
    }
    rgnrect.crc = rgnrect.crcReturned;
    rgnrect.crcReturned = 0;
    if (!GpiQueryRegionRects (hps, rgnVisible, 0, &rgnrect, rectPaint)) {
        printf ("%p  GpiQueryRegionRects for rectPaint\n", (void*)surface);
        goto done;
    }

    /* Get the window's position in screen coordinates. */
    WinQueryWindowRect (surface->hwnd, &rectWnd);
    WinMapWindowPoints (surface->hwnd, HWND_DESKTOP, (POINTL*)&rectWnd, 2);

    /* If required by the video driver, hide the mouse pointer. */
    if (dive_hideptr)
        WinShowPointer (HWND_DESKTOP, FALSE);

    /* Get access to the frame buffer */
    rc = DiveAcquireFrameBuffer (dive_handle, &rectWnd);
    if (rc) {
        printf("%p  DiveAcquireFrameBuffer - rc= %x\n",
               (void*)surface, rc);
        _cairo_os2_surface_dive_error ();
        goto done;
    }

    for (ndx = rgnrect.crcReturned, rectPtr = rectPaint; ndx; ndx--, rectPtr++) {
        int            x;
        int            y;
        unsigned char *srcPtr;
        unsigned char *dstPtr;

        /* Get the starting point in the surface's buffer. */
        x = rectPtr->xLeft;
        y = surface->height - rectPtr->yTop;
        srcPtr = surface->data + (y * surface->stride) + (x * 4);

        /* Get the starting point in the frame buffer. */
        x = rectPtr->xLeft + rectWnd.xLeft;
        y = dive_height - (rectPtr->yTop + rectWnd.yBottom);
        dstPtr = dive_scrnbuf + (y * dive_stride) +
                 (x * (display_use24bpp ? 3 : 4));

        x = (rectPtr->xRight - rectPtr->xLeft);
        y = rectPtr->yTop - rectPtr->yBottom;

        /* For 24-bit mode, copy 3 bytes at a time but move the source
         * pointer 4 bytes to skip over the alpha byte.  when we reach
         * the right edge of the rect, advance each ptr by the stride
         * minus the distance they've already advanced.
         */
        if (unlikely (display_use24bpp)) {
            int   ndx;
            int   srcAdvance = surface->stride - (x * 4);
            int   dstAdvance = dive_stride - (x * 3);

            while (y) {
                for (ndx = x; ndx; ndx--) {
                    memcpy (dstPtr, srcPtr, 3);
                    srcPtr += 4;
                    dstPtr += 3;
                }
                srcPtr += srcAdvance;
                dstPtr += dstAdvance;;
                y--;
            }
        } else {
            /* For 32-bit mode, copy an entire line at a time,
             * then advance each pointer by its respective stride.
             */
            x *= 4;
            while (y) {
                memcpy (dstPtr, srcPtr, x);
                srcPtr += surface->stride;
                dstPtr += dive_stride;
                y--;
            }
        }
    }

    /* Release the frame buffer */
    rc = DiveDeacquireFrameBuffer (dive_handle);
    if (rc) {
        printf("%p  DiveDeacquireFrameBuffer - rc= %x\n",
               (void*)surface, rc);
        _cairo_os2_surface_dive_error ();
        goto done;
    }

    status = CAIRO_STATUS_SUCCESS;

done:
    /* Cleanup */
    if (dive_hideptr)
        WinShowPointer (HWND_DESKTOP, TRUE);
    if (rectPaint)
        free (rectPaint);
    if (rgnPaint)
        GpiDestroyRegion (hps, rgnPaint);
    if (rgnVisible)
        GpiDestroyRegion (hps, rgnVisible);

    return status;
}


/**
 * _cairo_os2_surface_dive_error:
 *
 * Closes DIVE instance if open, resets values, and disables DIVE
 * for this process.  Used when a DIVE function fails.
 *
 * Returns: no return value.
 *
 * Since: 1.12
 **/

static void
_cairo_os2_surface_dive_error (void)
{
    if (dive_handle)
        DiveClose (dive_handle);
    dive_handle = 0;
    dive_scrnbuf = 0;
    dive_hideptr = TRUE;
    dive_status = -1;
    return;
}


/**
 * _cairo_os2_surface_load_dive:
 *
 * Loads dive.dll and gets the entry point addresses used by this module.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 1.12
 **/

#ifdef OS2_DYNAMIC_DIVE

static cairo_bool_t
_cairo_os2_surface_load_dive (void)
{
    HMODULE hmod;

    if (DosLoadModule (0, 0, "DIVE", &hmod))
        return FALSE;

    if (DosQueryProcAddr (hmod, ORD_DIVEQUERYCAPS,
                          0,    (PFN*)&pDiveQueryCaps) ||
        DosQueryProcAddr (hmod, ORD_DIVEOPEN,
                          0,    (PFN*)&pDiveOpen) ||
        DosQueryProcAddr (hmod, ORD_DIVECLOSE,
                          0,    (PFN*)&pDiveClose) ||
        DosQueryProcAddr (hmod, ORD_DIVEACQUIREFRAMEBUFFER,
                          0,    (PFN*)&pDiveAcquireFrameBuffer) ||
        DosQueryProcAddr (hmod, ORD_DIVEDEACQUIREFRAMEBUFFER,
                          0,    (PFN*)&pDiveDeacquireFrameBuffer)) {
        DosFreeModule(hmod);
        return FALSE;
    }

    return TRUE;
}
#endif

