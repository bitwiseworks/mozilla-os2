/* -*- Mode: c; tab-width: 8; c-basic-offset: 4; indent-tabs-mode: t; -*- */
/* Cairo - a vector graphics library with display and print output
 *
 * Copyright © 2007, 2008 Adrian Johnson
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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
 * The Initial Developer of the Original Code is Adrian Johnson.
 *
 * Contributor(s):
 *      Adrian Johnson <ajohnson@redneon.com>
 *      Vladimir Vukicevic <vladimir@pobox.com>
 *      Rich Walsh <rich@e-vertise.com>
 */


/**
 * cairo_os2_printing_surface:
 *
 * #cairo_os2_printing_surface is a port of #cairo_win32_printing_surface.
 * Changes have been made to accommodate differences in the platforms'
 * APIs and capabilities.  The source code has also been rearranged to
 * provide the file with more structure.
 **/


#define INCL_BASE
#define INCL_PM
#include <os2.h>
#include "cairo-os2-private.h"

#include "cairo-error-private.h"
#include "cairo-paginated-private.h"
#include "cairo-clip-private.h"
#include "cairo-recording-surface-private.h"
#include "cairo-scaled-font-subsets-private.h"
#include "cairo-image-info-private.h"

typedef union _RGB2LONG {
    RGB2  r;
    LONG  l;
} RGB2LONG;

static cairo_status_t
_cairo_os2_printing_surface_clipper_intersect_clip_path (cairo_surface_clipper_t *clipper,
                                                   cairo_path_fixed_t *path,
                                                   cairo_fill_rule_t   fill_rule,
                                                   double              tolerance,
                                                   cairo_antialias_t   antialias);

static const cairo_surface_backend_t cairo_os2_printing_surface_backend;
static const cairo_paginated_surface_backend_t cairo_os2_paginated_surface_backend;


/*
 * public function
 */

/**
 * cairo_os2_printing_surface_create:
 * @hps: presentation space handle associated with the printer's
 *   device context
 * @width: width of the surface in pixels
 * @height: height of the surface in pixels
 *
 * Creates a cairo surface that targets the given HPS.  GPI will be
 * used as much as possible to draw to the surface.
 *
 * The returned surface will be wrapped using the paginated surface to
 * provide correct complex rendering behaviour; show_page() and
 * associated methods must be used for correct output.
 *
 * Return value: the newly created surface
 *
 * Since: 1.12
 **/
cairo_surface_t *
cairo_os2_printing_surface_create (HPS  hps,
                                   int  width,
                                   int  height)
{
    cairo_os2_surface_t *surface;

    if (width < 0 || height < 0)
        return _cairo_surface_create_in_error (CAIRO_STATUS_INVALID_SIZE);

    if (!hps)
        return _cairo_surface_create_in_error (CAIRO_STATUS_NO_MEMORY);

    WinGetLastError (0);

    surface = malloc (sizeof (cairo_os2_surface_t));
    if (surface == NULL)
        return _cairo_surface_create_in_error (_cairo_error (CAIRO_STATUS_NO_MEMORY));

    surface->subtype = CAIRO_OS2_SUBTYPE_PRINT;
    surface->hps     = hps;
    surface->width   = width;
    surface->height  = height;
    surface->format  = CAIRO_FORMAT_RGB24;
    surface->content = CAIRO_CONTENT_COLOR_ALPHA;

    _cairo_surface_clipper_init (&surface->clipper,
                                 _cairo_os2_printing_surface_clipper_intersect_clip_path);

    _cairo_surface_init (&surface->base,
                         &cairo_os2_printing_surface_backend,
			 NULL, /* device */
                         CAIRO_CONTENT_COLOR_ALPHA);

    surface->paginated_surf = _cairo_paginated_surface_create (&surface->base,
                                                 CAIRO_CONTENT_COLOR_ALPHA,
                                                 &cairo_os2_paginated_surface_backend);
    /* paginated keeps the only reference to surface now, drop ours */
    cairo_surface_destroy (&surface->base);

    return surface->paginated_surf;
}


/*
 * analysis functions
 */

static cairo_int_status_t
analyze_surface_pattern_transparency (cairo_surface_pattern_t *pattern)
{
    cairo_image_surface_t     *image;
    void                      *image_extra;
    cairo_int_status_t         status;
    cairo_image_transparency_t transparency;

    status = _cairo_surface_acquire_source_image (pattern->surface,
                                                  &image,
                                                  &image_extra);
    if (status)
        return status;

    transparency = _cairo_image_analyze_transparency (image);
    switch (transparency) {
    case CAIRO_IMAGE_UNKNOWN:
        ASSERT_NOT_REACHED;
    case CAIRO_IMAGE_IS_OPAQUE:
        status = CAIRO_STATUS_SUCCESS;
        break;

    case CAIRO_IMAGE_HAS_BILEVEL_ALPHA:
    case CAIRO_IMAGE_HAS_ALPHA:
        status = CAIRO_INT_STATUS_FLATTEN_TRANSPARENCY;
        break;
    }

    _cairo_surface_release_source_image (pattern->surface, image, image_extra);

    return status;
}


static cairo_bool_t
surface_pattern_supported (const cairo_surface_pattern_t *pattern)
{
    if (_cairo_surface_is_recording (pattern->surface))
        return TRUE;

    if (cairo_surface_get_type (pattern->surface) != CAIRO_SURFACE_TYPE_OS2_PRINTING &&
        pattern->surface->backend->acquire_source_image == NULL)
    {
        return FALSE;
    }

    return TRUE;
}


static cairo_bool_t
pattern_supported (cairo_os2_surface_t *surface, const cairo_pattern_t *pattern)
{
    if (pattern->type == CAIRO_PATTERN_TYPE_SOLID)
        return TRUE;

    if (pattern->type == CAIRO_PATTERN_TYPE_SURFACE)
        return surface_pattern_supported ((const cairo_surface_pattern_t *) pattern);

    return FALSE;
}


static cairo_int_status_t
_cairo_os2_printing_surface_analyze_operation (cairo_os2_surface_t   *surface,
                                               cairo_operator_t       op,
                                               const cairo_pattern_t *pattern)
{
    if (! pattern_supported (surface, pattern))
        return CAIRO_INT_STATUS_UNSUPPORTED;

    if (!(op == CAIRO_OPERATOR_SOURCE ||
          op == CAIRO_OPERATOR_OVER ||
          op == CAIRO_OPERATOR_CLEAR))
        return CAIRO_INT_STATUS_UNSUPPORTED;

    if (pattern->type == CAIRO_PATTERN_TYPE_SURFACE) {
        cairo_surface_pattern_t *surface_pattern = (cairo_surface_pattern_t *) pattern;

        if ( _cairo_surface_is_recording (surface_pattern->surface))
            return CAIRO_INT_STATUS_ANALYZE_RECORDING_SURFACE_PATTERN;
    }

    if (op == CAIRO_OPERATOR_SOURCE ||
        op == CAIRO_OPERATOR_CLEAR)
        return CAIRO_STATUS_SUCCESS;

    /* CAIRO_OPERATOR_OVER is only supported for opaque patterns. If
     * the pattern contains transparency, we return
     * CAIRO_INT_STATUS_FLATTEN_TRANSPARENCY to the analysis
     * surface. If the analysis surface determines that there is
     * anything drawn under this operation, a fallback image will be
     * used. Otherwise the operation will be replayed during the
     * render stage and we blend the transarency into the white
     * background to convert the pattern to opaque.
     */

    if (pattern->type == CAIRO_PATTERN_TYPE_SURFACE) {
        cairo_surface_pattern_t *surface_pattern = (cairo_surface_pattern_t *) pattern;

        return analyze_surface_pattern_transparency (surface_pattern);
    }

    if (_cairo_pattern_is_opaque (pattern, NULL))
        return CAIRO_STATUS_SUCCESS;
    else
        return CAIRO_INT_STATUS_FLATTEN_TRANSPARENCY;
}


static cairo_bool_t
_cairo_os2_printing_surface_operation_supported (cairo_os2_surface_t   *surface,
                                                 cairo_operator_t       op,
                                                 const cairo_pattern_t *pattern)
{
    if (_cairo_os2_printing_surface_analyze_operation (surface, op, pattern)
        != CAIRO_INT_STATUS_UNSUPPORTED)
        return TRUE;

    return FALSE;
}


/*
 * utility functions
 */

static cairo_status_t
_gpi_error (const char *context)
{

    ULONG errid = WinGetLastError (0);

    fprintf (stderr, "_cairo_os2_printing_surface_%s - err= 0x%04lx\n",
             context, errid);
    fflush (stderr);
    return _cairo_error (CAIRO_STATUS_NO_MEMORY);
}


static void
_cairo_os2_printing_surface_init_clear_color (cairo_os2_surface_t   *surface,
                                              cairo_solid_pattern_t *color)
{
    if (surface->content == CAIRO_CONTENT_COLOR_ALPHA)
        _cairo_pattern_init_solid (color, CAIRO_COLOR_WHITE);
    else
        _cairo_pattern_init_solid (color, CAIRO_COLOR_BLACK);
}


static cairo_status_t
_cairo_os2_printing_surface_set_area_color (cairo_os2_surface_t   *surface,
                                            const cairo_pattern_t *source)
{
    cairo_solid_pattern_t *pattern = (cairo_solid_pattern_t *) source;
    AREABUNDLE bundle;
    RGB2LONG   c;

    c.r.bBlue  = pattern->color.blue_short  >> 8;
    c.r.bGreen = pattern->color.green_short >> 8;
    c.r.bRed   = pattern->color.red_short   >> 8;
    c.r.fcOptions = 0;

    if (!CAIRO_COLOR_IS_OPAQUE(&pattern->color) &&
        surface->content == CAIRO_CONTENT_COLOR_ALPHA) {
        /* Blend into white */
        BYTE one_minus_alpha = 255 - (pattern->color.alpha_short >> 8);

        c.r.bBlue  += one_minus_alpha;
        c.r.bGreen += one_minus_alpha;
        c.r.bRed   += one_minus_alpha;
    }

    bundle.lColor = c.l;
    if (!GpiSetAttrs (surface->hps, PRIM_AREA, ABB_COLOR, 0, (PBUNDLE)&bundle))
        return _gpi_error ("set_area_color - GpiSetAttrs");

    return CAIRO_STATUS_SUCCESS;
}


static inline void
_cairo_os2_matrixlf_to_matrix (const MATRIXLF *mlf,
                               cairo_matrix_t *m)
{
    m->xx = ((double) mlf->fxM11) / ((double) (1 << 16));
    m->yx = ((double) mlf->fxM12) / ((double) (1 << 16));

    m->xy = ((double) mlf->fxM21) / ((double) (1 << 16));
    m->yy = ((double) mlf->fxM22) / ((double) (1 << 16));

    m->x0 = (double) mlf->lM31;
    m->y0 = (double) mlf->lM32;
}


static inline void
_cairo_matrix_to_os2_matrixlf (const cairo_matrix_t *m,
                               MATRIXLF             *mlf)
{
    mlf->fxM11 = _cairo_fixed_16_16_from_double (m->xx);
    mlf->fxM12 = _cairo_fixed_16_16_from_double (m->yx);
    mlf->lM13  = 0;

    mlf->fxM21 = _cairo_fixed_16_16_from_double (m->xy);
    mlf->fxM22 = _cairo_fixed_16_16_from_double (m->yy);
    mlf->lM23  = 0;

    mlf->lM31  = (LONG) (m->x0 + 0.5);
    mlf->lM32  = (LONG) (m->y0 + 0.5);
    mlf->lM33  = 1;
}


static void
_cairo_matrix_factor_out_scale (cairo_matrix_t *m, double *scale)
{
    double s;

    s = fabs (m->xx);
    if (fabs (m->xy) > s)
        s = fabs (m->xy);
    if (fabs (m->yx) > s)
        s = fabs (m->yx);
    if (fabs (m->yy) > s)
        s = fabs (m->yy);
    *scale = s;
    s = 1.0/s;
    cairo_matrix_scale (m, s, s);
}


static int
_cairo_os2_printing_surface_line_cap (cairo_line_cap_t cap)
{
    switch (cap) {
    case CAIRO_LINE_CAP_BUTT:
        return LINEEND_FLAT;
    case CAIRO_LINE_CAP_ROUND:
        return LINEEND_ROUND;
    case CAIRO_LINE_CAP_SQUARE:
        return LINEEND_SQUARE;
    default:
        ASSERT_NOT_REACHED;
        return 0;
    }
}


static int
_cairo_os2_printing_surface_line_join (cairo_line_join_t join)
{
    switch (join) {
    case CAIRO_LINE_JOIN_MITER:
        return LINEJOIN_MITRE;
    case CAIRO_LINE_JOIN_ROUND:
        return LINEJOIN_ROUND;
    case CAIRO_LINE_JOIN_BEVEL:
        return LINEJOIN_BEVEL;
    default:
        ASSERT_NOT_REACHED;
        return 0;
    }
}


/*
 * path functions
 */

typedef struct _os2_print_path_info {
    cairo_os2_surface_t *surface;
} os2_path_info_t;


static cairo_status_t
_cairo_os2_printing_surface_path_move_to (void                *closure,
                                          const cairo_point_t *point)
{
    POINTL ptl;
    os2_path_info_t *path_info = closure;

    if (path_info->surface->has_ctm) {
        double x, y;

        x = _cairo_fixed_to_double (point->x);
        y = _cairo_fixed_to_double (point->y);
        cairo_matrix_transform_point (&path_info->surface->ctm, &x, &y);
        ptl.x = (LONG) x;
        ptl.y = (LONG) y;
    } else {
        ptl.x = _cairo_fixed_integer_part (point->x);
        ptl.y = _cairo_fixed_integer_part (point->y);
    }
    if (!GpiMove (path_info->surface->hps, &ptl))
        return _gpi_error ("path_move_to - GpiMove");

    return CAIRO_STATUS_SUCCESS;
}


static cairo_status_t
_cairo_os2_printing_surface_path_line_to (void                *closure,
                                          const cairo_point_t *point)
{
    POINTL ptl;
    os2_path_info_t *path_info = closure;

    path_info->surface->path_empty = FALSE;
    if (path_info->surface->has_ctm) {
        double x, y;

        x = _cairo_fixed_to_double (point->x);
        y = _cairo_fixed_to_double (point->y);
        cairo_matrix_transform_point (&path_info->surface->ctm, &x, &y);
        ptl.x = (LONG) x;
        ptl.y = (LONG) y;
    } else {
        ptl.x = _cairo_fixed_integer_part (point->x);
        ptl.y = _cairo_fixed_integer_part (point->y);
    }
    if (GpiLine (path_info->surface->hps, &ptl) == GPI_ERROR)
        return _gpi_error ("path_line_to - GpiLine");

    return CAIRO_STATUS_SUCCESS;
}


static cairo_status_t
_cairo_os2_printing_surface_path_curve_to (void                *closure,
                                           const cairo_point_t *b,
                                           const cairo_point_t *c,
                                           const cairo_point_t *d)
{
    os2_path_info_t *path_info = closure;
    POINTL points[3];

    path_info->surface->path_empty = FALSE;
    if (path_info->surface->has_ctm) {
        double x, y;

        x = _cairo_fixed_to_double (b->x);
        y = _cairo_fixed_to_double (b->y);
        cairo_matrix_transform_point (&path_info->surface->ctm, &x, &y);
        points[0].x = (LONG) x;
        points[0].y = (LONG) y;

        x = _cairo_fixed_to_double (c->x);
        y = _cairo_fixed_to_double (c->y);
        cairo_matrix_transform_point (&path_info->surface->ctm, &x, &y);
        points[1].x = (LONG) x;
        points[1].y = (LONG) y;

        x = _cairo_fixed_to_double (d->x);
        y = _cairo_fixed_to_double (d->y);
        cairo_matrix_transform_point (&path_info->surface->ctm, &x, &y);
        points[2].x = (LONG) x;
        points[2].y = (LONG) y;
    } else {
        points[0].x = _cairo_fixed_integer_part (b->x);
        points[0].y = _cairo_fixed_integer_part (b->y);
        points[1].x = _cairo_fixed_integer_part (c->x);
        points[1].y = _cairo_fixed_integer_part (c->y);
        points[2].x = _cairo_fixed_integer_part (d->x);
        points[2].y = _cairo_fixed_integer_part (d->y);
    }
    if (GpiPolySpline (path_info->surface->hps, 3, points) == GPI_ERROR)
        return _gpi_error ("path_curve_to - GpiPolySpline");

    return CAIRO_STATUS_SUCCESS;
}


static cairo_status_t
_cairo_os2_printing_surface_path_close_path (void *closure)
{
    os2_path_info_t *path_info = closure;

    if (!GpiCloseFigure (path_info->surface->hps))
        return _gpi_error ("path_close_path - GpiCloseFigure");

    return CAIRO_STATUS_SUCCESS;
}


static cairo_status_t
_cairo_os2_printing_surface_emit_path (cairo_os2_surface_t *surface,
                                       cairo_path_fixed_t  *path)
{
    os2_path_info_t path_info;

    path_info.surface = surface;
    return _cairo_path_fixed_interpret (path,
                                        CAIRO_DIRECTION_FORWARD,
                                        _cairo_os2_printing_surface_path_move_to,
                                        _cairo_os2_printing_surface_path_line_to,
                                        _cairo_os2_printing_surface_path_curve_to,
                                        _cairo_os2_printing_surface_path_close_path,
                                        &path_info);
}


/*
 * clip functions
 */

static cairo_status_t
_cairo_os2_printing_surface_clipper_intersect_clip_path (cairo_surface_clipper_t *clipper,
                                                   cairo_path_fixed_t *path,
                                                   cairo_fill_rule_t   fill_rule,
                                                   double              tolerance,
                                                   cairo_antialias_t   antialias)
{
    cairo_os2_surface_t *surface = cairo_container_of (clipper,
                                                       cairo_os2_surface_t,
                                                       clipper);
    cairo_status_t status;
    LONG options;
    RECTL rcl;
    LONG  rc;

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE)
        return CAIRO_STATUS_SUCCESS;

    if (path == NULL) {
        if (!GpiRestorePS (surface->hps, -1))
            return _gpi_error ("clipper_intersect_clip_path - GpiRestorePS");
        if (GpiSavePS (surface->hps) == GPI_ERROR)
            return _gpi_error ("clipper_intersect_clip_path - GpiSavePS");

        return CAIRO_STATUS_SUCCESS;
    }

    if (!GpiBeginPath (surface->hps, 1))
        return _gpi_error ("clipper_intersect_clip_path - GpiBeginPath");
    status = _cairo_os2_printing_surface_emit_path (surface, path);
    if (status)
        return status;
    if (!GpiEndPath (surface->hps))
        return _gpi_error ("clipper_intersect_clip_path - GpiEndPath");

    options = SCP_AND | SCP_INCL;

    switch (fill_rule) {
    case CAIRO_FILL_RULE_WINDING:
        options |= SCP_WINDING;
        break;
    case CAIRO_FILL_RULE_EVEN_ODD:
        options |= SCP_ALTERNATE;
        break;
    default:
        ASSERT_NOT_REACHED;
    }

    if (!GpiSetClipPath (surface->hps, 1, options))
        return _gpi_error ("clipper_intersect_clip_path - GpiSetClipPath - and");

    return CAIRO_STATUS_SUCCESS;
}


static cairo_status_t
_cairo_os2_printing_surface_get_ctm_clip_box (cairo_os2_surface_t *surface,
                                              RECTL               *clip)
{
    MATRIXLF mlf;

    _cairo_matrix_to_os2_matrixlf (&surface->ctm, &mlf);

    if (!GpiSetModelTransformMatrix (surface->hps, 9, &mlf, TRANSFORM_PREEMPT))
        return _gpi_error ("get_ctm_clip_box - GpiSetModelTransformMatrix (preempt)");
    if (GpiQueryClipBox (surface->hps, clip) == RGN_ERROR)
        return _gpi_error ("get_ctm_clip_box - GpiQueryClipBox");

    _cairo_matrix_to_os2_matrixlf (&surface->gpi_ctm, &mlf);
    if (!GpiSetModelTransformMatrix (surface->hps, 9, &mlf, TRANSFORM_REPLACE))
        return _gpi_error ("get_ctm_clip_box - GpiSetModelTransformMatrix (replace)");

    return CAIRO_STATUS_SUCCESS;
}


/*
 * paint
 */

static cairo_status_t
_cairo_os2_printing_surface_paint_solid_pattern (cairo_os2_surface_t   *surface,
                                                 const cairo_pattern_t *pattern)
{
    RECTL   clip;
    POINTL  ptl;
    cairo_status_t status;

    if (GpiQueryClipBox (surface->hps, &clip) == RGN_ERROR)
        return _gpi_error ("paint_solid_pattern - GpiQueryClipBox");

    status = _cairo_os2_printing_surface_set_area_color (surface, pattern);
    if (status)
        return status;

    if (!GpiQueryCurrentPosition (surface->hps, &ptl) ||
        !GpiMove (surface->hps, (POINTL*)&clip.xLeft) ||
        GpiBox (surface->hps, DRO_FILL, (POINTL*)&clip.xRight, 0, 0) == GPI_ERROR ||
        !GpiMove (surface->hps, &ptl))
        return _gpi_error ("paint_solid_pattern - GpiBox et al");

    return CAIRO_STATUS_SUCCESS;
}


static cairo_status_t
_cairo_os2_printing_surface_paint_recording_pattern (cairo_os2_surface_t     *surface,
                                                     cairo_surface_pattern_t *pattern)
{
    cairo_content_t old_content;
    cairo_matrix_t old_ctm;
    cairo_bool_t old_has_ctm;
    cairo_rectangle_int_t recording_extents;
    cairo_status_t status;
    cairo_extend_t extend;
    cairo_matrix_t p2d;
    MATRIXLF mlf;
    int x_tile, y_tile, left, right, top, bottom;
    RECTL clip;
    cairo_recording_surface_t *recording_surface =
                              (cairo_recording_surface_t *) pattern->surface;
    cairo_box_t bbox;

    extend = cairo_pattern_get_extend (&pattern->base);

    p2d = pattern->base.matrix;
    status = cairo_matrix_invert (&p2d);
    /* _cairo_pattern_set_matrix guarantees invertibility */
    assert (status == CAIRO_STATUS_SUCCESS);

    old_ctm = surface->ctm;
    old_has_ctm = surface->has_ctm;
    cairo_matrix_multiply (&p2d, &p2d, &surface->ctm);
    surface->ctm = p2d;
    if (GpiSavePS (surface->hps) == GPI_ERROR)
        return _gpi_error ("paint_recording_pattern - GpiSavePS 1");

    _cairo_matrix_to_os2_matrixlf (&p2d, &mlf);

    status = _cairo_recording_surface_get_bbox (recording_surface, &bbox, NULL);
    if (status)
        return status;

    _cairo_box_round_to_rectangle (&bbox, &recording_extents);

    status = _cairo_os2_printing_surface_get_ctm_clip_box (surface, &clip);
    if (status)
        return status;

    if (extend == CAIRO_EXTEND_REPEAT || extend == CAIRO_EXTEND_REFLECT) {
        left = floor (clip.xLeft / _cairo_fixed_to_double (bbox.p2.x - bbox.p1.x));
        right = ceil (clip.xRight / _cairo_fixed_to_double (bbox.p2.x - bbox.p1.x));
        top = floor (clip.yTop / _cairo_fixed_to_double (bbox.p2.y - bbox.p1.y));
        bottom = ceil (clip.yBottom / _cairo_fixed_to_double (bbox.p2.y - bbox.p1.y));
    } else {
        left = 0;
        right = 1;
        top = 0;
        bottom = 1;
    }

    old_content = surface->content;
    if (recording_surface->base.content == CAIRO_CONTENT_COLOR) {

	surface->content = CAIRO_CONTENT_COLOR;
	status = _cairo_os2_printing_surface_paint_solid_pattern (surface,
								    &_cairo_pattern_black.base);
        if (status)
            return status;
    }

    for (y_tile = top; y_tile < bottom; y_tile++) {
        for (x_tile = left; x_tile < right; x_tile++) {
            cairo_matrix_t m;
            double x, y;
            POINTL ptl;

            if (GpiSavePS (surface->hps) == GPI_ERROR)
                return _gpi_error ("paint_recording_pattern - GpiSavePS");
            m = p2d;
            cairo_matrix_translate (&m,
                                    x_tile*recording_extents.width,
                                    y_tile*recording_extents.height);
            if (extend == CAIRO_EXTEND_REFLECT) {
                if (x_tile % 2) {
                    cairo_matrix_translate (&m, recording_extents.width, 0);
                    cairo_matrix_scale (&m, -1, 1);
                }
                if (y_tile % 2) {
                    cairo_matrix_translate (&m, 0, recording_extents.height);
                    cairo_matrix_scale (&m, 1, -1);
                }
            }
            surface->ctm = m;
            surface->has_ctm = !_cairo_matrix_is_identity (&surface->ctm);

            /* Set clip path around bbox of the pattern. */
            if (!GpiBeginPath (surface->hps, 1))
                return _gpi_error ("paint_recording_pattern - GpiBeginPath");

            x = 0;
            y = 0;
            cairo_matrix_transform_point (&surface->ctm, &x, &y);
            ptl.x = (LONG) x;
            ptl.y = (LONG) y;
            if (!GpiMove (surface->hps, &ptl))
                return _gpi_error ("paint_recording_pattern - GpiMove");

            x = recording_extents.width;
            y = 0;
            cairo_matrix_transform_point (&surface->ctm, &x, &y);
            ptl.x = (LONG) x;
            ptl.y = (LONG) y;
            if (GpiLine (surface->hps, &ptl) == GPI_ERROR)
                return _gpi_error ("paint_recording_pattern - GpiLine 1");

            x = recording_extents.width;
            y = recording_extents.height;
            cairo_matrix_transform_point (&surface->ctm, &x, &y);
            ptl.x = (LONG) x;
            ptl.y = (LONG) y;
            if (GpiLine (surface->hps, &ptl) == GPI_ERROR)
                return _gpi_error ("paint_recording_pattern - GpiLine 2");

            x = 0;
            y = recording_extents.height;
            cairo_matrix_transform_point (&surface->ctm, &x, &y);
            ptl.x = (LONG) x;
            ptl.y = (LONG) y;
            if (GpiLine (surface->hps, &ptl) == GPI_ERROR)
                return _gpi_error ("paint_recording_pattern - GpiLine 3");

            if (!GpiCloseFigure (surface->hps))
                return _gpi_error ("paint_recording_pattern - GpiCloseFigure");
            if (!GpiEndPath (surface->hps))
                return _gpi_error ("paint_recording_pattern - GpiEndPath");
            if (!GpiSetClipPath (surface->hps, 1, SCP_AND))
                return _gpi_error ("paint_recording_pattern - GpiSetClipPath");

            /* Allow clip path to be reset during replay */
            if (GpiSavePS (surface->hps) == GPI_ERROR)
                return _gpi_error ("paint_recording_pattern - GpiSavePS 2");
            status = _cairo_recording_surface_replay_region (&recording_surface->base,NULL,
                                                             &surface->base,
                                                             CAIRO_RECORDING_REGION_NATIVE);
            assert (status != CAIRO_INT_STATUS_UNSUPPORTED);

            /* Restore both the clip save and our earlier path SaveDC */
            if (!GpiRestorePS (surface->hps, -2))
                return _gpi_error ("paint_recording_pattern - GpiRestorePS 1");

            if (status)
                return status;
        }
    }

    surface->content = old_content;
    surface->ctm = old_ctm;
    surface->has_ctm = old_has_ctm;
    if (!GpiRestorePS (surface->hps, -1))
        return _gpi_error ("paint_recording_pattern - GpiRestorePS 2");

    return status;
}


static cairo_status_t
_cairo_os2_printing_surface_paint_image_pattern (cairo_os2_surface_t     *surface,
                                                 cairo_surface_pattern_t *pattern)
{
    cairo_status_t status;
    cairo_extend_t extend;
    cairo_image_surface_t *image;
    void *image_extra;
    cairo_matrix_t m;
    MATRIXLF    mlf;
    BITMAPINFO2 bi;
    RECTL       clip;
    POINTL      aptl[4];
    int         x_tile, y_tile, left, right, top, bottom;
    int         bufsize, tgt_stride, tgt_pad, x_ctr, y_ctr;
    char       *pchBuffer = 0;
    char       *pchTarget;
    int        *pulSource;

    extend = cairo_pattern_get_extend (&pattern->base);
    status = _cairo_surface_acquire_source_image (pattern->surface,
                                                  &image, &image_extra);
    if (status)
        return status;

    if (image->base.status) {
        status = image->base.status;
        goto CLEANUP_IMAGE;
    }

    if (image->width == 0 || image->height == 0) {
        status = CAIRO_STATUS_SUCCESS;
        goto CLEANUP_IMAGE;
    }

    m = pattern->base.matrix;
    status = cairo_matrix_invert (&m);
    /* _cairo_pattern_set_matrix guarantees invertibility */
    assert (status == CAIRO_STATUS_SUCCESS);

    cairo_matrix_multiply (&m, &m, &surface->gpi_ctm);
    if (GpiSavePS (surface->hps) == GPI_ERROR) {
        status = _gpi_error ("paint_image_pattern - GpiSavePS 1");
        goto CLEANUP_IMAGE;
    }
    _cairo_matrix_to_os2_matrixlf (&m, &mlf);

    if (!GpiSetModelTransformMatrix (surface->hps, 9, &mlf, TRANSFORM_REPLACE)) {
        status = _gpi_error ("paint_image_pattern - GpiSetModelTransformMatrix");
        goto CLEANUP_IMAGE;
    }

    /* OS/2 printer drivers typically don't support 32-bit bitmaps, so the
     * image data has to be converted to 24-bit using a temporary buffer.
     * This is done by copying 4 bytes from the source but advancing the
     * target pointer by 3 bytes so the high-order byte gets overwritten
     * on the next copy.  Because the start of each row has to be DWORD
     * aligned, padding bytes may be needed at the end of the row.  If it
     * happens that no padding is needed, then the temp buffer has to be
     * one byte longer than the bitmap or else the high-order byte from
     * the last source row will end up in unallocated memory.
     */

    memset (&bi, 0, sizeof (bi));
    bi.cbFix = sizeof (BITMAPINFO2) - sizeof (bi.argbColor[0]);
    bi.cx = image->width;
    bi.cy = image->height;
    bi.cPlanes = 1;
    bi.cBitCount = 24;

    tgt_stride = (((bi.cx * bi.cBitCount) + 31) / 32) * 4;
    tgt_pad = tgt_stride - bi.cx * 3;
    bufsize = tgt_stride * bi.cy + (tgt_pad ? 0 : 1);

#ifdef OS2_USE_PLATFORM_ALLOC
# ifdef OS2_HIGH_MEMORY
    if (DosAllocMem ((void**) &pchBuffer, bufsize,
                     OBJ_ANY | PAG_READ | PAG_WRITE | PAG_COMMIT))
# endif
        if (DosAllocMem ((void**) &pchBuffer, bufsize,
                         PAG_READ | PAG_WRITE | PAG_COMMIT))
            status = CAIRO_STATUS_NO_MEMORY;
#else
    pchBuffer = (unsigned char*) calloc (1, bufsize);
    if (!pchBuffer)
        status = CAIRO_STATUS_NO_MEMORY;
#endif

    if (status == CAIRO_STATUS_NO_MEMORY) {
        fprintf (stderr, "_cairo_os2_printing_surface_paint_image_pattern - DosAlloc/calloc\n");
        fflush (stderr);
        goto CLEANUP_IMAGE;
    }

    pulSource = (int*)image->data;
    pchTarget = pchBuffer;
    for (y_ctr = bi.cy; y_ctr; y_ctr--) {
        for (x_ctr = bi.cx; x_ctr; x_ctr--) {
            *((int*)pchTarget) = *pulSource++;
            pchTarget += 3;
        }
        pchTarget += tgt_pad;
    }

    if (GpiQueryClipBox (surface->hps, &clip) == RGN_ERROR) {
        status = _gpi_error ("paint_image_pattern - GpiQueryClipBox");
        goto CLEANUP_BUFFER;
    }

    if (extend == CAIRO_EXTEND_REPEAT || extend == CAIRO_EXTEND_REFLECT) {
        left = floor ( clip.xLeft / (double) image->width);
        right = ceil (clip.xRight / (double) image->width);
        top = floor (clip.yTop / (double) image->height);
        bottom = ceil (clip.yBottom / (double) image->height);
    } else {
        left = 0;
        right = 1;
        top = 0;
        bottom = 1;
    }

    /* src coordinates */
    aptl[2].x = 0;
    aptl[2].y = 0;
    aptl[3].x = image->width;
    aptl[3].y = image->height;

    for (y_tile = top; y_tile < bottom; y_tile++) {
        for (x_tile = left; x_tile < right; x_tile++) {

            /* dst coordinates (made non-inclusive) */
            aptl[0].x = x_tile*image->width;
            aptl[0].y = y_tile*image->height;
            aptl[1].x = aptl[0].x + image->width - 1;
            aptl[1].y = aptl[0].y + image->height - 1;

            if (GpiDrawBits (surface->hps, pchBuffer, &bi,
                             4, aptl, ROP_SRCCOPY, BBO_IGNORE) == GPI_ERROR)
            {
                status = _gpi_error ("paint_image_pattern - GpiDrawBits");
                goto CLEANUP_BUFFER;
            }
        }
    }
    if (!GpiRestorePS (surface->hps, -1)) {
        status = _gpi_error ("paint_image_pattern - GpiRestorePS");
        goto CLEANUP_BUFFER;
    }

CLEANUP_BUFFER:
    /* Free the temp buffer */
    if (pchBuffer)
#ifdef OS2_USE_PLATFORM_ALLOC
        DosFreeMem (pchBuffer);
#else
        free (pchBuffer);
#endif

CLEANUP_IMAGE:
    _cairo_surface_release_source_image (pattern->surface, image, image_extra);

    return status;
}


static cairo_int_status_t
_cairo_os2_printing_surface_paint_pattern (cairo_os2_surface_t   *surface,
                                           const cairo_pattern_t *pattern)
{
    cairo_status_t status;

    switch (pattern->type) {
        case CAIRO_PATTERN_TYPE_SOLID:
            return _cairo_os2_printing_surface_paint_solid_pattern (surface, pattern);

        case CAIRO_PATTERN_TYPE_SURFACE: {
            cairo_surface_pattern_t * surf_pattern = (cairo_surface_pattern_t *) pattern;

            if (_cairo_surface_is_recording (surf_pattern->surface))
                return _cairo_os2_printing_surface_paint_recording_pattern (surface,
                                                                            surf_pattern);

            return _cairo_os2_printing_surface_paint_image_pattern (surface,
                                                                    surf_pattern);
        }

        case CAIRO_PATTERN_TYPE_LINEAR:
        case CAIRO_PATTERN_TYPE_RADIAL:
            break;
    }

    return CAIRO_INT_STATUS_UNSUPPORTED;
}


static cairo_int_status_t
_cairo_os2_printing_surface_paint (void                  *abstract_surface,
                                   cairo_operator_t       op,
                                   const cairo_pattern_t *source,
                                   cairo_clip_t          *clip)
{
    cairo_os2_surface_t *surface = abstract_surface;
    cairo_solid_pattern_t clear;
    cairo_status_t status;

    status = _cairo_surface_clipper_set_clip (&surface->clipper, clip);
    if (status)
        return status;

    if (op == CAIRO_OPERATOR_CLEAR) {
        _cairo_os2_printing_surface_init_clear_color (surface, &clear);
        source = (cairo_pattern_t*) &clear;
        op = CAIRO_OPERATOR_SOURCE;
    }

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE)
        return _cairo_os2_printing_surface_analyze_operation (surface, op, source);

    assert (_cairo_os2_printing_surface_operation_supported (surface, op, source));

    return _cairo_os2_printing_surface_paint_pattern (surface, source);
}


/*
 * stroke
 */

static cairo_int_status_t
_cairo_os2_printing_surface_stroke (void                  *abstract_surface,
                                    cairo_operator_t       op,
                                    const cairo_pattern_t *source,
                                    cairo_path_fixed_t    *path,
                                    const cairo_stroke_style_t  *style,
                                    const cairo_matrix_t        *stroke_ctm,
                                    const cairo_matrix_t        *stroke_ctm_inverse,
                                    double                 tolerance,
                                    cairo_antialias_t      antialias,
                                    cairo_clip_t          *clip)
{
    cairo_os2_surface_t *surface = abstract_surface;
    cairo_int_status_t status;
    MATRIXLF mlf;
    unsigned int i;
    cairo_solid_pattern_t clear;
    cairo_matrix_t mat;
    double scale;
    LINEBUNDLE lb;

    status = _cairo_surface_clipper_set_clip (&surface->clipper, clip);
    if (status)
        return status;

    if (op == CAIRO_OPERATOR_CLEAR) {
        _cairo_os2_printing_surface_init_clear_color (surface, &clear);
        source = (cairo_pattern_t*) &clear;
        op = CAIRO_OPERATOR_SOURCE;
    }

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE) {
        /* OS/2 doesn't support user-styled dashed lines, and trying
         * to map cairo's user styling to its few built-in line types
         * produces unsatisfactory results, so use fallback images.
         */
        if (style->num_dashes)
            return CAIRO_INT_STATUS_UNSUPPORTED;

        return _cairo_os2_printing_surface_analyze_operation (surface, op, source);
    }

    assert (_cairo_os2_printing_surface_operation_supported (surface, op, source));
    assert (style->num_dashes == 0);

    cairo_matrix_multiply (&mat, stroke_ctm, &surface->ctm);
    _cairo_matrix_factor_out_scale (&mat, &scale);

    /* For this function, the only area attribute that needs to be
     * changed from the default is foreground color, and then only
     * if we're using GpiStrokePath()
     */
    if (source->type == CAIRO_PATTERN_TYPE_SOLID) {
        status = _cairo_os2_printing_surface_set_area_color (surface, source);
        if (status)
            return status;
    }

    lb.lGeomWidth = (LONG) (scale * style->line_width + 0.5);
    lb.usType = LINETYPE_SOLID;
    lb.usEnd = _cairo_os2_printing_surface_line_cap (style->line_cap);
    lb.usJoin = _cairo_os2_printing_surface_line_join (style->line_join);

    if (!GpiSetAttrs (surface->hps,
                      PRIM_LINE,
                      LBB_GEOM_WIDTH | LBB_TYPE | LBB_END | LBB_JOIN,
                      0,
                      &lb))
        return _gpi_error ("stroke - GpiSetAttrs 2");

    if (!GpiBeginPath (surface->hps, 1))
        return _gpi_error ("stroke - GpiBeginPath");
    status = _cairo_os2_printing_surface_emit_path (surface, path);
    if (!GpiEndPath (surface->hps))
        return _gpi_error ("stroke - GpiEndPath");
    if (status)
        return status;

    /*
     * Switch to user space to set line parameters
     */
    if (GpiSavePS (surface->hps) == GPI_ERROR)
        return _gpi_error ("stroke - GpiSavePS");

    _cairo_matrix_to_os2_matrixlf (&mat, &mlf);
    mlf.lM31 = 0;
    mlf.lM32 = 0;

    if (!GpiSetModelTransformMatrix (surface->hps, 9, &mlf, TRANSFORM_PREEMPT))
        return _gpi_error ("stroke - GpiSetModelTransformMatrix (preempt)");

    if (source->type == CAIRO_PATTERN_TYPE_SOLID) {
        if (GpiStrokePath (surface->hps, 1, 0) == GPI_ERROR)
            return _gpi_error ("stroke - GpiStrokePath");
    } else {
        if (!GpiModifyPath (surface->hps, 1, MPATH_STROKE))
            return _gpi_error ("stroke - GpiModifyPath");
        if (!GpiSetClipPath (surface->hps, 1, SCP_WINDING | SCP_AND | SCP_INCL))
            return _gpi_error ("stroke - GpiSetClipPath");

        /* Return to device space to paint the pattern */
        _cairo_matrix_to_os2_matrixlf (&surface->gpi_ctm, &mlf);
        if (!GpiSetModelTransformMatrix (surface->hps, 9, &mlf, TRANSFORM_REPLACE))
            return _gpi_error ("stroke - GpiSetModelTransformMatrix (replace)");
        status = _cairo_os2_printing_surface_paint_pattern (surface, source);
    }
    if (!GpiRestorePS (surface->hps, -1))
        return _gpi_error ("stroke - GpiRestorePS");

    return status;
}


/*
 * fill
 */

static cairo_int_status_t
_cairo_os2_printing_surface_fill (void                  *abstract_surface,
                                  cairo_operator_t       op,
                                  const cairo_pattern_t *source,
                                  cairo_path_fixed_t    *path,
                                  cairo_fill_rule_t      fill_rule,
                                  double                 tolerance,
                                  cairo_antialias_t      antialias,
                                  cairo_clip_t          *clip)
{
    cairo_os2_surface_t *surface = abstract_surface;
    cairo_int_status_t status;
    cairo_solid_pattern_t clear;
    LONG fill_mode;

    status = _cairo_surface_clipper_set_clip (&surface->clipper, clip);
    if (status)
        return status;

    if (op == CAIRO_OPERATOR_CLEAR) {
        _cairo_os2_printing_surface_init_clear_color (surface, &clear);
        source = (cairo_pattern_t*) &clear;
        op = CAIRO_OPERATOR_SOURCE;
    }

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE)
        return _cairo_os2_printing_surface_analyze_operation (surface, op, source);

    assert (_cairo_os2_printing_surface_operation_supported (surface, op, source));

    surface->path_empty = TRUE;

    if (!GpiBeginPath (surface->hps, 1))
        return _gpi_error ("fill - GpiBeginPath");
    status = _cairo_os2_printing_surface_emit_path (surface, path);
    if (!GpiEndPath (surface->hps))
        return _gpi_error ("fill - GpiEndPath");

    /* note:  FPATH_* == SCP_* */
    switch (fill_rule) {
    case CAIRO_FILL_RULE_WINDING:
        fill_mode = FPATH_WINDING;
        break;
    case CAIRO_FILL_RULE_EVEN_ODD:
        fill_mode = FPATH_ALTERNATE;
        break;
    default:
        fill_mode = FPATH_WINDING;
        ASSERT_NOT_REACHED;
    }

    if (source->type == CAIRO_PATTERN_TYPE_SOLID) {
        status = _cairo_os2_printing_surface_set_area_color (surface, source);
        if (status)
            return status;
        if (GpiFillPath (surface->hps, 1, fill_mode) == GPI_ERROR)
            return _gpi_error ("fill - GpiFillPath");
    } else if (surface->path_empty == FALSE) {
        if (GpiSavePS (surface->hps) == GPI_ERROR)
            return _gpi_error ("fill - GpiSavePS");
        if (!GpiSetClipPath (surface->hps, 1, fill_mode | SCP_AND | SCP_INCL))
            return _gpi_error ("fill - GpiSetClipPath");
        status = _cairo_os2_printing_surface_paint_pattern (surface, source);
        if (!GpiRestorePS (surface->hps, -1))
            return _gpi_error ("fill - GpiRestorePS");
    }

    return status;
}


/*
 * glyphs
 */

static cairo_int_status_t
_cairo_os2_printing_surface_show_glyphs (void                   *abstract_surface,
                                         cairo_operator_t        op,
                                         const cairo_pattern_t  *source,
                                         cairo_glyph_t          *glyphs,
                                         int                     num_glyphs,
                                         cairo_scaled_font_t    *scaled_font,
                                         cairo_clip_t           *clip,
                                         int                    *remaining_glyphs)
{
    cairo_os2_surface_t *surface = abstract_surface;
    cairo_status_t status = CAIRO_STATUS_SUCCESS;
    cairo_scaled_glyph_t *scaled_glyph;
    int i;
    cairo_matrix_t old_ctm;
    cairo_bool_t old_has_ctm;
    cairo_solid_pattern_t clear;

    status = _cairo_surface_clipper_set_clip (&surface->clipper, clip);
    if (status)
        return status;

    if (op == CAIRO_OPERATOR_CLEAR) {
        _cairo_os2_printing_surface_init_clear_color (surface, &clear);
        source = (cairo_pattern_t*) &clear;
        op = CAIRO_OPERATOR_SOURCE;
    }

    if (surface->paginated_mode == CAIRO_PAGINATED_MODE_ANALYZE) {

        /* Check that each glyph has a path available. If a path is
         * not available, _cairo_scaled_glyph_lookup() will return
         * CAIRO_INT_STATUS_UNSUPPORTED and a fallback image will be
         * used.
         */
        for (i = 0; i < num_glyphs; i++) {
            status = _cairo_scaled_glyph_lookup (scaled_font,
                                                 glyphs[i].index,
                                                 CAIRO_SCALED_GLYPH_INFO_PATH,
                                                 &scaled_glyph);
            if (status)
                return status;
        }

        return _cairo_os2_printing_surface_analyze_operation (surface, op, source);
    }

    if (GpiSavePS (surface->hps) == GPI_ERROR)
        return _gpi_error ("show_glyphs - GpiSavePS");
    old_ctm = surface->ctm;
    old_has_ctm = surface->has_ctm;
    surface->has_ctm = TRUE;
    surface->path_empty = TRUE;
    if (!GpiBeginPath (surface->hps, 1))
        return _gpi_error ("show_glyphs - GpiBeginPath");
    for (i = 0; i < num_glyphs; i++) {
        status = _cairo_scaled_glyph_lookup (scaled_font,
                                             glyphs[i].index,
                                             CAIRO_SCALED_GLYPH_INFO_PATH,
                                             &scaled_glyph);
        if (status)
            break;
        surface->ctm = old_ctm;
        cairo_matrix_translate (&surface->ctm, glyphs[i].x, glyphs[i].y);
        status = _cairo_os2_printing_surface_emit_path (surface, scaled_glyph->path);
    }
    if (!GpiEndPath (surface->hps))
        return _gpi_error ("show_glyphs - GpiEndPath");
    surface->ctm = old_ctm;
    surface->has_ctm = old_has_ctm;
    if (status == CAIRO_STATUS_SUCCESS && surface->path_empty == FALSE) {
        if (source->type == CAIRO_PATTERN_TYPE_SOLID) {
            status = _cairo_os2_printing_surface_set_area_color (surface, source);
            if (status)
                return status;
            if (GpiFillPath (surface->hps, 1, FPATH_WINDING) == GPI_ERROR)
                return _gpi_error ("show_glyphs - GpiFillPath");

        } else {
            if (!GpiSetClipPath (surface->hps, 1, SCP_WINDING | SCP_AND | SCP_INCL))
                return _gpi_error ("show_glyphs - GpiSetClipPath");
            status = _cairo_os2_printing_surface_paint_pattern (surface, source);
        }
    }
    if (!GpiRestorePS (surface->hps, -1))
        return _gpi_error ("show_glyphs - GpiRestorePS");

    return status;
}


/*
 * additional backend functions
 */

static cairo_surface_t *
_cairo_os2_printing_surface_create_similar (void            *abstract_surface,
                                            cairo_content_t  content,
                                            int              width,
                                            int              height)
{
    cairo_rectangle_t extents;

    extents.x = extents.y = 0;
    extents.width  = width;
    extents.height = height;
    return cairo_recording_surface_create (content, &extents);
}


static cairo_status_t
_cairo_os2_printing_surface_finish (void *abstract_surface)
{
    return CAIRO_STATUS_SUCCESS;
}


static cairo_int_status_t
_cairo_os2_printing_surface_show_page (void *abstract_surface)
{
    cairo_os2_surface_t *surface = abstract_surface;

    /* Undo both GpiSavePS's that we did in start_page */
    if (!GpiRestorePS (surface->hps, -2))
        return _gpi_error ("show_page - GpiRestorePS");

    return CAIRO_STATUS_SUCCESS;
}


static cairo_bool_t
_cairo_os2_printing_surface_get_extents (void                  *abstract_surface,
                                         cairo_rectangle_int_t *rectangle)
{
    cairo_os2_surface_t *surface = abstract_surface;

    rectangle->x = 0;
    rectangle->y = 0;
    rectangle->width  = surface->width;
    rectangle->height = surface->height;

    return TRUE;
}


static void
_cairo_os2_printing_surface_get_font_options (void                 *abstract_surface,
                                              cairo_font_options_t *options)
{
    _cairo_font_options_init_default (options);

    cairo_font_options_set_hint_style (options, CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_hint_metrics (options, CAIRO_HINT_METRICS_OFF);
    cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_GRAY);
}


/*
 * printing surface backend
 */

static const cairo_surface_backend_t cairo_os2_printing_surface_backend = {
    CAIRO_SURFACE_TYPE_OS2_PRINTING,
    _cairo_os2_printing_surface_create_similar,
    _cairo_os2_printing_surface_finish,
    NULL, /* acquire_source_image */
    NULL, /* release_source_image */
    NULL, /* acquire_dest_image */
    NULL, /* release_dest_image */
    NULL, /* clone_similar */
    NULL, /* composite */
    NULL, /* fill_rectangles */
    NULL, /* composite_trapezoids */
    NULL, /* create_span_renderer */
    NULL, /* check_span_renderer */
    NULL, /* copy_page */
    _cairo_os2_printing_surface_show_page,
    _cairo_os2_printing_surface_get_extents,
    NULL, /* old_show_glyphs */
    _cairo_os2_printing_surface_get_font_options,
    NULL, /* flush */
    NULL, /* mark_dirty_rectangle */
    NULL, /* scaled_font_fini */
    NULL, /* scaled_glyph_fini */
    _cairo_os2_printing_surface_paint,
    NULL, /* mask */
    _cairo_os2_printing_surface_stroke,
    _cairo_os2_printing_surface_fill,
    _cairo_os2_printing_surface_show_glyphs,
    NULL, /* snapshot */
    NULL, /* is_similar */
    NULL, /* fill_stroke */
    NULL, /* create_solid_pattern_surface */
    NULL, /* can_repaint_solid_pattern_surface */
    NULL, /* has_show_text_glyphs */
    NULL  /* show_text_glyphs */
};


/*
 * paginated surface backend functions
 */

static cairo_int_status_t
_cairo_os2_paginated_surface_start_page (void *abstract_surface)
{
    cairo_os2_surface_t *surface = abstract_surface;
    MATRIXLF mlf;
    HDC hdc;
    LONG xy_res[2];
    double x_res, y_res;
    cairo_matrix_t inverse_ctm;
    cairo_status_t status;

    /* Rather than trying to translate every coordinate for every
     * call from cairo to PM coordinate space, the entire image is
     * generated upside-down using cairo coordinates.  This viewing
     * transform then flips and repositions the entire image to map
     * it into PM coordinate space.  Note:  GpiEnableYInversion()
     * should accomplish the same result - but it doesn't.
     */
    memset (&mlf, 0, sizeof (mlf));
    mlf.fxM11 = (1 << 16);
    mlf.fxM22 = (-1 << 16);
    mlf.lM32  = surface->height - 1;
    mlf.lM33  = 1;
    if (!GpiSetDefaultViewMatrix (surface->hps, 9, &mlf, TRANSFORM_REPLACE))
        return _gpi_error ("start_page - GpiSetDefaultViewMatrix");

    /* Set full-color (32-bit) mode */
    if (!GpiCreateLogColorTable (surface->hps, 0, LCOLF_RGB, 0, 0, 0))
        return _gpi_error ("start_page - GpiCreateLogColorTable");

    /* Save application context first */
    if (GpiSavePS (surface->hps) == GPI_ERROR)
        return _gpi_error ("start_page - GpiSavePS 1");

    /* As the logical coordinates used by GDI functions (eg LineTo)
     * are integers we need to do some additional work to prevent
     * rounding errors. For example the obvious way to paint a recording
     * pattern is to:
     *
     *   SaveDC()
     *   transform the device context DC by the pattern to device matrix
     *   replay the recording surface
     *   RestoreDC()
     *
     * The problem here is that if the pattern to device matrix is
     * [100 0 0 100 0 0], coordinates in the recording pattern such as
     * (1.56, 2.23) which correspond to (156, 223) in device space
     * will be rounded to (100, 200) due to (1.56, 2.23) being
     * truncated to integers.
     *
     * This is solved by saving the current GDI CTM in surface->ctm,
     * switch the GDI CTM to identity, and transforming all
     * coordinates by surface->ctm before passing them to GDI. When
     * painting a recording pattern, surface->ctm is transformed by the
     * pattern to device matrix.
     *
     * For printing device contexts where 1 unit is 1 dpi, switching
     * the GDI CTM to identity maximises the possible resolution of
     * coordinates.
     *
     * If the device context is an EMF file, using an identity
     * transform often provides insufficent resolution. The workaround
     * is to set the GDI CTM to a scale < 1 eg [1.0/16 0 0 1/0/16 0 0]
     * and scale the cairo CTM by [16 0 0 16 0 0]. The
     * SetWorldTransform function call to scale the GDI CTM by 1.0/16
     * will be recorded in the EMF followed by all the graphics
     * functions by their coordinateds multiplied by 16.
     *
     * To support allowing the user to set a GDI CTM with scale < 1,
     * we avoid switching to an identity CTM if the CTM xx and yy is < 1.
     */

    if (!GpiQueryModelTransformMatrix (surface->hps, 9, &mlf))
        return _gpi_error ("start_page - GpiQueryModelTransformMatrix");

    if (mlf.fxM11 < (1 << 16) && mlf.fxM22 < (1 << 16)) {
        cairo_matrix_init_identity (&surface->ctm);
        _cairo_os2_matrixlf_to_matrix (&mlf, &surface->gpi_ctm);
    } else {
        _cairo_os2_matrixlf_to_matrix (&mlf, &surface->ctm);
        cairo_matrix_init_identity (&surface->gpi_ctm);

        memset (&mlf, 0, sizeof (mlf));
        mlf.fxM11 = (1 << 16);
        mlf.fxM22 = (1 << 16);
        mlf.lM33  = 1;
        if (!GpiSetModelTransformMatrix (surface->hps, 9, &mlf, TRANSFORM_REPLACE))
            return _gpi_error ("start_page - GpiSetModelTransformMatrix");
    }

    surface->has_ctm = !_cairo_matrix_is_identity (&surface->ctm);
    surface->has_gpi_ctm = !_cairo_matrix_is_identity (&surface->gpi_ctm);
    inverse_ctm = surface->ctm;
    status = cairo_matrix_invert (&inverse_ctm);
    if (status)
        return status;

    hdc = GpiQueryDevice (surface->hps);
    if (!hdc)
        return _gpi_error ("start_page - GpiQueryDevice");

    /* For printers, CAPS_*_FONT_RES == actual printer resolution in DPI */
    if (!DevQueryCaps (hdc, CAPS_HORIZONTAL_FONT_RES, 2, (PLONG)&xy_res))
        return _gpi_error ("start_page - DevQueryCaps");
    x_res = xy_res[0];
    y_res = xy_res[1];
    cairo_matrix_transform_distance (&inverse_ctm, &x_res, &y_res);

    _cairo_surface_set_resolution (&surface->base, x_res, y_res);
    _cairo_surface_set_resolution (surface->paginated_surf, x_res, y_res);

    /* ensure fallback images are at the same res as the primary surfaces */
    cairo_surface_set_fallback_resolution (&surface->base, x_res, y_res);
    cairo_surface_set_fallback_resolution (surface->paginated_surf, x_res, y_res);

    /* Save Cairo's known-good clip state, so the clip path can be reset */
    if (GpiSavePS (surface->hps) == GPI_ERROR)
        return _gpi_error ("start_page - GpiSavePS 2");

    return CAIRO_STATUS_SUCCESS;
}


static void
_cairo_os2_paginated_surface_set_paginated_mode (void                   *abstract_surface,
                                                cairo_paginated_mode_t  paginated_mode)
{
    cairo_os2_surface_t *surface = abstract_surface;

    surface->paginated_mode = paginated_mode;
}


static cairo_bool_t
_cairo_os2_paginated_surface_supports_fine_grained_fallbacks (void *abstract_surface)
{
    return TRUE;
}


/*
 * paginated surface backend
 */

static const cairo_paginated_surface_backend_t cairo_os2_paginated_surface_backend = {
    _cairo_os2_paginated_surface_start_page,
    _cairo_os2_paginated_surface_set_paginated_mode,
    NULL, /* set_bounding_box */
    NULL, /* set_fallback_images_required */
    _cairo_os2_paginated_surface_supports_fine_grained_fallbacks
};

