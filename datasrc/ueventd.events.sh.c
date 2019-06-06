#include <sys/types.h>
#include <sys/wait.h>

#include <unistd.h>
#include <string.h>

#include "ueventd.h"

int
shell_make_rule(struct rules *r, const char *path)
{
	dbg("loading (shell): %s", path);
	r->handler.shell_script = xstrdup(path);
	return 1;
}

void
shell_free_rule(struct rules *r)
{
	free(r->handler.shell_script);
	free(r);
}

static int
run_script(const char *script)
{
	const char *basename = NULL;
	pid_t pid;

	if ((pid = fork()) < 0) {
		err("fork: %m");
		return -1;
	}

	if (pid > 0) {
		int status;

		if (waitpid(pid, &status, 0) < 0) {
			err("waitpid: %m");
			return -1;
		}

		return 0;
	}

	if ((basename = strrchr(script, '/')) == NULL)
		basename = script;

	execl(script, basename, (char *) NULL);
	err("execl: %m");
	return -1;
}

void
shell_process_rule(const struct rules *r)
{
	run_script(r->handler.shell_script);
}
