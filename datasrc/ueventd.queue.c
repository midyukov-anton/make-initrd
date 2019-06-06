#include <sys/param.h>

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>

#include "ueventd.h"

static int
is_dir_not_empty(const char *path)
{
	struct dirent *ent;
	DIR *dir = NULL;
	int ret = 0;

	if (!(dir = opendir(path))) {
		err("opendir: %s: %m", path);
		return -1;
	}

	errno = 0;
	while ((ent = readdir(dir))) {
		if (ent->d_type == DT_REG && ent->d_name[0] != '.') {
			ret = 1;
			break;
		}
	}

	if (errno != 0) {
		err("readdir: %s: %m", path);
		ret = -1;
	}

	closedir(dir);

	return ret;
}

static int
queue_filter(const struct dirent *ent)
{
	if (ent->d_type != DT_DIR)
		return 0;
	if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
		return 0;
	return 1;
}

void
close_queues(struct pool *pool, struct queues *p)
{
	ssize_t i;
	if (!p)
		return;
	for (i = 0; i < p->count; i++) {
		if (pool)
			remove_pool(pool, p->dirs[i].wd);
		free(p->dirs[i].name);
	}
	free(p->dirs);
	free(p);
}

int
add_queues(struct pool *pool, char *basepath, struct queues **p)
{
	struct queues *ret = NULL;
	struct dirent **namelist = NULL;
	ssize_t i, n;

	n = scandir(basepath, &namelist, queue_filter, alphasort);
	if (n < 0) {
		err("scandir: %s: %m", basepath);
		return -1;
	}

	if (n == 0)
		goto cleanup;

	if (*p && (*p)->count == n) {
		int changed = 0;

		i = n;
		while (i-- && !changed)
			changed = strcmp(namelist[i]->d_name, (*p)->dirs[i].name);

		if (!changed) {
			dbg("queues have not changed");
			return 0;
		}
	}

	ret = xcalloc(1, sizeof(struct queues));

	ret->count = n;
	ret->dirs = xcalloc((size_t) n, sizeof(struct queue));

	while (n--) {
		int found = 0;

		if (*p) {
			for (i = 0; i < (*p)->count; i++) {
				if (!strcmp(namelist[n]->d_name, (*p)->dirs[i].name)) {
					found = 1;
					break;
				}
			}
		}

		if (!found) {
			char path[PATH_MAX];

			dbg("Add queue %s", namelist[n]->d_name);
			sprintf(path, "%s/%s", basepath, namelist[n]->d_name);

			ret->dirs[n].name = strdup(namelist[n]->d_name);
			ret->dirs[n].wd   = add_watch_directory(pool, path, IN_DONT_FOLLOW | IN_MOVED_TO | IN_CLOSE_WRITE);

			if (is_dir_not_empty(path))
				ret->dirs[n].dirty = 1;
		} else {
			dbg("Preserve queue %s", namelist[n]->d_name);
			ret->dirs[n].name    = (*p)->dirs[i].name;
			ret->dirs[n].wd      = (*p)->dirs[i].wd;
			ret->dirs[n].handler = (*p)->dirs[i].handler;
			ret->dirs[n].dirty   = (*p)->dirs[i].dirty;
		}

		free(namelist[n]);
	}

cleanup:
	free(namelist);

	if (*p) {
		for (i = 0; i < (*p)->count;) {
			for (n = 0; ret && n < ret->count; n++) {
				if (!strcmp(ret->dirs[n].name, (*p)->dirs[i].name))
					goto found;
			}
			dbg("Remove queue %s", (*p)->dirs[i].name);
			free((*p)->dirs[i].name);
			remove_pool(pool, (*p)->dirs[i].wd);
found:
			i++;
		}

		free((*p)->dirs);
		free(*p);
	}

	*p = ret;
	return 0;
}
