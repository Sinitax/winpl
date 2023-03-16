#include <linux/limits.h>
#include <sys/stat.h>
#include <err.h>
#include <dirent.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>

#define ARRLEN(x) (sizeof(x) / sizeof((x)[0]))

static const char *libpath = "/tmp/winpl.so";

extern char _binary_winpl_so_start[];
extern char _binary_winpl_so_end[];

extern char **environ;

void
usage(int rc, bool full)
{
	fprintf(stderr, "Usage: winpl [OPT].. -- CMD [ARG]..\n");
	if (!full) exit(rc);
	fprintf(stderr, "\n");
	fprintf(stderr, "  [-ax|-ay] POS    Absolute window position\n");
	fprintf(stderr, "  [-mx|-my] POS    Window position relative to monitor\n");
	fprintf(stderr, "  [-rx|-ry] SCALE  Window position relative to monitor\n"
	                "                   as a factor of monitor size\n");
	fprintf(stderr, "  [-aw|-ah] SIZE   Absolute window width/height\n");
	fprintf(stderr, "  [-rw|-rh] SCALE  Window size relative to monitor\n");
	fprintf(stderr, "  -c               Center window on monitor\n");
	fprintf(stderr, "  -f               Tag window as dialog (floating)\n");
	fprintf(stderr, "  -mn SCREEN       Select monitor based on number\n");
	fprintf(stderr, "  -mp              Select monitor based on cursor\n");
	fprintf(stderr, "  -mf              Select monitor based on focus\n");
	fprintf(stderr, "  -ni              Window does not take input (focus)\n");
	fprintf(stderr, "\n");
	exit(rc);
}

void
write_lib(const char *filename)
{
	FILE *file;
	size_t size;
	void *data;

	file = fopen(filename, "wb+");
	if (!file) err(1, "fopen %s", filename);

	data = (void*) _binary_winpl_so_start;
	size = (size_t) (_binary_winpl_so_end - _binary_winpl_so_start);

	if (fwrite(data, size, 1, file) != 1)
		errx(1, "failed to write winpl lib");

	fclose(file);
}

bool
find_bin(char *pathbuf, const char *bin)
{
	char tmp[PATH_MAX];
	const char *env_path;
	const char *tok, *start, *end;
	struct dirent *ent;
	DIR *dir;

	env_path = getenv("PATH");
	if (!env_path) return false;

	start = tok = env_path;
	while (tok) {
		tok = strchr(start, ':');
		if (!tok) end = start + strlen(start);
		else end = tok;

		snprintf(tmp, PATH_MAX, "%.*s", (int) (end - start), start);
		dir = opendir(tmp);
		if (!dir) goto next;

		while ((ent = readdir(dir))) {
			if (!strcmp(ent->d_name, bin)) {
				snprintf(pathbuf, PATH_MAX, "%s/%s",
					tmp, ent->d_name);
				return true;
			}
		}

		closedir(dir);

next:
		start = tok + 1;
	}

	return false;
}

int
main(int argc, char *const *argv)
{
	char *const *arg, *const *cmd_argv;
	char pathbuf[PATH_MAX];
	struct stat st;
	int rc;

	if (argc < 1) return 0;

	cmd_argv = NULL;
	for (arg = argv + 1; *arg; arg++) {
		if (!strcmp(*arg, "-h")) {
			usage(0, true);
		} else if (!strcmp(*arg, "-ax")) {
			setenv("WINPL_WX", *++arg, true);
		} else if (!strcmp(*arg, "-ay")) {
			setenv("WINPL_WY", *++arg, true);
		} else if (!strcmp(*arg, "-mx")) {
			setenv("WINPL_MWX", *++arg, true);
		} else if (!strcmp(*arg, "-my")) {
			setenv("WINPL_MWY", *++arg, true);
		} else if (!strcmp(*arg, "-rx")) {
			setenv("WINPL_RWX", *++arg, true);
		} else if (!strcmp(*arg, "-ry")) {
			setenv("WINPL_RWY", *++arg, true);
		} else if (!strcmp(*arg, "-aw")) {
			setenv("WINPL_WW", *++arg, true);
		} else if (!strcmp(*arg, "-ah")) {
			setenv("WINPL_WH", *++arg, true);
		} else if (!strcmp(*arg, "-rw")) {
			setenv("WINPL_RWW", *++arg, true);
		} else if (!strcmp(*arg, "-rh")) {
			setenv("WINPL_RWH", *++arg, true);
		} else if (!strcmp(*arg, "-c")) {
			setenv("WINPL_CENTER", "1", true);
		} else if (!strcmp(*arg, "-f")) {
			setenv("WINPL_FLOAT", "1", true);
		} else if (!strcmp(*arg, "-mn")) {
			setenv("WINPL_MON_NUM", *++arg, true);
		} else if (!strcmp(*arg, "-mp")) {
			setenv("WINPL_MON_PTR", "1", true);
		} else if (!strcmp(*arg, "-mf")) {
			setenv("WINPL_MON_FOCUS", "1", true);
		} else if (!strcmp(*arg, "-ni")) {
			setenv("WINPL_NO_INPUT", "1", true);
		} else if (!strcmp(*arg, "--") && *(arg+1)) {
			cmd_argv = arg + 1;
			break;
		} else {
			usage(1, true);
		}
	}
	if (cmd_argv == NULL)
		usage(1, false);

	if (stat(libpath, &st))
		write_lib(libpath);

	setenv("LD_PRELOAD", libpath, true);

	if (!find_bin(pathbuf, *cmd_argv))
		errx(1, "Binary not in PATH: %s", *cmd_argv);

	rc = execve(pathbuf, cmd_argv, environ);
	if (rc) err(1, "execve %s", pathbuf);
}
