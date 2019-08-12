#define _GNU_SOURCE
#include <sys/prctl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <dirent.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>

#include "ueventd.h"

char g_buf[GBUFSZ];

extern int log_priority;
extern char *logfile;

static void __attribute__((noreturn))
print_help(void)
{
	printf("Usage: uevent [options]\n"
	       "Options:\n"
		       " -b, --basedir=DIR    directory which contains queues;\n"
		       " -r, --rulesdir=DIR   rules location;\n"
		       " -p, --pidfile=FILE   pid file location;\n"
		       " -l, --logfile=FILE   log file;\n"
		       " -L, --loglevel=LVL   logging level;\n"
		       " -f, --foreground     stay in the foreground;\n"
		       " -V, --version        print program version and exit;\n"
		       " -h, --help           show this text and exit.\n"
		       "\n");
	exit(EXIT_SUCCESS);
}

static void __attribute__ ((noreturn))
print_version(void)
{
	printf("uevent version "VERSION"\n"
	       "Written by Alexey Gladkov <gladkov.alexey@gmail.com>\n"
	       "\n"
	       "Copyright (C) 2019  Alexey Gladkov <gladkov.alexey@gmail.com>\n"
	       "This is free software; see the source for copying conditions.  There is NO\n"
	       "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
	       "\n");
	exit(EXIT_SUCCESS);
}

static int
process_signals(int fd, struct pool *pool, struct queues *queues)
{
	ssize_t size;
	struct signalfd_siginfo fdsi;

	size = TEMP_FAILURE_RETRY(read(fd, &fdsi, sizeof(struct signalfd_siginfo)));

	if (size != sizeof(struct signalfd_siginfo)) {
		err("unable to read signal info");
		return -1;
	}

	switch (fdsi.ssi_signo) {
		case SIGHUP:
			break;
		case SIGINT:
		case SIGTERM:
			close(pool->fd);
			pool->fd = -1;
			break;
		case SIGCHLD:
			while (1) {
				ssize_t i;
				pid_t pid;
				int status;

				errno = 0;
				if ((pid = waitpid(-1, &status, WNOHANG)) <= 0) {
					if (errno && errno != ECHILD)
						err("waitpid: %m");
					break;
				}

				for (i = 0; queues && i < queues->count; i++) {
					if (pid == queues->dirs[i].handler)
						queues->dirs[i].handler = 0;
				}
			}
			break;
	}

	return 0;
}

static int
drop_inotify_events(int fd)
{
	char *buf;
	size_t len = 0;

	if (ioctl(fd, FIONREAD, &len) < 0) {
		err("ioctl(FIONREAD): %m");
		return -1;
	}

	buf = xmalloc(len);
	TEMP_FAILURE_RETRY(read(fd, buf, len));
	free(buf);

	return 0;
}

static int
is_directory(char *path)
{
	struct stat st = { 0 };

	errno = 0;
	if (stat(path, &st) < 0) {
		if (errno != ENOENT && errno != ENOTDIR)
			err("stat: %s", path);
		return 0;
	}

	if ((st.st_mode & S_IFMT) != S_IFDIR)
		return 0;

	return 1;
}

static int
rules_filter(const struct dirent *ent)
{
	char *suffix;
	size_t len;

	if (ent->d_type != DT_REG)
		return 0;

	if ((len = strlen(ent->d_name)) < 1)
		return 0;

	if (ent->d_name[0] == '.')
		return 0;

	if (ent->d_name[len - 1] == '~')
		return 0;

	if ((suffix = strrchr(ent->d_name, '.')) != NULL) {
		if (!strcmp(suffix, ".#") ||
		    !strcmp(suffix, ".swp") ||
		    !strcmp(suffix, ".rpmnew") ||
		    !strcmp(suffix, ".rpmsave"))
			return 0;
	}

	return 1;
}

static int
rules_compar(const struct dirent **a, const struct dirent **b)
{
	int rc = alphasort(a, b);
	if (rc < 0)
		return 1;
	if (rc > 0)
		return -1;
	return rc;
}

static void
free_rules(struct rules *rules)
{
	struct rules *next;
	while (rules) {
		next = rules->next;
		switch (rules->type) {
			case T_HANDLER_SHELL:
				shell_free_rule(rules);
				break;
#ifdef WITH_LUA
			case T_HANDLER_LUA:
				lua_free_rule(rules);
				break;
#endif
			default:
				free(rules);
				break;
		}
		rules = next;
	}
}

static enum handler_type
get_handler_type(const char *path)
{
	int fp = -1;
	char buf[3];
	ssize_t n;
	enum handler_type tp = T_HANDLER_NONE;

	if ((fp = open(path, O_RDONLY|O_CLOEXEC)) < 0) {
		err("open: %s: %m", path);
		goto exit;
	}

	buf[0] = '\0';

	n = TEMP_FAILURE_RETRY(read(fp, &buf, sizeof(char) * 3));
	if (n < 0) {
		err("read: %s: %m", path);
		goto exit;
	}

	if (buf[0] == '#' && buf[1] == '!' && buf[2] == '/') {
		struct stat st;

		if (fstat(fp, &st) < 0) {
			err("fstat: %s: %m", path);
			goto exit;
		}

		if (!(st.st_mode & S_IXUSR))
			goto exit;

		tp = T_HANDLER_SHELL;
#ifdef WITH_LUA
	} else {
		tp = T_HANDLER_LUA;
#endif
	}
exit:
	if (fp >= 0)
		close(fp);

	return tp;
}

static int
read_rules(const char *rulesdir, struct rules **rules)
{
	int i;
	ssize_t n;
	struct dirent **namelist;

	dbg("load rules from %s", rulesdir);

	n = scandir(rulesdir, &namelist, rules_filter, rules_compar);
	if (n < 0) {
		err("scandir: %s: %m", rulesdir);
		return -1;
	}

	if (*rules) {
		free_rules(*rules);
		*rules = NULL;
	}

	for (i = 0; i < n; i++) {
		xconcat(g_buf, sizeof(g_buf), rulesdir, "/", namelist[i]->d_name, NULL);

		struct rules *r = xmalloc(sizeof(struct rules));
		r->type = get_handler_type(g_buf);

		switch (r->type) {
			case T_HANDLER_SHELL:
				if (!shell_make_rule(r, g_buf)) {
					free(r);
					goto next;
				}
				break;
#ifdef WITH_LUA
			case T_HANDLER_LUA:
				if (!lua_make_rule(r, g_buf)) {
					free(r);
					goto next;
				}
				break;
#endif
			default:
				free(r);
				goto next;
		}

		r->next = (*rules);
		(*rules) = r;
next:
		free(namelist[i]);
	}

	free(namelist);

	return 0;
}

int
main(int argc, char **argv)
{
	sigset_t mask, sigmask_orig;
	int c;
	int ep_timeout = 0;

	char basedir[PATH_MAX], rulesdir[PATH_MAX];
	char *pidfile = NULL;
	int daemonize = 1;
	int rc = EXIT_FAILURE;

	struct pool *pool = NULL;
	struct queues *queues = NULL;
	struct rules *rules = NULL;

	struct option long_options[] = {
		{ "help", no_argument, 0, 'h' },
		{ "version", no_argument, 0, 'V' },
		{ "foreground", no_argument, 0, 'f' },
		{ "loglevel", required_argument, 0, 'L' },
		{ "logfile", required_argument, 0, 'l' },
		{ "pidfile", required_argument, 0, 'p' },
		{ "basedir", required_argument, 0, 'b' },
		{ "rulesdir", required_argument, 0, 'r' },
		{ 0, 0, 0, 0 }
	};

	logging_level("error");

	snprintf(basedir,  PATH_MAX - 1, "/.initrd/uevent/queues");
	snprintf(rulesdir, PATH_MAX - 1, "/lib/uevent/handlers");

	while ((c = getopt_long(argc, argv, "hVfb:L:l:p:r:", long_options, NULL)) != -1) {
		switch (c) {
			case 'b':
				if (!realpath(optarg, basedir))
					error(EXIT_FAILURE, errno, "bad path: %s", optarg);
				break;
			case 'r':
				if (!realpath(optarg, rulesdir))
					error(EXIT_FAILURE, errno, "bad path: %s", optarg);
				break;
			case 'p':
				pidfile = optarg;
				break;
			case 'l':
				logfile = optarg;
				break;
			case 'L':
				logging_level(optarg);
				break;
			case 'f':
				daemonize = 0;
				break;
			case 'V':
				print_version();
			default:
			case 'h':
				print_help();
		}
	}

	if (!is_directory(basedir))
		error(EXIT_FAILURE, 0, "%s: queues directory does not exist", basedir);

	if (!is_directory(rulesdir))
		error(EXIT_FAILURE, 0, "%s: rules directory does not exist", rulesdir);

	if (daemonize && daemon(0, 0) < 0) {
		error(0, errno, "daemon");
		goto exit_failure;
	}

	if (!log_priority)
		logging_level("info");

	logging_init();

	info("starting version %s", VERSION);

	if (pidfile && !write_pid(pidfile))
		goto exit_failure;

	if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0) {
		err("prctl(PR_SET_CHILD_SUBREAPER): %m");
		goto exit_failure;
	}

	if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0) {
		err("prctl(PR_SET_PDEATHSIG): %m");
		goto exit_failure;
	}

	setenv("BASEDIR", basedir, 1);
	setenv("RULESDIR", rulesdir, 1);

	sigfillset(&mask);
	sigprocmask(SIG_SETMASK, &mask, &sigmask_orig);

	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigdelset(&mask, SIGABRT);
	sigdelset(&mask, SIGSEGV);

	pool = create_pool();
	if (!pool)
		goto exit_failure;

	int fd_signal, fd_basedir, fd_rulesdir;

	if ((fd_signal   = add_watch_signals(pool, &mask, SFD_NONBLOCK | SFD_CLOEXEC)) < 0 ||
	    (fd_basedir  = add_watch_directory(pool, basedir, IN_DONT_FOLLOW | IN_CREATE | IN_DELETE)) < 0 ||
	    (fd_rulesdir = add_watch_directory(pool, rulesdir, IN_DONT_FOLLOW | IN_ATTRIB | IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_CLOSE_WRITE)) < 0)
		goto exit_failure;

	if (add_queues(pool, basedir, &queues) < 0)
		goto exit_failure;

	if (read_rules(rulesdir, &rules) < 0)
		goto exit_failure;

	ep_timeout = 500;

	while (!is_closed_pool(pool)) {
		struct epoll_event ev[4096];

		errno = 0;
		ssize_t fdcount = epoll_wait(pool->fd, ev, 4096, ep_timeout);

		if (fdcount < 0) {
			if (errno == EINTR)
				continue;
			err("epoll_wait: %m");
			goto exit_failure;
		}

		for (ssize_t i = 0; i < fdcount; i++) {
			if (!(ev[i].events & EPOLLIN))
				continue;

			if (ev[i].data.fd == fd_signal) {
				if (process_signals(fd_signal, pool, queues) < 0)
					goto exit_failure;
				continue;
			}

			if (drop_inotify_events(ev[i].data.fd) < 0)
				goto exit_failure;

			if (ev[i].data.fd == fd_rulesdir) {
				if (read_rules(rulesdir, &rules) < 0)
					goto exit_failure;
				continue;
			}

			if (ev[i].data.fd == fd_basedir) {
				if (add_queues(pool, basedir, &queues) < 0)
					goto exit_failure;
				continue;
			}

			for (ssize_t j = 0; queues && j < queues->count; j++) {
				if (ev[i].data.fd != queues->dirs[j].wd)
					continue;
				queues->dirs[j].dirty = 1;
			}
		}

		for (ssize_t i = 0; queues && i < queues->count; i++) {
			if (queues->dirs[i].dirty == 0 || queues->dirs[i].handler != 0)
				continue;

			if (process_events(basedir, &(queues->dirs[i]), rules) < 0)
				goto exit_failure;

			xconcat(g_buf, sizeof(g_buf), basedir, "/", queues->dirs[i].name, "/.dirty", NULL);

			if (!access(g_buf, F_OK))
				queues->dirs[i].dirty = 1;
		}
	}

	rc = EXIT_SUCCESS;

exit_failure:
	close_queues(pool, queues);
	close_pool(pool);
	free_rules(rules);

	if (pidfile)
		remove_pid(pidfile);

	logging_close();

	return rc;
}
