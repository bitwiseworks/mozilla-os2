/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsgfxdefs_h
#define _nsgfxdefs_h

// nsGfxDefs.h - common includes etc. for gfx library


#define INCL_BASE
#define INCL_PM
#include <os2.h>

#include "nscore.h"

#include "prlog.h"
#include "nsHashtable.h"

#include <uconv.h> // XXX hack XXX

class nsString;
class nsDeviceContext;

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define MK_RGB(r,g,b) ((r) * 65536) + ((g) * 256) + (b)

#endif
