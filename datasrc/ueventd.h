#ifndef _UEVENT_H_
#define _UEVENT_H_

#define GBUFSZ PATH_MAX

/* ueventd.memory.c */

#include <stddef.h>

void *xmalloc(size_t size);
void *xcalloc(size_t nmemb, size_t size);
void *xrealloc(void *ptr, size_t nmemb, size_t size);
char *xstrdup(const char *s);
char *xasprintf(char **ptr, const char *fmt, ...)
	__attribute__ ((__format__(__printf__, 2, 3)))
	__attribute__ ((__nonnull__(2)));

/* ueventd.pool.c */

#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/inotify.h>

struct pool {
	int fd;
	int *fds;
	size_t n_fds;
};

struct pool *create_pool(void);
void *close_pool(struct pool *pool);
void remove_pool(struct pool *pool, int fd);
int add_pool(struct pool *pool, const int fd, const uint32_t events);
int is_closed_pool(const struct pool *pool);

int add_watch_directory(struct pool *pool, char *path, uint32_t mask);
int add_watch_signals(struct pool *pool, const sigset_t *mask, int flags);

/* ueventd.queue.c */

struct queue {
	pid_t handler;
	int dirty;
	char *name;    // directory name
	int wd;        // nonnegative watch descriptor
};

struct queues {
	struct queue *dirs;
	ssize_t count;
};

void close_queues(struct pool *pool, struct queues *p);
int add_queues(struct pool *pool, char *dir, struct queues **p);

/* ueventd.events.c */

#ifdef WITH_LUA
    #include <lauxlib.h>
    #include <lualib.h>
#endif

enum handler_type {
	T_HANDLER_NONE,
	T_HANDLER_SHELL,
#ifdef WITH_LUA
	T_HANDLER_LUA,
#endif
};

union handler {
	char *shell_script;
#ifdef WITH_LUA
	lua_State *lua_state;
#endif
};

struct rules {
	enum handler_type type;
	union handler handler;
	struct rules *next;
};

int process_events(const char *basedir, struct queue *queue, struct rules *rules);

/* ueventd.events.sh.c */

int shell_make_rule(struct rules *r, const char *path);
void shell_free_rule(struct rules *r);
void shell_process_rule(const struct rules *r);

#ifdef WITH_LUA
/* ueventd.events.lua.c */

int lua_make_rule(struct rules *r, const char *path);
void lua_free_rule(struct rules *r);
void lua_process_rule(const struct rules *r);
#endif

/* ueventd.pidfile.c */

int read_pid(const char *pidfile);
int check_pid(const char *pidfile);
int write_pid(const char *pidfile);
int remove_pid(const char *pidfile);

/* ueventd.logging.c */

#include <syslog.h>
#include <stdlib.h>

void logging_init(void);
void logging_close(void);
void logging_level(const char *lvl);

void message(const int priority, const char *fmt, ...);

#define fatal(format, arg...)               \
	do {                                \
		message(LOG_CRIT,           \
			"%s(%d): " format , \
			__FILE__, __LINE__, \
			## arg);            \
		exit(EXIT_FAILURE);         \
	} while (0)

#define err(format, arg...)                 \
	do {                                \
		message(LOG_ERR,            \
			"%s(%d): " format , \
			__FILE__, __LINE__, \
			## arg);            \
	} while (0)

#define info(format, arg...)                \
	do {                                \
		message(LOG_INFO,           \
			"%s(%d): " format , \
			__FILE__, __LINE__, \
			## arg);            \
	} while (0)

#define dbg(format, arg...)                 \
	do {                                \
		message(LOG_DEBUG,          \
			"%s(%d): " format , \
			__FILE__, __LINE__, \
			## arg);            \
	} while (0)

#endif /* _UEVENT_H_ */
