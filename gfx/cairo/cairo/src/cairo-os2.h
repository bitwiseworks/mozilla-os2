/* vim: set sw=4 sts=4 et cin: */
/* cairo - a vector graphics library with display and print output
 *
 * Copyright (c) 2005-2006 netlabs.org
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

#ifndef _CAIRO_OS2_H_
#define _CAIRO_OS2_H_

#include "cairo.h"

CAIRO_BEGIN_DECLS

/* The OS/2 Specific Cairo API */

cairo_public void
cairo_os2_init (void);

cairo_public void
cairo_os2_fini (void);

#if CAIRO_HAS_OS2_SURFACE

typedef struct _cairo_os2_surface cairo_os2_surface_t;

cairo_public cairo_surface_t *
cairo_os2_surface_create (cairo_format_t  format,
                          int             width,
                          int             height);

cairo_public cairo_surface_t *
cairo_os2_surface_create_for_window (HWND  hwnd,
                                     int   width,
                                     int   height);

cairo_public cairo_surface_t *
cairo_os2_surface_create_null_surface (HPS  hps,
                                       int  width,
                                       int  height);

cairo_public cairo_status_t
cairo_os2_surface_paint_window (cairo_os2_surface_t *surface,
                                HPS                  hps,
                                RECTL               *rect,
                                int                  count);

cairo_public cairo_status_t
cairo_os2_surface_paint_bitmap (cairo_os2_surface_t *surface,
                                HPS                  hps,
                                RECTL               *src,
                                RECTL               *dst,
                                int                  count,
                                BOOL                 use24bpp);

cairo_public cairo_status_t
cairo_os2_surface_get_hps (cairo_os2_surface_t *surface,
                           HPS                 *hps);

cairo_public cairo_status_t
cairo_os2_surface_set_hps (cairo_os2_surface_t *surface,
                           HPS                  hps);

cairo_public cairo_status_t
cairo_os2_surface_set_hwnd (cairo_os2_surface_t *surface,
                            HWND                 hwnd);

cairo_public cairo_status_t
cairo_os2_surface_set_size (cairo_os2_surface_t *surface,
                            int                  width,
                            int                  height,
                            int                  copy);

cairo_public cairo_bool_t
cairo_os2_surface_enable_dive (cairo_bool_t enable,
                               cairo_bool_t hide_pointer);

cairo_surface_t *
cairo_os2_printing_surface_create (HPS  hps,
                                   int  width,
                                   int  height);

#else  /* CAIRO_HAS_OS2_SURFACE */
# error Cairo was not compiled with support for the OS/2 backend
#endif /* CAIRO_HAS_OS2_SURFACE */

CAIRO_END_DECLS

#endif /* _CAIRO_OS2_H_ */

