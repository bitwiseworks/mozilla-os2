// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_OS2_PIPE_H_
#define BASE_OS2_PIPE_H_

#include <sys/socket.h>

// Use socketpair instead of pipe to create select-compatible sockets
#define pipe(fds) socketpair(AF_LOCAL, SOCK_STREAM, 0, fds)

#endif  // BASE_OS2_PIPE_H_
