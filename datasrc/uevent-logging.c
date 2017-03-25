#include <stdio.h>
#include <stdarg.h>
#include <strings.h>
#include <syslog.h>

#include "uevent-logging.h"

extern int log_priority;

int
logging_level(const char *name)
{
	if (!strcasecmp(name, "debug"))
		return LOG_DEBUG;

	if (!strcasecmp(name, "info"))
		return LOG_INFO;

	if (!strcasecmp(name, "warning"))
		return LOG_WARNING;

	if (!strcasecmp(name, "error"))
		return LOG_ERR;

	return 0;
}

void
logging_init(void)
{
	openlog(PROGRAM_NAME, LOG_PID, LOG_DAEMON);
}

void
logging_close(void)
{
	closelog();
}

void
__attribute__ ((format (printf, 2, 3)))
message(int priority, const char *fmt, ...)
{
	va_list ap;

	if (priority > log_priority)
		return;

	va_start(ap, fmt);
	vsyslog(priority, fmt, ap);
	va_end(ap);
#if 1
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	printf("\n");
	fflush(stdout);
	va_end(ap);
#endif
}
