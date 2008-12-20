/*
  File: pty_fork.h

  Copyright (C) 2008 Andreas Gruenbacher <agruen@suse.de>, SUSE Labs

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this program. If not, see http://www.gnu.org/licenses/.
*/

#ifndef __PTY_FORK_H
#define __PTY_FORK_H

#include <unistd.h>

extern pid_t pty_fork(int *fd);

#endif  /* __PTY_FORK_H */
