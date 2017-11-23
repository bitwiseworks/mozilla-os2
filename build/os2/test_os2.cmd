/* REXX */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Invoke unit tests on OS/2 */

/*
 * NOTE: we have to use a CMD script rather than sh because setting
 * BEGINLIBPATH and friends isn't supported in dash yet, see
 * http://trac.netlabs.org/ports/ticket/161 for details. Note that
 * we have to call sh back from here rather than continue using CMD
 * because prog may be a symlink (e.g. python in virtualenv).
 */

parse arg dist prog parm
dist_d=translate(dist, '\', '/')
'@set BEGINLIBPATH='dist_d'\bin;%BEGINLIBPATH%'
'@set LIBPATHSTRICT=T'
shell = value('SHELL', ,'OS2ENVIRONMENT')
if shell == '' then shell = 'sh'
'@'shell '-c "'prog parm'"'
exit
