#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <strings.h>
#include <fcntl.h>
#include <syslog.h>
#include <error.h>
#include <errno.h>

#include "ueventd.h"

char *logfile;
int log_priority;

void
logging_level(const char *name)
{
	log_priority = 0;

	if (!strcasecmp(name, "debug")) {
		log_priority = LOG_DEBUG;
		return;
	}

	if (!strcasecmp(name, "info")) {
		log_priority = LOG_INFO;
		return;
	}

	if (!strcasecmp(name, "warning")) {
		log_priority = LOG_WARNING;
		return;
	}

	if (!strcasecmp(name, "error")) {
		log_priority = LOG_ERR;
		return;
	}
}

void
logging_init(void)
{
	int fd;

	openlog("uevent", LOG_PID|LOG_PERROR, LOG_DAEMON);

	if (!logfile)
		return;

	if ((fd = open(logfile, O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR)) < 0)
		error(EXIT_FAILURE, errno, "open");

	close(STDOUT_FILENO);
	dup2(fd, STDOUT_FILENO);

	close(STDERR_FILENO);
	dup2(fd, STDERR_FILENO);

	close(fd);
}

void
logging_close(void)
{
	closelog();

	if (!logfile)
		return;

	close(STDOUT_FILENO);
	close(STDERR_FILENO);
}

void
__attribute__ ((format (printf, 2, 3)))
message(const int priority, const char *fmt, ...)
{
	va_list ap;

	if (priority > log_priority)
		return;

	va_start(ap, fmt);
	vsyslog(priority, fmt, ap);
	va_end(ap);
}
