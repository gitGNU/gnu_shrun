/*
  File: shrun.c

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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>

#define _GNU_SOURCE
#include <getopt.h>

#include "queue.h"

#define max(a, b) ((a) > (b) ? (a) : (b))

/* TODO:
 * - var expansion in the output?
 * - regexp comparison?
 * - implement a verbose mode that shows all the output of successful
 *   commands as well.
 */

enum { PIPE_READ, PIPE_WRITE };

const char *ansi_red = "\033[31m\033[1m";
const char *ansi_green = "\033[32m";
const char *ansi_clear = "\033[m";

const char *end_marker_cmd = "echo $'\\4'\n";
const char *restore_stdin_cmd = "exec <&111-\n";
const char *control_cmds = "timeout() { echo \"timeout $1\" >&109; }\n";

const char *progname;
char *tempdir_name, *stdin_fifo_name;

const char *opt_shell = "/bin/sh";
unsigned int opt_timeout = 5;
unsigned int opt_stop_at = (unsigned int)-1;
int opt_stderr = 1;
int opt_color = -1;

static int append_line(struct queue *queue, const char *line, size_t sz)
{
	size_t append_newline = 1;
	char *buf;

	line++;
	sz--;
	if (sz && (*line == ' ')) {
		line++;
		sz--;
	}

	if (sz && line[sz - 1] == '\n')
		append_newline = 0;
	buf = queue_write_pos(queue, sz + append_newline, NULL);
	if (!buf)
		return -1;
	memcpy(buf, line, sz);
	if (append_newline)
		buf[sz] = '\n';
	queue_advance_write(queue, sz + append_newline);

	return 0;
}

size_t first_lineno = 1;

static int read_testcase(struct queue *script, int eof, struct queue *testcase,
			 struct queue *expected, struct queue *input,
			 int preamble)
{
	static size_t lineno = 1;
	char *buf;
	ssize_t sz;

	for (;;) {
		char *end, *newline, *l;

		buf = queue_read_pos(script, &sz);
		if (!buf)
			break;
		newline = memchr(buf, '\n', sz);
		if (newline)
			sz = newline - buf + 1;
		else if (!eof)
			break;

		l = buf; end = buf + sz;
		while (l < end && (*l == ' ' || *l == '\t'))
			l++;
		if (l < end && (*l == '$' ||
				queue_length(testcase) > preamble)) {
			switch(*l) {
			case '$': case '+':
				if (*l == '$' &&
				    queue_length(testcase) > preamble) {
					eof = 1;
					goto done;
				}

				if (*l == '$') {
					first_lineno = lineno;
					if (opt_stop_at <= first_lineno)
						return 0;
				}
				if (append_line(testcase, l, end - l) != 0)
					return -1;
				break;

			case '>':
				if (append_line(expected, l, end - l) != 0)
					return -1;
				break;

			case '<':
				if (append_line(input, l, end - l) != 0)
					return -1;
				break;
			}
		}
		queue_advance_read(script, sz);
		lineno++;
	}

done:
	return eof;
}

static int create_stdin_fifo(void) {
	if (!stdin_fifo_name) {
		const char *tmpdir;
		size_t len;

		tmpdir = getenv("TMPDIR");
		if (!tmpdir)
			tmpdir = "/tmp";
		len = strlen(tmpdir) + 1 + strlen(progname) + 8;
		tempdir_name = malloc(len);
		stdin_fifo_name = malloc(len + 6);
		if (!tempdir_name || !stdin_fifo_name) {
			perror(progname);
			return -1;
		}
		sprintf(tempdir_name, "%s/%s.XXXXXX", tmpdir, progname);
		if (!mkdtemp(tempdir_name)) {
			fprintf(stderr, "%s: %s: %s\n",
				progname, tempdir_name, strerror(errno));
			return -1;
		}
		sprintf(stdin_fifo_name, "%s/stdin", tempdir_name);
	} else {
		/*
		 * It is not guaranteed that the read side of the pipe
		 * has been closed already (e.g., a timeout may have
		 * occurred, or the process may have forked). If still
		 * open, we would reopen the pipe to that old process,
		 * and lose synchronization with the new process in
		 * redirect_stdin(). To avoid this, unlink and recreate
		 * the pipe here.
		 */
		unlink(stdin_fifo_name);
	}
	if (mkfifo(stdin_fifo_name, 0700) != 0) {
		fprintf(stderr, "%s: %s: %s\n",
			progname, stdin_fifo_name, strerror(errno));
		return -1;
	}
	return 0;
}

static int redirect_stdin(int out)
{
	static const char *redirect_stdin_fmt = "exec 111<&0- <%s\n";
	char *redirect_stdin;
	size_t sz;
	int ret = 0;

	if (create_stdin_fifo() != 0)
		return -1;
	redirect_stdin = malloc(strlen(redirect_stdin_fmt) +
				strlen(stdin_fifo_name));
	if (!redirect_stdin)
		return -1;
	sz = sprintf(redirect_stdin, redirect_stdin_fmt, stdin_fifo_name);

	if (write(out, redirect_stdin, sz) != sz)
		ret = -1;
	free(redirect_stdin);
	if (ret != -1)
		ret = open(stdin_fifo_name, O_WRONLY);
	return ret;
}

static void report_begin(struct queue *testcase, size_t preamble)
{
	char *buf, *newline;
	ssize_t sz;

	buf = queue_read_pos(testcase, &sz);
	buf += preamble;
	sz -= preamble;
	newline = memchr(buf, '\n', sz);
	if (!newline)
		newline = buf + sz -1;

	printf("[%u] $ %.*s%s -- ",
	       first_lineno, newline - buf, buf,
	       (newline == buf + sz - 1) ? "" : "...");
	fflush(stdout);
}

static int report_end(struct queue *fifo1, struct queue *fifo2,
		      int testcase_eof)
{
	int width = 0;
	char *buf1, *buf2, *l1, *l2, *eol1, *eol2;
	ssize_t sz1, sz2, lz1, lz2;

	buf1 = queue_read_pos(fifo1, &sz1);
	buf2 = queue_read_pos(fifo2, &sz2);
	if (!buf1)
		sz1 = 0;
	if (!buf2)
		sz2 = 0;
	if (testcase_eof && sz1 == sz2 && memcmp(buf1, buf2, sz1) == 0) {
		printf("%s%s%s\n", ansi_green, "ok", ansi_clear);
		return 0;
	}
	if (!testcase_eof) {
		printf("%s%s%s\n", ansi_red, "short result", ansi_clear);
		return 1;
	}
	printf("%s%s%s\n", ansi_red, "failed", ansi_clear);

	l1 = buf1;
	eol1 = l1 + sz1;
	lz1 = 0;
	for (;;) {
		if (l1 == eol1 || *l1 == '\n') {
			if (lz1 > width)
				width = lz1;
			if (l1 == eol1)
				break;
			lz1 = -1;
		}
		l1++;
		lz1++;
	}
	l2 = buf2;
	eol2 = l2 + sz2;
	lz2 = 0;
	for (;;) {
		if (l2 == eol2 || *l2 == '\n') {
			if (lz2 > width)
				width = lz2;
			if (l2 == eol2)
				break;
			lz2 = -1;
		}
		l2++;
		lz2++;
	}

	l1 = buf1;
	l2 = buf2;
	while (sz1 || sz2) {
		int eq;

		eol1 = memchr(l1, '\n', sz1);
		if (!eol1) {
				lz1 = sz1;
				eol1 = l1 + sz1;
		} else {
			lz1 = eol1 - l1;
			eol1++;
		}
		eol2 = memchr(l2, '\n', sz2);
		if (!eol2) {
			lz2 = sz2;
			eol2 = l2 + sz2;
		} else {
			lz2 = eol2 - l2;
			eol2++;
		}
		eq = (lz1 == lz2 && memcmp(l1, l2, lz1) == 0);
		if (!sz1) {
			l1 = "~"; eol1 = l1;
			lz1 = 1;
		}
		if (!sz2) {
			l2 = "~"; eol2 = l2;
			lz2 = 1;
		}

		printf("%s%-*.*s%s %c %s%.*s%s\n",
		       eq ? "" : ansi_red, width, lz1, l1, ansi_clear,
		       eq ? '|' : '?',
		       eq ? "" : ansi_green, lz2, l2, ansi_clear);

		sz1 -= eol1 - l1; l1 = eol1;
		sz2 -= eol2 - l2; l2 = eol2;
	}
	return 1;
}

int interrupted;
void catch_interrupted(int signal)
{
	interrupted = 1;
}

pid_t child_pid;
#if 0
int child_status;
void catch_child_died(int signal)
{
	int saved_errno;

	saved_errno = errno;
	waitpid(child_pid, &child_status, WNOHANG);
	errno = saved_errno;
}
#endif

static int interactive(int in, int out)
{
	struct queue input, output;
	sigset_t sigset;
	int stdin_eof = 0, interactive_eof = 0, retval = 0;

	sigemptyset(&sigset);
	queue_init(&input);
	queue_init(&output);

	printf("%sinteractive; press ^D to continue%s\n",
	      ansi_red, ansi_clear);
	fflush(stdout);

	for(;;) {
		fd_set rfds, wfds;
		int maxfd = 0, retval2;

		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		if (!stdin_eof) {
			FD_SET(STDIN_FILENO, &rfds);
			maxfd = max(maxfd, STDIN_FILENO + 1);
		}
		if (!queue_empty(&input)) {
			FD_SET(out, &wfds);
			maxfd = max(maxfd, out + 1);
		}
		FD_SET(in, &rfds);
		maxfd = max(maxfd, in + 1);
		do {
			retval2 = pselect(maxfd, &rfds, &wfds, NULL, NULL,
					  &sigset);
		} while (retval2 < 0 && errno == EINTR && !interrupted);
		if (retval2 < 0)
			break;
		if (FD_ISSET(STDIN_FILENO, &rfds)) {
			char *buf;
			ssize_t sz;

			buf = queue_write_pos(&input, 256, &sz);
			if (!buf)
				break;
			sz = read(STDIN_FILENO, buf, sz);
			if (sz < 0)
				break;
			queue_advance_write(&input, sz);
			if (sz == 0) {
				stdin_eof = 1;

				if (queue_append(&input, end_marker_cmd) != 0)
					return -1;
			}
		}
		if (FD_ISSET(out, &wfds)) {
			char *buf;
			ssize_t sz;

			buf = queue_read_pos(&input, &sz);
			if (buf) {
				sz = write(out, buf, sz);
				if (sz < 0)
					break;
			}
			queue_advance_read(&input, sz);
		}
		if (FD_ISSET(in, &rfds)) {
			char *buf;
			ssize_t sz;

			buf = queue_write_pos(&output, 256, &sz);
			if (!buf)
				break;
			sz = read(in, buf, sz);
			if (sz <= 0)
				break;
			queue_advance_write(&output, sz);

			buf = queue_read_pos(&output, &sz);
			if (buf && sz >= 2 && buf[sz - 2] == '\4' &&
			    buf[sz - 1] == '\n') {
				interactive_eof = 1;
				queue_erase_tail(&output, 2);
			}
			for(;;) {

				buf = queue_read_pos(&output, &sz);
				if (!buf)
					break;
				sz = write(STDOUT_FILENO, buf, sz);
				if (sz <= 0)
					break;
				queue_advance_read(&output, sz);
			}
			if (interactive_eof)
				goto out;
		}
	}
	retval = -1;

out:
	queue_destroy(&input);
	queue_destroy(&output);
	return retval;
}

static int shrun(int script_fd, int in, int out, int control_fd)
{
	struct queue script, control, testcase, expected, input, output;
	int script_eof = 0, in_eof = 0, testcase_eof = 0;
	int reading_testcase = 1, retval = 0;
	size_t passed = 0, failed = 0, timed_out = 0, preamble = 0;
	int stdin_fd = -1;
	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGCHLD);
	sigprocmask(SIG_BLOCK, &sigset, NULL);
	sigemptyset(&sigset);

	signal(SIGHUP, catch_interrupted);
	signal(SIGINT, catch_interrupted);
	signal(SIGCHLD, SIG_IGN /*catch_child_died*/);
	signal(SIGPIPE, SIG_IGN);

	queue_init(&script);
	queue_init(&control);
	queue_init(&testcase);
	queue_init(&expected);
	queue_init(&input);
	queue_init(&output);

	if (queue_append(&testcase, control_cmds) != 0)
		return -1;
	preamble = queue_length(&testcase);

	for(;;) {
		fd_set rfds, wfds;
		int maxfd = 0, retval2;
		struct timespec timeout, *ptimeout = NULL;

		if (!reading_testcase && (testcase_eof || in_eof)) {
			if (report_end(&output, &expected, testcase_eof) == 0)
				passed++;
			else
				failed++;
			queue_reset(&expected);
			queue_reset(&input);
			queue_reset(&output);
			if (stdin_fd != -1) {
				close(stdin_fd);
				stdin_fd = -1;
			}
			reading_testcase = 1;
			preamble = 0;
		}
		if (reading_testcase) {
			if (script_eof && queue_empty(&script) &&
			    queue_length(&testcase) == preamble)
				goto out;

			retval2 = read_testcase(&script, script_eof, &testcase,
						&expected, &input, preamble);
			if (retval2 < 0)
				break;
			if (retval2 > 0) {
				report_begin(&testcase, preamble);

				if (!queue_empty(&input)) {
					stdin_fd = redirect_stdin(out);
					if (stdin_fd == -1)
						break;
					if (queue_append(&testcase,
							restore_stdin_cmd) != 0)
						break;
				}
				if (queue_append(&testcase,
						 end_marker_cmd) != 0)
					return -1;
				reading_testcase = 0;
				testcase_eof = 0;
			}
		}
		if (opt_stop_at <= first_lineno) {
			retval2 = interactive(in, out);
			if (retval2 < 0)
				break;
			opt_stop_at = (unsigned int)-1;
		}

		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		if (reading_testcase) {
			if (!script_eof) {
				FD_SET(script_fd, &rfds);
				maxfd = max(maxfd, script_fd + 1);
			}
		} else {
			if (!in_eof) {
				FD_SET(in, &rfds);
				maxfd = max(maxfd, in + 1);
			}
			if (!queue_empty(&testcase)) {
				FD_SET(out, &wfds);
				maxfd = max(maxfd, out + 1);
			}
			if (stdin_fd != -1) {
				FD_SET(stdin_fd, &wfds);
				maxfd = max(maxfd, stdin_fd + 1);
			}

			if (opt_timeout) {
				ptimeout = &timeout;
				timeout.tv_sec = opt_timeout;
				timeout.tv_nsec = 0;
			}
		}
		if (control_fd != -1) {
			FD_SET(control_fd, &rfds);
			maxfd = max(maxfd, control_fd + 1);
		}

		do {
			retval2 = pselect(maxfd, &rfds, &wfds, NULL,
				  ptimeout, &sigset);
		} while (retval2 < 0 && errno == EINTR && !interrupted);
		if (retval2 < 0 || interrupted)
			break;
		if (!reading_testcase && retval2 == 0) {
			timed_out = 1;
			break;
		}

		if (FD_ISSET(script_fd, &rfds)) {
			char *buf;
			ssize_t sz;

			buf = queue_write_pos(&script, 256, &sz);
			if (!buf)
				break;
			sz = read(script_fd, buf, sz);
			if (sz < 0)
				break;
			queue_advance_write(&script, sz);
			if (sz == 0)
				script_eof = 1;
		}
		if ((stdin_fd != -1 && FD_ISSET(stdin_fd, &wfds)) ||
		    (!reading_testcase && in_eof)) {
			char *buf;
			ssize_t sz;

			buf = queue_read_pos(&input, &sz);
			if (!in_eof) {
				sz = write(stdin_fd, buf, sz);
				if (sz < 0)
					break;
			}
			queue_advance_read(&input, sz);
			if (queue_empty(&input)) {
				close(stdin_fd);
				stdin_fd = -1;
			}
		}
		if (FD_ISSET(out, &wfds) || (!reading_testcase && in_eof)) {
			char *buf;
			ssize_t sz;

			buf = queue_read_pos(&testcase, &sz);
			if (!in_eof) {
				sz = write(out, buf, sz);
				if (sz < 0)
					break;
			}
			queue_advance_read(&testcase, sz);
		}
		if (FD_ISSET(in, &rfds)) {
			char *buf;
			ssize_t sz;

			buf = queue_write_pos(&output, 256, &sz);
			if (!buf)
				return -1;
			sz = read(in, buf, sz);
			if (sz == 0)
				in_eof = 1;
			else if (sz < 0)
				break;
			else {
				char *buf;

				queue_advance_write(&output, sz);
				buf = queue_read_pos(&output, &sz);
				if (buf && sz >= 2 && buf[sz - 2] == '\4'
				    && buf[sz - 1] == '\n') {
					testcase_eof = 1;
					queue_erase_tail(&output, 2);
				}
			}
		}
		if (control_fd != -1 && FD_ISSET(control_fd, &rfds)) {
			char *buf;
			ssize_t sz;

			buf = queue_write_pos(&control, 256, &sz);
			if (!buf)
				return -1;
			sz = read(control_fd, buf, sz);
			if (sz == 0) {
				close(control_fd);
				control_fd = -1;
			} else if (sz < 0)
				break;
			else {
				queue_advance_write(&control, sz);
				buf = queue_read_pos(&control, &sz);
				if (buf[sz - 1] == '\n') {
					if (strncmp(buf, "timeout ", 8) == 0)
						opt_timeout = atoi(buf + 8);
					else {
						fprintf(stderr, "%sunknown "
							"control command%s\n",
							ansi_red, ansi_clear);
						break;
					}
					queue_advance_read(&control, sz);
				}
			}
		}
	}
	retval = -1;

out:
	queue_destroy(&script);
	queue_destroy(&control);
	queue_destroy(&testcase);
	queue_destroy(&expected);
	queue_destroy(&input);
	queue_destroy(&output);

	if (stdin_fifo_name)
		unlink(stdin_fifo_name);
	free(stdin_fifo_name);
	rmdir(tempdir_name);
	free(tempdir_name);

	failed++;
	if (timed_out)
		printf("%scommand timed out%s\n",
			ansi_red, ansi_clear);
	else if (interrupted)
		printf("%sinterrupted%s\n",
		       ansi_red, ansi_clear);
	else if (retval == 0) {
		failed--;
		if (passed + failed > 0)
			printf("%s%u commands (%u passed, %u failed)\%s\n",
			       (failed == 0) ? ansi_green : ansi_red,
			       passed + failed, passed, failed, ansi_clear);
	} else
		printf("%s%s%s\n", ansi_red, strerror(errno), ansi_clear);

	if (failed && !retval)
		retval = 1;
	return retval;
}

void usage(int status)
{
	fprintf(status ? stderr : stdout,
		"usage: %s [--timeout n] [--stop-at n] [--shell path]"
		"[--color[={never|always|auto}]] [--no-stderr]\n",
		progname);
	exit(status);
}

struct option long_options[] = {
	{"timeout", 1, NULL, 't'},
	{"stop-at", 1, NULL, 1},
	{"shell", 1, NULL, 2},
	{"color", 2, NULL, 3},
	{"no-stderr", 0, NULL, 4},
	{"help", 0, NULL, 'h'},
	{NULL, 0, NULL, 0}
};

int main(int argc, char *argv[])
{
	int pipe0[2], pipe1[2], control[2];
	int retval = 0;
	int script_fd = STDIN_FILENO;
	int c;

	progname = basename(argv[0]);
	while ((c = getopt_long(argc, argv, "t:h", long_options, NULL)) != -1) {
		switch(c) {
		case 't':
			opt_timeout = atoi(optarg);
			break;

		case 1:  /* --stop-at */
			opt_stop_at = atoi(optarg);
			break;

		case 2:  /*  --shell */
			opt_shell = optarg;
			break;

		case 3:  /* --color */
			if (optarg == NULL || strcmp(optarg, "always") == 0)
				opt_color = 1;
			else if (strcmp(optarg, "never") == 0)
				opt_color = 0;
			else if (strcmp(optarg, "auto") == 0)
				opt_color = -1;
			else
				usage(1);
			break;

		case 4:  /* --no-stderr */
			opt_stderr = 0;
			break;

		case 'h':
			usage(0);
			break;

		case '?':
			return 1;
		}
	}

	if (optind + 1 < argc)
		usage(1);
	if (optind < argc) {
		script_fd = open(argv[optind], O_RDONLY);
		if (script_fd < 0) {
			fprintf(stderr, "%s: %s: %s\n",
			        progname, argv[optind], strerror(errno));
			return 1;
		}
	}
	if (script_fd == STDIN_FILENO)
		opt_stop_at = (unsigned int)-1;
	if (opt_color == 0 || (opt_color == -1 && !isatty(1)))
		ansi_red = ansi_green = ansi_clear = "";

	if (access(opt_shell, X_OK) != 0) {
		/* FIXME: report exec failures properly instead! */
		fprintf(stderr, "%s: %s: %s\n",
			progname, opt_shell, strerror(errno));
		return 1;
	}

	if (pipe(pipe0) != 0 || pipe(pipe1) != 0 || pipe(control)) {
		perror(progname);
		return 1;
	}

	child_pid = fork();
	if (child_pid < 0) {
		perror(progname);
		return 1;
	}

	/*
	 * parent ---pipe0--> shell ---pipe1
	 *    ^                          |
	 *    '--------------------------'
	 */

	if (child_pid == 0) {
		close(pipe0[PIPE_WRITE]);
		close(pipe1[PIPE_READ]);
		close(control[PIPE_READ]);

		dup2(pipe0[PIPE_READ], 110);
		close(pipe0[PIPE_READ]);
		if (script_fd == STDIN_FILENO) {
			close(STDIN_FILENO);
			open("/dev/null", O_RDONLY);
		}
		dup2(pipe1[PIPE_WRITE], STDOUT_FILENO);
		if (opt_stderr)
			dup2(pipe1[PIPE_WRITE], STDERR_FILENO);
		close(pipe1[PIPE_WRITE]);
		dup2(control[PIPE_WRITE], 109);
		close(control[PIPE_WRITE]);

		execl(opt_shell, opt_shell, "/dev/fd/110", NULL);
		fprintf(stderr, "%s%s: %s: %s%s\n",
			ansi_red, progname, opt_shell, strerror(errno),
			ansi_clear);
		return 1;
	} else {
		close(pipe0[PIPE_READ]);
		close(pipe1[PIPE_WRITE]);
		close(control[PIPE_WRITE]);

		if (shrun(script_fd, pipe1[PIPE_READ], pipe0[PIPE_WRITE],
			  control[PIPE_READ]) != 0)
			retval = 1;
	}
	return retval;
}
