/*
  File: pty_fork.c

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
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#define _GNU_SOURCE
#include <stdlib.h>
#include <fcntl.h>
#include <stdlib.h>

#include "pty_fork.h"

/*
  See the "Terminal I/O" and "Pseudo Terminal" chapters in "Advanced
  Programming in the UNIX Environment", Second Edition, W. Richard Stevens
  and Stephen A. Rago, Addison Wesley, 2008.
*/

pid_t pty_fork(int *fd)
{
	pid_t pid;
	int ptm;

	ptm = posix_openpt(O_RDWR);
	if (ptm < 0)
		return -1;
	if (grantpt(ptm) != 0 || unlockpt(ptm) != 0) {
		close(ptm);
		return -1;
	}
	pid = fork();
	if (pid < 0) {
		close(ptm);
		return -1;
	}
	if (pid == 0) {
		const char *pts_name;

		if (setsid() < 0)
			exit(1);
		pts_name = ptsname(ptm);
		if (pts_name == NULL)
			exit(1);
		close(STDIN_FILENO);
		if (open(pts_name, O_RDWR) != STDIN_FILENO)
			exit(1);
		close(ptm);
#if defined TIOCSCTTY
		/* TIOCSCTTY is the BSD way to acquire a controlling
		   terminal. */
		if (ioctl(STDIN_FILENO, TIOCSCTTY, (char *)0) < 0)
			exit(1);
#endif
		dup2(STDIN_FILENO, STDOUT_FILENO);
		return 0;
	} else {
		*fd = ptm;
		return pid;
	}
}
