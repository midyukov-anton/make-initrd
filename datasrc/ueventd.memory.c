#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <error.h>
#include <limits.h>

#include "ueventd.h"

void   *
xmalloc(size_t size)
{
	void   *r = malloc(size);

	if (!r)
		fatal("malloc: allocating %lu bytes: %m",
		      (unsigned long) size);
	return r;
}

void   *
xcalloc(size_t nmemb, size_t size)
{
	void   *r = calloc(nmemb, size);

	if (!r)
		fatal("calloc: allocating %lu*%lu bytes: %m",
		      (unsigned long) nmemb, (unsigned long) size);
	return r;
}

void   *
xrealloc(void *ptr, size_t nmemb, size_t elem_size)
{
	if (nmemb && ULONG_MAX / nmemb < elem_size)
		fatal("realloc: nmemb*size > ULONG_MAX");

	size_t  size = nmemb * elem_size;
	void   *r = realloc(ptr, size);

	if (!r)
		fatal("realloc: allocating %lu*%lu bytes: %m",
		      (unsigned long) nmemb, (unsigned long) elem_size);
	return r;
}

char   *
xstrdup(const char *s)
{
	size_t  len = strlen(s);
	char   *r = xmalloc(len + 1);

	memcpy(r, s, len + 1);
	return r;
}

char   *
xasprintf(char **ptr, const char *fmt, ...)
{
	va_list arg;

	va_start(arg, fmt);
	if (vasprintf(ptr, fmt, arg) < 0)
		fatal("vasprintf: %m");
	va_end(arg);

	return *ptr;
}
