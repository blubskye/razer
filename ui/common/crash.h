#pragma once
/*
 * crash.h — shared crash/signal handler for razercfg C/C++ frontends.
 *
 * Registers handlers for SIGSEGV, SIGABRT, SIGBUS that:
 *   1. Print a stack trace to stderr via backtrace_symbols_fd()
 *   2. Write the same trace to /tmp/<progname>-crash-<pid>.log
 *   3. Re-raise the signal (SA_RESETHAND) to produce a core dump
 *
 * Compile with -g -rdynamic for useful symbol names in the trace.
 *
 * Usage: call install_crash_handler(argv[0]) at the start of main().
 */

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static char _crash_progname[64];

static void _crash_signal_handler(int sig)
{
	void *frames[64];
	int n = backtrace(frames, 64);

	const char *signame = (sig == SIGSEGV) ? "SIGSEGV" :
	                      (sig == SIGABRT) ? "SIGABRT" :
	                      (sig == SIGBUS)  ? "SIGBUS"  : "SIG?";
	(void)write(STDERR_FILENO, "Crashed: ", 9);
	(void)write(STDERR_FILENO, signame, strlen(signame));
	(void)write(STDERR_FILENO, "\n", 1);
	backtrace_symbols_fd(frames, n, STDERR_FILENO);

	/* Log file */
	char path[128];
	/* snprintf is not async-signal-safe, but works in practice here */
	snprintf(path, sizeof(path), "/tmp/%s-crash-%d.log",
	         _crash_progname[0] ? _crash_progname : "razercfg",
	         (int)getpid());
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd >= 0) {
		backtrace_symbols_fd(frames, n, fd);
		close(fd);
	}

	signal(sig, SIG_DFL);
	raise(sig);
}

static inline void install_crash_handler(const char *argv0)
{
	/* Extract basename */
	const char *base = strrchr(argv0, '/');
	base = base ? base + 1 : argv0;
	strncpy(_crash_progname, base, sizeof(_crash_progname) - 1);

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = _crash_signal_handler;
	sa.sa_flags   = SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
	sigaction(SIGBUS,  &sa, NULL);
}
