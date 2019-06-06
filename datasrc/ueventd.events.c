#include <linux/limits.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <dirent.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#include "ueventd.h"

#ifndef _GNU_SOURCE
extern char **environ;
#endif

extern char g_buf[GBUFSZ];

static char **base_environ = NULL;
static char ***cache_environ = NULL;

static int
event_filter(const struct dirent *ent)
{
	if (ent->d_type != DT_REG)
		return 0;
	return 1;
}

static void
sanitize_fds(void)
{
	struct stat st;
	long int max_fd;
	int fd;

	umask(0);

	for (fd = STDIN_FILENO; fd <= STDERR_FILENO; ++fd) {
		if (fstat(fd, &st) < 0)
			fatal("fstat: %m");
	}

	max_fd = sysconf(_SC_OPEN_MAX);

	if (max_fd < NR_OPEN)
		max_fd = NR_OPEN;

	if (max_fd > INT_MAX)
		max_fd = INT_MAX;

	for (; fd < (int) max_fd; ++fd)
		(void) close(fd);

	errno = 0;
}

static int
set_environ(const char *fname)
{
	char *a, *map = NULL;
	int fd = -1;
	struct stat sb;

	errno = 0;
	if ((fd = open(fname, O_RDONLY | O_CLOEXEC)) < 0) {
		if (errno != ENOENT)
			err("open: %s: %m", fname);
		return -1;
	}

	if (fstat(fd, &sb) < 0) {
		err("fstat: %s: %m", fname);
		return -1;
	}

	if (!sb.st_size) {
		close(fd);
		return 0;
	}

	if ((a = map = mmap(NULL, (size_t) sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
		err("mmap: %s: %m", fname);
		return -1;
	}

	while (a && *a) {
		char *name, *value;
		size_t namesz, valuesz;
		FILE *valfd;
		int is_quote = 0;

		while (isspace(*a)) a++;

		if (*a == '\0')
			break;

		namesz = valuesz = 0;

		while (a[namesz] != '=') {
			if (a[namesz] == '\0') {
				err("open: %s: expect '=' character for '%s', but found EOF", fname, a);
				return -1;
			}
			namesz++;
		}

		name = strndup(a, namesz);
		a += namesz + 1;

		if (*a != '"') {
			free(name);
			err("open: %s: expect opening quote not found for '%s', but '%c' found", fname, name, *a);
			return -1;
		}

		a++;

		if ((valfd = open_memstream(&value, &valuesz)) == NULL) {
			free(name);
			err("open_memstream: %m");
			return -1;
		}

		while (*a != '\0') {
			if (!is_quote) {
				if (*a == '"')
					break;

				if (*a == '\\') {
					is_quote = 1;
					a++;
					continue;
				}
			} else {
				is_quote = 0;
			}

			fprintf(valfd, "%c", *a);
			a++;
		}

		fclose(valfd);

		if (*a != '"') {
			err("open: %s: expect closing quote not found for '%s', but '%c' found", fname, name, *a);
			return -1;
		}

		a++;

		setenv(name, value, 1);

		free(name);
		free(value);
	}

	munmap(map, (size_t) sb.st_size);
	close(fd);

	return 0;
}

static void
save_base_environ(void)
{
	char **e = environ;

	while(e && *e)
		e++;

	base_environ = xcalloc((size_t)(e - environ + 1), sizeof(char *));

	e = environ;
	while (e && *e) {
		base_environ[e - environ] = *e;
		e++;
	}
}

static char **
get_environ(ssize_t i)
{
	char **e = base_environ;

	if (cache_environ[i])
		return cache_environ[i];

	while(e && *e)
		e++;

	cache_environ[i] = xcalloc((size_t)(e - base_environ + 1), sizeof(char *));

	e = base_environ;
	while (e && *e) {
		cache_environ[i][e - base_environ] = *e;
		e++;
	}

	return cache_environ[i];
}

int
process_events(const char *basedir, struct queue *queue, struct rules *rules)
{
	pid_t pid;

	if((pid = fork()) < 0) {
		err("fork: %m");
		return -1;
	}

	if (pid > 0) {
		queue->handler = pid;
		queue->dirty = 0;
		return 0;
	}

	struct dirent **namelist = NULL;
	ssize_t i, n;
	struct rules *r, *nr;

	snprintf(g_buf, sizeof(g_buf) - 1, "%s/%s", basedir, queue->name);

	errno = 0;
	if (chdir(g_buf) < 0) {
		if (errno != ENOENT && errno != ENOTDIR)
			fatal("chdir: %s: %m", g_buf);
		exit(0);
	}

	n = scandir(".", &namelist, event_filter, alphasort);
	if (n < 0)
		fatal("scandir: %s: %m", g_buf);

	if (n == 0)
		exit(0);

	sanitize_fds();

	setenv("PROCESS", "EVENT", 1);
	setenv("QUEUE", queue->name, 1);

	save_base_environ();
	cache_environ = xcalloc((size_t) n, sizeof(char *));

	r = rules;
	while (r) {
		for (i = 0; i < n; i++) {
			if (namelist[i]->d_name[0] == '\0')
				continue;

			environ = get_environ(i);
			if (!environ) {
				err("unable to setup environ");
				continue;
			}

			if (set_environ(namelist[i]->d_name) < 0) {
				namelist[i]->d_name[0] = '\0';
				continue;
			}

			setenv("EVENTNAME", namelist[i]->d_name, 1);

			switch (r->type) {
				case T_HANDLER_SHELL:
					shell_process_rule(r);
					break;
#ifdef WITH_LUA
				case T_HANDLER_LUA:
					lua_process_rule(r);
					break;
#endif
				default:
					break;
			}
		}

		r = r->next;
	}

	unsetenv("EVENTNAME");

	environ = base_environ;

	setenv("PROCESS", "POST", 1);

	r = rules;
	while (r) {
		nr = r->next;

		switch (r->type) {
			case T_HANDLER_SHELL:
				shell_process_rule(r);
				shell_free_rule(r);
				break;
#ifdef WITH_LUA
			case T_HANDLER_LUA:
				lua_process_rule(r);
				lua_free_rule(r);
				break;
#endif
			default:
				free(r);
				break;
		}

		r = nr;
	}

	for (i = 0; i < n; i++) {
		free(cache_environ[i]);
		free(namelist[i]);
	}

	free(base_environ);
	free(namelist);

	exit(0);
}
