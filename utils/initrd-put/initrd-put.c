#define _GNU_SOURCE

#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <err.h>
#include <sysexits.h>
#include <fts.h>
#include <fcntl.h>
#include <ctype.h>

#include <gelf.h>

struct file {
	unsigned processed:1;
	mode_t mode;
	size_t size;
	dev_t  dev;
	uid_t  uid;
	gid_t  gid;
	char   *source;
	size_t source_len;
	char   *symlink;
};

static const char *progname = NULL;

static char *destdir = NULL;
static char *prefix = NULL;
static size_t prefix_len = 0;
static char *logfile = NULL;
static int verbose = 0;
static int dry_run = 0;
static int force = 0;

static size_t n_files = 0;
static struct file *files = NULL;

static int dsort(const void *a, const void *b)
{
	struct file const *aa = a;
	struct file const *bb = b;

	if (S_ISDIR(aa->mode)) {
		if (!S_ISDIR(bb->mode))
			return -1;
	} else if (S_ISDIR(bb->mode))
		return 1;

	return strcmp(aa->source, bb->source);
}

static char *canonicalize_symlink(char *file, const char *target)
{
	static char pathbuf[PATH_MAX];
	char *slash;

	strcpy(pathbuf, file);

	if (target[0] == '/') {
		// FIXME
		strcpy(pathbuf, target);
		return pathbuf;
	}

	slash = strrchr(pathbuf, '/');
	if (slash)
		*slash = 0;

	while (1) {
		if (!strncmp(target, "../", 3)) {
			target += 3;
			slash = strrchr(pathbuf, '/');
			if (slash)
				*slash = 0;
			continue;
		}
		if (!strncmp(target, "./", 2)) {
			target += 2;
			continue;
		}
		strcat(pathbuf, "/");

		slash = strchr(target, '/');
		if (!slash) {
			strcat(pathbuf, target);
			break;
		}

		strncat(pathbuf, target, (size_t) (slash - target));
		target = slash + 1;
	}

	return pathbuf;
}

static struct file *append_path(char *path)
{
	for (int i = 0; i < (int)n_files; i++) {
		if (!strcmp(files[i].source, path))
			return files + i;
	}

	if (verbose)
		warnx("append: %s", path);

	files = realloc(files, (n_files + 1) * sizeof(struct file));
	if (!files)
		err(EXIT_FAILURE, "reallocarray");

	struct file *f = files + n_files;

	memset(f, 0, sizeof(struct file));

	f->source = strdup(path);
	if (!f->source)
		err(EXIT_FAILURE, "strdup");

	f->source_len = strlen(path);

	n_files++;

	return f;
}

static struct file *append_fullpath(char *path)
{
	char filebuf[PATH_MAX];

	strncpy(filebuf, path, sizeof(filebuf) - 1);

	while (1) {
		if (prefix && !strcmp(filebuf, prefix))
			break;

		char *slash = strrchr(filebuf, '/');

		if (!slash || filebuf == slash)
			break;

		*slash = 0;

		struct file *p = append_path(filebuf);

		if (p->processed)
			continue;

		struct stat sb;

		if (lstat(filebuf, &sb) < 0)
			err(EXIT_FAILURE, "lstat: %s", filebuf);

		p->mode = sb.st_mode;

		if (S_ISDIR(sb.st_mode)) {
			p->processed = 1;
		}
	}

	return append_path(path);
}

enum ftype {
	FTYPE_ERROR = 0,
	FTYPE_IGNORE,
	FTYPE_DATA,
	FTYPE_TEXT_SCRIPT,
	FTYPE_ELF_STATIC,
	FTYPE_ELF_DYNAMIC,
};

static enum ftype elf_file(int fd)
{
	int is_dynamic;
	Elf *e;
	Elf_Scn *scn;
	size_t shstrndx;
	int rc = FTYPE_ERROR;

	if ((e = elf_begin(fd, ELF_C_READ, NULL)) == NULL) {
		warnx("elf_begin: %s", elf_errmsg(-1));
		goto err;
	}

	switch (elf_kind(e)) {
		case  ELF_K_NONE:
		case  ELF_K_AR:
		case ELF_K_COFF:
		case  ELF_K_ELF:
			break;
		default:
			goto end;
	}

	if (elf_getshdrstrndx(e, &shstrndx) != 0) {
		warnx("elf_getshdrstrndx: %s", elf_errmsg(-1));
		goto end;
	}

	is_dynamic = 0;

	for (scn = NULL; (scn = elf_nextscn(e, scn)) != NULL;) {
		GElf_Shdr  shdr;

		if (gelf_getshdr(scn , &shdr) != &shdr) {
			warnx("gelf_getshdr: %s", elf_errmsg(-1));
			goto end;
		}

		if (shdr.sh_type == SHT_DYNAMIC) {
			is_dynamic = 1;
			break;
		}
	}

	rc = (is_dynamic ? FTYPE_ELF_DYNAMIC : FTYPE_ELF_STATIC);
end:
	elf_end(e);
err:
	return rc;
}

static int shared_object_dependencies(const char *filename)
{
	FILE *pfd;
	char *line = NULL;
	size_t len = 0;
	ssize_t n;
	char command[PATH_MAX + 10];

	snprintf(command, sizeof(command), "ldd %s 2>&1", filename);

	pfd = popen(command, "r");

	while ((n = getline(&line, &len, pfd)) != -1) {
		char *p;

		if (line[n - 1] == '\n')
			line[n - 1] = '\0';

		p = strstr(line, "(0x");
		if (!p)
			continue;
		*p-- = '\0';

		while (line != p && isspace(*p))
			*p-- = '\0';

		p = strstr(line, " => ");
		if (p)
			p += 4;
		else
			p = line;

		while (*p != '\0' && isspace(*p))
			*p++ = '\0';

		if (*p != '/')
			continue;

		append_fullpath(p);
	}

	free(line);
	pclose(pfd);

	return 0;
}

static int process_regular_file(const char *filename)
{
	static char buf[LINE_MAX];
	int fd, ret = -1;

	errno = 0;
	fd = open(filename, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		if (errno == EACCES || errno == EPERM) {
			ret = 0;
			goto end;
		}
		warn("open: %s", filename);
		return -1;
	}

	if (pread(fd, buf, sizeof(buf), 0) < 0) {
		warn("read: %s", filename);
		goto end;
	}

	buf[LINE_MAX - 1] = 0;

	if (buf[0] == '#' &&
	    buf[1] == '!') {
		char *p, *q;

		for (p = &buf[2]; *p && isspace(*p); p++);
		for (q = p; *q && (!isspace(*q)); q++);
		*q = '\0';

		append_fullpath(p);

		ret = 0;
		goto end;
	}

	if (buf[0] == ELFMAG[0] &&
	    buf[1] == ELFMAG[1] &&
	    buf[2] == ELFMAG[2] &&
	    buf[3] == ELFMAG[3] &&
	    elf_file(fd) == FTYPE_ELF_DYNAMIC) {
		ret = shared_object_dependencies(filename);
		goto end;
	}

	ret = 0;
end:
	close(fd);
	return ret;
}

static int read_files(void)
{
	static char symlink[PATH_MAX];
	char *argv[2] = { NULL, NULL };
	FTS *t = NULL;
	int rc = -1;

	while (1) {
		FTSENT *p;

		argv[0] = NULL;

		for (size_t n = 0; n < n_files; n++) {
			if (!files[n].processed) {
				argv[0] = files[n].source;
				if (verbose)
					warnx("processing: %s", argv[0]);
				break;
			}
		}

		if (argv[0] == NULL)
			break;

		if ((t = fts_open(argv, FTS_PHYSICAL, NULL)) == NULL) {
			warn("fts_open");
			return -1;
		}

		while ((p = fts_read(t))) {
			switch (p->fts_info) {
				case FTS_D:
				case FTS_DC:
				case FTS_F:
				case FTS_SL:
				case FTS_SLNONE:
				case FTS_DEFAULT:
					break;
				case FTS_DNR:
				case FTS_ERR:
				case FTS_NS:
					errno = p->fts_errno;
					warn("fts_read: %s", p->fts_path);
					goto end;
			}

			struct file *f = append_fullpath(p->fts_path);

			if (f->processed)
				continue;

			f->processed = 1;
			f->mode = p->fts_statp->st_mode;
			f->size = (size_t) p->fts_statp->st_size;
			f->dev  = p->fts_statp->st_dev;
			f->uid  = p->fts_statp->st_uid;
			f->gid  = p->fts_statp->st_gid;

			if (FTS_F == p->fts_info) {
				if (process_regular_file(p->fts_path) < 0) {
					warnx("failed to read regular file: %s", p->fts_path);
					goto end;
				}
				continue;
			}

			if (FTS_SL     == p->fts_info ||
			    FTS_SLNONE == p->fts_info) {
				ssize_t linklen = readlink(p->fts_path, symlink, sizeof(symlink));
				if (linklen >= 0) {
					symlink[linklen] = 0;
					f->symlink = strdup(symlink);
					append_fullpath(canonicalize_symlink(p->fts_path, f->symlink));
				} else {
					warn("readlink: %s", p->fts_path);
				}
			}
		}

		if (t) {
			fts_close(t);
			t = NULL;
		}
	}
	rc = 0;
end:
	if (t)
		fts_close(t);
	return rc;
}

static void print_files(FILE *log)
{
	for (int i = 0; i < (int)n_files; i++) {
		char type;
		struct file *p = files + i;
		char *dest = p->source;

		if (prefix) {
			if (p->source_len != prefix_len && dest[prefix_len] == '/' && !strncmp(dest, prefix, prefix_len - 1))
				dest += prefix_len;
			else if (!strcmp(dest, prefix))
				continue;
		}

		switch (p->mode & S_IFMT) {
			case S_IFBLK:  type = 'b'; break;
			case S_IFCHR:  type = 'c'; break;
			case S_IFDIR:  type = 'd'; break;
			case S_IFIFO:  type = 'p'; break;
			case S_IFLNK:  type = 'l'; break;
			case S_IFREG:  type = 'f'; break;
			case S_IFSOCK: type = 's'; break;
			default:       type = '?'; break;
		}

		fprintf(log, "%c\t%s\t%s%s%s\t%s\n",
				type,
				p->source,
				destdir, (dest[0] == '/' ? "" : "/"), dest,
				(p->symlink ? p->symlink : ""));
	}
}

static int mksock(const char *filename, mode_t mode)
{
	struct sockaddr_un sun;

	if (strlen(filename) >= sizeof(sun)) {
		errno = EINVAL;
		warn("cannot bind socket: %s", filename);
		return -1;
	}

	memset(&sun, 0, sizeof (sun));
	sun.sun_family = AF_UNIX;
	strcpy(sun.sun_path, filename);

	int fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		warn("cannot create socket: %s", filename);
		return -1;
	}

	if (fchmod(fd, mode)) {
		warn("cannot set permissions of socket: %s", filename);
		close (fd);
		return -1;
	}

	if (bind(fd, (struct sockaddr *) &sun, sizeof(sun))) {
		warn("cannot bind socket: %s", filename);
		close (fd);
		return -1;
	}

	close (fd);
	return 0;
}

static int install_files(void)
{
	static char path[PATH_MAX];
	int sfd, dfd;
	size_t destdir_len = 0;

	destdir_len = strlen(destdir);

	strncpy(path, destdir, sizeof(path) - 1);
	path[destdir_len] = 0;

	for (int i = 0; i < (int)n_files; i++) {
		struct file *p = files + i;
		char *dest = p->source;

		if (prefix) {
			if (p->source_len != prefix_len && dest[prefix_len] == '/' && !strncmp(dest, prefix, prefix_len - 1))
				dest += prefix_len;
			else if (!strcmp(dest, prefix))
				continue;
		}

		strncpy(path + destdir_len, dest, sizeof(path) - destdir_len - 1);

		if (S_IFDIR == (p->mode & S_IFMT)) {
			errno = 0;
			if (mkdir(path, p->mode) < 0) {
				if (errno != EEXIST) {
					warn("mkdir: %s", path);
					return -1;
				} else if (verbose) {
					warnx("skip (directory): %s", dest);
				}
			} else if (verbose) {
				warnx("install (directory): %s", dest);
			}
			goto chown;
		}

		errno = 0;
		if (force && remove(path) < 0 && errno != ENOENT) {
			warn("remove: %s", path);
			return -1;
		}

		if (S_IFBLK == (p->mode & S_IFMT) ||
		    S_IFCHR == (p->mode & S_IFMT)) {
			errno = 0;
			if (mknod(path, p->mode, p->dev)) {
				if (errno != EEXIST) {
					warn("mknod: %s", path);
					return -1;
				} else if (verbose) {
					warnx("skip (divice file): %s", dest);
				}
			} else if (verbose) {
				warnx("install (divice file): %s", dest);
			}
			goto chown;
		}

		if (S_IFLNK == (p->mode & S_IFMT)) {
			errno = 0;
			if (symlink(p->symlink, path) < 0) {
				if (errno != EEXIST) {
					warn("symlink: %s", path);
					return -1;
				} else if (verbose) {
					warnx("skip (symlink): %s", dest);
				}
			} else if (verbose) {
				warnx("install (symlink): %s", dest);
			}
			goto chown;
		}

		if (S_IFIFO == (p->mode & S_IFMT)) {
			errno = 0;
			if (mkfifo(path, p->mode) < 0) {
				if (errno != EEXIST) {
					warn("mkfifo: %s", path);
					return -1;
				} else if (verbose) {
					warnx("skip (fifo): %s", dest);
				}
			} else if (verbose) {
				warnx("install (fifo): %s", dest);
			}
			goto chown;
		}

		if (S_IFSOCK == (p->mode & S_IFMT)) {
			errno = 0;
			if (mksock(path, p->mode) < 0)
				return -1;
			if (verbose)
				warnx("install (socket): %s", dest);
			goto chown;
		}

		if (S_IFREG != (p->mode & S_IFMT)) {
			warnx("not implemented: %s", path);
			return -1;
		}

		if (!access(path, X_OK)) {
			if (verbose)
				warnx("skip (file): %s", path);
			goto chown;
		}

		errno = 0;
		if ((dfd = creat(path, p->mode)) < 0) {
			warn("creat: %s", path);
			return -1;
		} else if (verbose) {
			warnx("install (file): %s", path);
		}

		errno = 0;
		if ((sfd = open(p->source, O_RDONLY)) < 0) {
			warn("open: %s", p->source);
			close(dfd);
			return -1;
		}

		ssize_t ret;
		size_t len = p->size;

		do {
			ret = copy_file_range(sfd, NULL, dfd, NULL, len, 0);
			if (ret < 0) {
				warn("copy_file_range: %s -> %s", p->source, path);
				close(sfd);
				close(dfd);
				return -1;
			}
			len -= (size_t) ret;
		} while (len > 0 && ret > 0);

		close(sfd);
		close(dfd);
chown:
		errno = 0;
		if (lchown(path, p->uid, p->gid) < 0 && errno != EPERM) {
			warn("chown: %s", path);
			return -1;
		}
	}

	return 0;
}

static void cleanup_files(void)
{
	for (int i = 0; i < (int)n_files; i++) {
		struct file *p = files + i;
		free(p->source);
		free(p->symlink);
	}
	free(files);
}

static void show_help(int rc)
{
	fprintf(stdout,
		"Usage: %1$s [<options>] <destdir> directory [directory ...]\n"
		"   or: %1$s [<options>] <destdir> file [file ...]\n"
		"\n"
		"Utility allows to copy files and directories along with their dependencies\n"
		"into a specified destination directory.\n"
		"\n"
		"This utility follows symbolic links and binary dependencies and copies them\n"
		"along with the specified files.\n"
		"\n"
		"Options:\n"
		"   -n, --dry-run              don't do nothing.\n"
		"   -f, --force                overwrite destination file if exists.\n"
		"   -l, --log=FILE             white log about what was copied.\n"
		"   -r, --remove-prefix=PATH   ignore prefix in path.\n"
		"   -v, --verbose              print a message for each action/\n"
		"   -V, --version              output version information and exit.\n"
		"   -h, --help                 display this help and exit.\n"
		"\n"
		"Report bugs to authors.\n"
		"\n",
		progname
	);
	exit(rc);
}

static void show_version(void)
{
	fprintf(stdout,
		"%1$s version " PACKAGE_VERSION "\n"
		"Written by Alexey Gladkov.\n"
		"\n"
		"Copyright (C) 2012-2020  Alexey Gladkov <gladkov.alexey@gmail.com>\n"
		"This is free software; see the source for copying conditions.  There is NO\n"
		"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n",
		progname
	);
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
	const char *optstring = "fnl:r:vVh";
	const struct option longopts[] = {
		{"remove-prefix", required_argument, 0, 'r' },
		{"force", no_argument, 0, 'f' },
		{"dry-run", no_argument, 0, 'n' },
		{"log", required_argument, 0, 'l' },
		{"verbose", no_argument, 0, 'v' },
		{"version", no_argument, 0, 'V' },
		{"help", no_argument, 0, 'h' },
		{ 0, 0, 0, 0 }
	};
	int c;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	while ((c = getopt_long(argc, argv, optstring, longopts, NULL)) != -1) {
		switch (c) {
			case 'f':
				force = 1;
				break;
			case 'l':
				logfile = optarg;
				break;
			case 'n':
				dry_run = 1;
				break;
			case 'r':
				prefix = optarg;
				prefix_len = strlen(optarg);
				break;
			case 'v':
				verbose++;
				break;
			case 'V':
				show_version();
				break;
			case 'h':
				show_help(EXIT_SUCCESS);
				break;
			default:
				fprintf(stderr, "Try '%s --help' for more information.\n",
					progname);
				return EX_USAGE;
		}
	}

	if (optind == argc)
		errx(EX_USAGE, "more arguments required");

	destdir = argv[optind++];

	if (optind == argc)
		errx(EX_USAGE, "more arguments required");

	if (elf_version(EV_CURRENT) == EV_NONE)
		errx(EXIT_FAILURE, "ELF library initialization failed: %s", elf_errmsg(-1));

	if (verbose)
		warnx("remove prefix: %s", prefix);

	umask(0);

	int rc = EXIT_FAILURE;

	for (int i = optind; i < argc; i++)
		append_fullpath(argv[i]);

	if (read_files() < 0) {
		warnx("failed to read files");
		goto end;
	}

	if (files)
		qsort(files, n_files, sizeof(struct file), dsort);

	if (dry_run) {
		warnx("dry run only ...");
		print_files(stdout);
		goto end;
	}

	FILE *logfd = NULL;

	if (logfile && (logfd = fopen(logfile, "a")) == NULL) {
		warn("open: %s", logfile);
		goto end;
	}

	if (install_files() < 0) {
		warnx("failed to install files");
		goto end;
	}

	if (logfile) {
		print_files(logfd);
		fclose(logfd);
	}

	rc = EXIT_SUCCESS;
end:
	cleanup_files();
	return rc;
}
