#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#define ARRLEN(x) (sizeof(x) / sizeof((x)[0]))

#define ERROR(msg) { fprintf(stderr, "winpl: %s\n", msg); exit(1); }

extern char _binary_winpl_so_start[];
extern char _binary_winpl_so_end[];

extern char **environ;

static void write_lib(const char *filename);
static bool parse_arg(const char *arg);

static const char *conv[][2] = {
	{ "wx:", "WINPL_WX" },
	{ "wy:", "WINPL_WY" },
	{ "rwx:", "WINPL_RWX" },
	{ "rwy:", "WINPL_RWY" },
	{ "mwx:", "WINPL_MWX" },
	{ "mwy:", "WINPL_MWY" },
	{ "ww:", "WINPL_WW" },
	{ "wh:", "WINPL_WH" },
	{ "rww:", "WINPL_RWW" },
	{ "rwh:", "WINPL_RWH" },
	{ "center", "WINPL_CENTER" },
	{ "float", "WINPL_FLOAT" },
};

void
write_lib(const char *filename)
{
	FILE *file;
	void *data;
	size_t expected, actual;

	file = fopen(filename, "w+");
	if (!file) ERROR("Failed to create temp file");

	data = (void*) _binary_winpl_so_start;
	expected = (size_t) (_binary_winpl_so_end - _binary_winpl_so_start);

	actual = fwrite(data, 1, expected, file);
	if (actual != expected) ERROR("Failed to write temp file");

	fclose(file);
}

bool
parse_arg(const char *arg)
{
	int i, len;

	for (i = 0; i < ARRLEN(conv); i++) {
		len = strlen(conv[i][0]);
		if (strncmp(arg, conv[i][0], len))
			continue;

		if (conv[i][0][len-1] == ':') {
			setenv(conv[i][1], arg + len, true);
		} else if (!arg[len]) {
			setenv(conv[i][1], "1", true);
		} else {
			return false;;
		}

		return true;
	}

	return false;
}

int
main(int argc, char *const *argv)
{
	char *tmpfile = "/tmp/winpl.so";
	struct stat st;
	char *const *arg, *const *cmd;

	if (argc < 1) return 0;

	cmd = NULL;
	for (arg = argv + 1; *arg; arg++) {
		if (!strcmp(*arg, "--") && *(arg+1)) {
			cmd = arg + 1;
			break;
		}

		if (!parse_arg(*arg))
			ERROR("Invalid argument");
	}

	if (cmd == NULL)
		ERROR("Missing args separator");

	if (stat(*cmd, &st))
		ERROR("Binary not full path");

	if (stat(tmpfile, &st))
		write_lib(tmpfile);

	setenv("LD_PRELOAD", tmpfile, true);

	execve(*cmd, cmd, environ);
}
