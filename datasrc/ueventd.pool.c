#include <sys/epoll.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "ueventd.h"

struct pool *
create_pool(void)
{
	struct pool *pool = xmalloc(sizeof(struct pool));

	if ((pool->fd = epoll_create1(EPOLL_CLOEXEC)) < 0) {
		err("epoll_create1: %m");
		return NULL;
	}

	pool->fds = NULL;
	pool->n_fds = 0;

	return pool;
}

int
is_closed_pool(const struct pool *pool)
{
	return (!pool || pool->fd < 0);
}

void *
close_pool(struct pool *pool)
{
	size_t i;
	if (!pool)
		return NULL;

	for (i = 0; i < pool->n_fds; i++)
		remove_pool(pool, pool->fds[i]);

	if (pool->fd >= 0)
		close(pool->fd);

	free(pool->fds);
	free(pool);

	return NULL;
}

int
add_pool(struct pool *pool, const int fd, const uint32_t events)
{
	struct epoll_event ev = { 0 };

	if (is_closed_pool(pool) || fd < 0) {
		err("add_pool: invalid call");
		return -1;
	}

	ev.events  = events;
	ev.data.fd = fd;

	if (epoll_ctl(pool->fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
		err("epoll_ctl: %m");
		return -1;
	}

	pool->fds = xrealloc(pool->fds, pool->n_fds + 1, sizeof(int));
	pool->fds[pool->n_fds++] = fd;

	return 0;
}

void
remove_pool(struct pool *pool, int fd)
{
	size_t i;
	if (is_closed_pool(pool) || fd < 0)
		return;

	for (i = 0; i < pool->n_fds; i++) {
		if (pool->fds[i] == fd) {
			epoll_ctl(pool->fd, EPOLL_CTL_DEL, fd, NULL);
			close(fd);
			pool->fds[i] = -1;
		}
	}
}

#include <sys/inotify.h>

int
add_watch_directory(struct pool *pool, char *path, uint32_t mask)
{
	int fd;

	if ((fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) < 0) {
		err("inotify_init1: %m");
		return -1;
	}

	errno = 0;
	if (inotify_add_watch(fd, path, IN_ONLYDIR | mask) < 0) {
		if (errno == ENOSPC) {
			err("unable to add watcher for %s because the user limit on the total number of inotify watches was reached", path);
			return -1;
		}

		err("inotify_add_watch: %s: %m", path);
		return -1;
	}

	if (add_pool(pool, fd, EPOLLIN) < 0)
		return -1;

	return fd;
}

#include <sys/signalfd.h>

int
add_watch_signals(struct pool *pool, const sigset_t *mask, int flags)
{
	int fd;

	if ((fd = signalfd(-1, mask, flags)) < 0) {
		err("signalfd: %m");
		return -1;
	}

	if (add_pool(pool, fd, EPOLLIN) < 0)
		return -1;

	return fd;
}
