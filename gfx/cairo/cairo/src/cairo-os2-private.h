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

#ifndef CAIRO_OS2_PRIVATE_H
#define CAIRO_OS2_PRIVATE_H

#include "cairoint.h"
#include "cairo-surface-clipper-private.h"

/**
 * Unpublished API:
 *   GpiEnableYInversion = PMGPI.723
 *   GpiQueryYInversion = PMGPI.726
 **/

BOOL APIENTRY GpiEnableYInversion (HPS hps, LONG lHeight);
LONG APIENTRY GpiQueryYInversion (HPS hps);

/**
 * Function declaration for GpiDrawBits () (missing from OpenWatcom headers)
 **/
#ifdef __WATCOMC__
LONG APIENTRY GpiDrawBits (HPS hps, PVOID pBits,
                           PBITMAPINFO2 pbmiInfoTable,
                           LONG lCount, PPOINTL aptlPoints,
                           LONG lRop, ULONG flOptions);
#endif

/**
 * Support for dynamically loading dive.dll
 **/

#ifdef OS2_DYNAMIC_DIVE
typedef ULONG (APIENTRY *DiveQuery_t)(void*, ULONG);
typedef ULONG (APIENTRY *DiveOpen_t)(ULONG*, BOOL, VOID*);
typedef ULONG (APIENTRY *DiveClose_t)(ULONG);
typedef ULONG (APIENTRY *DiveAcquire_t)(ULONG, RECTL*);
typedef ULONG (APIENTRY *DiveDeacquire_t)(ULONG);

#define ORD_DIVEQUERYCAPS             1
#define ORD_DIVEOPEN                  2
#define ORD_DIVECLOSE                 3
#define ORD_DIVEACQUIREFRAMEBUFFER    6
#define ORD_DIVEDEACQUIREFRAMEBUFFER  8
#endif

/**
 * OS/2 surface subtypes
 **/

typedef enum _cairo_os2_subtype {
    CAIRO_OS2_SUBTYPE_NULL = 0,
    CAIRO_OS2_SUBTYPE_IMAGE,
    CAIRO_OS2_SUBTYPE_PRINT
} cairo_os2_subtype_t;

/**
 * MMIO bitmap formats defined here to avoid #including
 * multiple headers that aren't otherwise needed.
 **/

#ifndef FOURCC_BGR4
#define FOURCC_BGR4 0x34524742
#endif
#ifndef FOURCC_BGR3
#define FOURCC_BGR3 0x33524742
#endif

/**
 * cairo_os2_surface_t:
 *
 * @base: Standard #cairo_surface_t structure.
 * @subtype: This #cairo_os2_surface-specific value identifies whether the
 *   surface will be used for images or as a dummy with no image.
 * @width: Width of the surface in pixels.
 * @height: Height of the surface in pixels.
 * @hps: PM presentation space handle whose ownership is retained by the
 *   caller.  Required for printing surfaces, optional otherwise.
 * @format: Standard #cairo_format_t value.
 * @content: Standard #cairo_content_t value.
 * @hwnd: PM window handle whose ownership is retained by the caller.
 *   Required for surfaces associated with a window and ignored otherwise.
 * @stride: Distance in bytes from the start of one scan line to the next.
 * @data: Pointer to the memory #cairo_image_surface_t uses for its bitmap.
 *   It is allocated and freed by cairo_os2_surface_t and is not accessible
 *   outside this module.
 * @image: Pointer to the underlying image surface. It is only used for
 *   subtype %CAIRO_OS2_SUBTYPE_IMAGE.
 **/

typedef struct _cairo_os2_surface {
    /* common data */
    cairo_surface_t         base;
    cairo_os2_subtype_t     subtype;
    int                     width;
    int                     height;
    HPS                     hps;
    cairo_format_t          format;
    cairo_content_t         content;

    /* image surface data */
    HWND                    hwnd;
    int                     stride;
    unsigned char          *data;
    cairo_surface_t        *image;

    /* printing surface data */
    cairo_bool_t            path_empty;
    cairo_bool_t            has_ctm;
    cairo_bool_t            has_gpi_ctm;
    cairo_matrix_t          ctm;
    cairo_matrix_t          gpi_ctm;
    cairo_surface_clipper_t clipper;
    cairo_paginated_mode_t  paginated_mode;
    cairo_surface_t *       paginated_surf;
} cairo_os2_surface_t;

#endif /* CAIRO_OS2_PRIVATE_H */

