/* -*- Mode: c; tab-width: 8; c-basic-offset: 4; indent-tabs-mode: t; -*- */
/* Cairo - a vector graphics library with display and print output
 *
 * Copyright © 2005 Red Hat, Inc.
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
 * The Initial Developer of the Original Code is Red Hat, Inc.
 *
 * Contributor(s):
 *	Owen Taylor <otaylor@redhat.com>
 *	Stuart Parmenter <stuart@mozilla.com>
 *	Vladimir Vukicevic <vladimir@pobox.com>
 *  Rich Walsh <rich@e-vertise.com>
 */

/* This file should include code that is system-specific, not
 * feature-specific.  For example, the DLL initialization/finalization
 * code on Win32 or OS/2 must live here (not in cairo-whatever-surface.c).
 * Same about possible ELF-specific code.
 *
 * And no other function should live here.
 */


#include "cairoint.h"



#if CAIRO_MUTEX_IMPL_WIN32
#if !CAIRO_WIN32_STATIC_BUILD

#define WIN32_LEAN_AND_MEAN
/* We require Windows 2000 features such as ETO_PDY */
#if !defined(WINVER) || (WINVER < 0x0500)
# define WINVER 0x0500
#endif
#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0500)
# define _WIN32_WINNT 0x0500
#endif

#include "cairo-clip-private.h"
#include "cairo-paginated-private.h"
#include "cairo-win32-private.h"
#include "cairo-scaled-font-subsets-private.h"

#include <windows.h>

/* declare to avoid "no previous prototype for 'DllMain'" warning */
BOOL WINAPI
DllMain (HINSTANCE hinstDLL,
         DWORD     fdwReason,
         LPVOID    lpvReserved);

BOOL WINAPI
DllMain (HINSTANCE hinstDLL,
         DWORD     fdwReason,
         LPVOID    lpvReserved)
{
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            CAIRO_MUTEX_INITIALIZE ();
            break;

        case DLL_PROCESS_DETACH:
            CAIRO_MUTEX_FINALIZE ();
            break;
    }

    return TRUE;
}

#endif
#endif

#ifdef __OS2__

#include <float.h>
#if CAIRO_HAS_FC_FONT
#include <fontconfig/fontconfig.h>
#endif

cairo_public void
cairo_os2_init (void);
cairo_public void
cairo_os2_fini (void);

static int cairo_os2_init_count = 0;

/**
 * DLL Initialization/Termination functions -
 * not used when Cairo is statically linked.
 *
 **/

#ifdef BUILD_CAIRO_DLL

#ifdef __WATCOMC__
unsigned _System
LibMain (unsigned hmod,
         unsigned termination)
{
    if (termination) {
        cairo_os2_fini ();
        return 1;
    }

    cairo_os2_init ();
    return 1;
}

#else

#include <emx/startup.h>

unsigned long _System
_DLL_InitTerm (unsigned long hmod,
               unsigned long termination)
{
    if (termination) {
        cairo_os2_fini ();
        __ctordtorTerm ();
        _CRT_term ();
        return 1;
    }

    if (_CRT_init ())
        return 0;
    __ctordtorInit ();

    cairo_os2_init ();
    return 1;
}
#endif /* __WATCOMC__ */

#endif /* BUILD_CAIRO_DLL */

/**
 * cairo_os2_init:
 * System-specific initialization.
 *
 * This is called automatically if Cairo is built as a DLL, but must be
 * explicitly invoked if Cairo is used as a statically linked library.
 *
 * Since: 1.4
 **/

cairo_public void
cairo_os2_init (void)
{
    unsigned short usCW;

    cairo_os2_init_count++;
    if (cairo_os2_init_count > 1)
        return;

    /* Workaround a bug in some OS/2 PM API calls that
     * modify the FPU Control Word but fail to restore it.
     */
    usCW = _control87 (0, 0);
    usCW = usCW | EM_INVALID | 0x80;
    _control87 (usCW, MCW_EM | 0x80);

#if CAIRO_HAS_FC_FONT
    FcInit ();
#endif

    CAIRO_MUTEX_INITIALIZE ();
}


/**
 * cairo_os2_init:
 * System-specific finalization.
 *
 * This is called automatically if Cairo is built as a DLL, but must be
 * explicitly invoked if Cairo is used as a statically linked library.
 *
 * Since: 1.4
 **/

cairo_public void
cairo_os2_fini (void)
{
    if (!cairo_os2_init_count)
        return;
    cairo_os2_init_count--;
    if (cairo_os2_init_count)
        return;

#if CAIRO_HAS_FC_FONT
#if HAVE_FCFINI
    FcFini ();
#endif
#endif

    CAIRO_MUTEX_FINALIZE ();

    cairo_debug_reset_static_data ();
}

#endif /* __OS2__ */

