/* original: https://www.mail-archive.com/devel@xfree86.org/msg05806.html */

#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <dlfcn.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

#define MAX(a,b) ((a > b) ? a : b)
#define MIN(a,b) ((a < b) ? a : b)

extern char **environ;

static const char prefix[] = "WINPL_";
static const int prefixlen = sizeof(prefix) - 1;

static void *lib_xlib = NULL;

static void
warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "winpl: warn: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
}

static void
err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "winpl: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);

	exit(1);
}

static char **
iter_args(char **env, const char **key, size_t *keylen, const char **val)
{
	const char *tok;

	for (; *env; env++) {            \
		if (strncmp(*env, prefix, prefixlen))
			continue;
		*key = *env + prefixlen;
		tok = strchr(*env, '=');
		if (!tok) continue;
		*keylen = tok - *env - prefixlen;
		*val = tok + 1;
		return env + 1;
	}

	return NULL;
}

static int
intersection_area(XineramaScreenInfo *info, XWindowAttributes *wa)
{
	int dx, dy;

	dx = MIN(wa->x + wa->width, info->x_org + info->width);
	dx -= MAX(wa->x, info->x_org);

	dy = MIN(wa->y + wa->height, info->y_org + info->height);
	dy -= MAX(wa->y, info->y_org);

	return MAX(0, dx) * MAX(0, dy);
}

static int
monitor_from_pointer(XineramaScreenInfo *info, int mcount,
		Display *display, Window window)
{
	Window dummy;
	int x = 0, y = 0, di, i, screen;
	unsigned int dui;

	screen = -1;
	XQueryPointer(display, window, &dummy, &dummy,
		&x, &y, &di, &di, &dui);

	for (i = 0; i < mcount; i++) {
		if (x >= info[i].x_org && y >= info[i].y_org
				&& x < info[i].x_org + info[i].width
				&& y < info[i].y_org + info[i].height) {
			screen = i;
			break;
		}
	}

	return screen;
}

static int
monitor_from_focussed(Display *display, XineramaScreenInfo *info, int mcount)
{
	Window w, root, *dws, dw, pw;
	XWindowAttributes wa;
	int di, area, maxarea, i;
	unsigned du;
	int screen;

	screen = -1;
	root = XDefaultRootWindow(display);

	/* check if a focussed window exists.. */
	XGetInputFocus(display, &w, &di);

	/* modified snippet from dmenu-4.9 source */
	if (w != root && w != PointerRoot && w != None) {
		do {
			if (XQueryTree(display, (pw = w), &dw, &w, &dws, &du) && dws)
				XFree(dws);
		} while (w != root && w != pw);

		/* find xinerama screen with largest screen intersection */
		if (XGetWindowAttributes(display, pw, &wa)) {
			for (i = 0; i < mcount; i++) {
				area = intersection_area(&info[i], &wa);
				if (area > maxarea) {
					maxarea = area;
					screen = i;
				}
			}
		}
	}

	return screen;
}

static void
set_prop(Display *display, Window window, const char *name,
	int type, size_t size, void *val)
{
	int atom;

	atom = XInternAtom(display, name, False);
	XChangeProperty(display, window, atom, type, size,
			PropModeReplace, val, 1);
}

static void
set_props(Display *display, Window window)
{
	int wx, wy, mx, my;
	unsigned int ww, wh, mw, mh;
	const char *key, *val;
	unsigned int border, depth;
	XWMHints hints;
	size_t keylen;
	char **env;
	Window root;
	Atom atom;
	pid_t pid, ppid;
	uid_t uid;

	uid = getuid();
	pid = getpid();
	ppid = getppid();

	if (!XGetGeometry(display, window, &root,
			&wx, &wy, &ww, &wh, &border, &depth))
		err("Failed to get window geometry");

	if (!XGetGeometry(display, root, &root,
			&mx, &my, &mw, &mh, &border, &depth))
		err("Failed to get screen geometry");

	if (XineramaIsActive(display)) {
		XineramaScreenInfo *info;
		int mcount, mon;

		if (!(info = XineramaQueryScreens(display, &mcount)))
			err("Failed to query xinerama");

		mon = -1;

		env = environ;
		while ((env = iter_args(env, &key, &keylen, &val))) {
			if (!strncmp("MON_NUM", key, keylen)) {
				mon = strtoul(val, NULL, 0);
				if (mon < 0 || mon >= mcount)
					err("Monitor number OOB");
			} else if (!strncmp("MON_PTR", key, keylen)) {
				mon = monitor_from_pointer(info,
					mcount, display, window);
			} else if (!strncmp("MON_FOCUS", key, keylen)) {
				mon = monitor_from_focussed(display,
					info, mcount);
			} else if (keylen >= 4 && !strncmp(key, "MON_", 4)) {
				err("Unsupported env var: WINPL_%.*s",
					(int) keylen, key);
			}
		}

		if (mon == -1)
			mon = monitor_from_focussed(display, info, mcount);

		if (mon == -1)
			mon = monitor_from_pointer(info, mcount,
				display, window);

		if (mon == -1)
			err("Failed to get monitor");

		mx = info[mon].x_org;
		my = info[mon].y_org;
		mw = info[mon].width;
		mh = info[mon].height;
	} else {
		env = environ;
		while ((env = iter_args(env, &key, &keylen, &val))) {
			if (keylen >= 4 && !strncmp(key, "MON_", 4)) {
				warn("Xinerama is not active");
				break;
			}
		}
	}

	env = environ;
	while ((env = iter_args(env, &key, &keylen, &val))) {
		if (!strncmp(key, "WX", keylen)) {
			/* absolute window x */
			wx = strtoul(val, NULL, 0);
		} else if (!strncmp(key, "WY", keylen)) {
			/* absolute window y */
			wy = strtoul(val, NULL, 0);
		} else if (!strncmp(key, "RWX", keylen)) {
			/* window x relative to monitor size */
			wx = mw * strtof(val, NULL);
		} else if (!strncmp(key, "RWY", keylen)) {
			/* window y relative to monitor size */
			wy = mh * strtof(val, NULL);
		} else if (!strncmp(key, "MWX", keylen)) {
			/* window x from monitor top left */
			wx = mx + strtoul(val, NULL, 0);
		} else if (!strncmp(key, "MWY", keylen)) {
			/* window y from monitor top left */
			wy = my + strtoul(val, NULL, 0);
		} else if (!strncmp(key, "WW", keylen)) {
			/* window width */
			ww = strtoul(val, NULL, 0);
		} else if (!strncmp(key, "WH", keylen)) {
			/* window height */
			wh = strtoul(val, NULL, 0);
		} else if (!strncmp(key, "RWW", keylen)) {
			/* window width relative to monitor size */
			ww = mw * strtof(val, NULL);
		} else if (!strncmp(key, "RWH", keylen)) {
			/* window height relative to monitor size */
			wh = mh * strtof(val, NULL);
		} else if (!strncmp(key, "CENTER", keylen)) {
			/* window centered in monitor */
			wx = mx + (mw - ww) / 2.f;
			wy = my + (mh - wh) / 2.f;
		} else if (!strncmp(key, "FLOAT", keylen)) {
			/* window 'floating' in tiled WM */
			atom = XInternAtom(display,
				"_NET_WM_WINDOW_TYPE_DIALOG", False);
			set_prop(display, window, "_NET_WM_WINDOW_TYPE",
				XA_ATOM, 32, &atom);
		} else if (!strncmp(key, "NO_INPUT", keylen)) {
			/* never take input (focus) */
			hints.flags = InputHint;
			hints.input = false;
			XSetWMHints(display, window, &hints);
		} else if (keylen < 4 || strncmp(key, "MON_", 4)) {
			err("Unsupported env var: WINPL_%.*s",
				(int) keylen, key);
		}
	}

	/* set basic properties */
	set_prop(display, window, "_NET_WM_UID",
		XA_CARDINAL, 32, &uid);
	set_prop(display, window, "_NET_WM_PID",
		XA_CARDINAL, 32, &pid);
	set_prop(display, window, "_NET_WM_PPID",
		XA_CARDINAL, 32, &ppid);

	/* update window pos and geometry */
	XMoveResizeWindow(display, window, wx, wy, ww, wh);
}

Window
XCreateWindow(Display *display, Window parent, int x, int y,
	unsigned int width, unsigned int height, unsigned int border_width,
	int depth, unsigned int class, Visual *visual, unsigned long valuemask,
	XSetWindowAttributes *attributes)
{
	static Window (*func)(Display *display, Window parent, int x, int y,
		unsigned int width, unsigned int height,
		unsigned int border_width, int depth, unsigned int class,
		Visual *visual, unsigned long valuemask,
		XSetWindowAttributes *attributes) = NULL;
	Window window;
	int i;

	if (!lib_xlib) lib_xlib = dlopen("libX11.so", RTLD_GLOBAL | RTLD_LAZY);
	if (!func) func = dlsym (lib_xlib, "XCreateWindow");

	for (i = 0; i < ScreenCount(display); i++) {
		/* for toplevel windows */
		if (parent == RootWindow(display, i)) {
			window = (*func) (display, parent, x, y, width, height,
				border_width, depth, class, visual, valuemask, attributes);
			set_props(display, window);
			return window;
		}
	}

	/* create window as usual */
	return (*func) (display, parent, x, y, width, height, border_width, depth,
			class, visual, valuemask, attributes);
}

Window
XCreateSimpleWindow(Display *display, Window parent, int x, int y,
	unsigned int width, unsigned int height, unsigned int border_width,
	unsigned long border, unsigned long background)
{
	static Window (*func)(Display *display, Window parent, int x, int y,
		unsigned int width, unsigned int height,
		unsigned int border_width, unsigned long border,
		unsigned long background) = NULL;
	Window window;
	int i;

	if (!lib_xlib) lib_xlib = dlopen("libX11.so", RTLD_GLOBAL | RTLD_LAZY);
	if (!func) func = dlsym (lib_xlib, "XCreateSimpleWindow");

	for (i = 0; i < ScreenCount(display); i++) {
		/* for toplevel windows */
		if (parent == RootWindow(display, i)) {
			window = (*func) (display, parent, x, y, width, height, 
				border_width, border, background);
			set_props(display, window);
			return window;
		}
	}

	/* create window as usual */
	return (*func) (display, parent, x, y, width, height,
		border_width, border, background);
}

int
XReparentWindow(Display *display, Window window, Window parent, int x, int y)
{
	static int (*func)(Display *display, Window window, Window parent,
		int x, int y) = NULL;
	int i;

	if (!lib_xlib) lib_xlib = dlopen("libX11.so", RTLD_GLOBAL | RTLD_LAZY);
	if (!func) func = dlsym (lib_xlib, "XReparentWindow");

	for (i = 0; i < ScreenCount(display); i++) {
		/* for toplevel windows */
		if (parent == RootWindow(display, i)) {
			set_props(display, window);
			return (*func)(display, window, parent, x, y);
		}
	}

	/* reparent as usual */
	return (*func) (display, window, parent, x, y);
}

