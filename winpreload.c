/* original source by https://www.mail-archive.com/devel@xfree86.org/msg05806.html */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <X11/Xlib.h>
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>

extern char **environ;

static const char env_prefix[] = "WINPRELOAD_";
static const int env_prefixlen = sizeof(env_prefix) - 1;
/* dlopened xlib so we can find the symbols in the real xlib to call them */
static void *lib_xlib = NULL;

#define MAX(a,b) ((a > b) ? a : b)
#define MIN(a,b) ((a < b) ? a : b)
#define INTERSECT(x,y,w,h,r)     (MAX(0, MIN((x)+(w),(r).x_org+(r).width)  \
									- MAX((x),(r).x_org))                  \
								* MAX(0, MIN((y)+(h),(r).y_org+(r).height) \
									- MAX((y),(r).y_org)))
#define ERRQUIT(msg) { fprintf(stderr, msg); exit(1); }
#define ENVPARSE(...)                                                \
	{                                                                \
		char **__envvar, *__enviter, *key, *value, __varbuf[256];    \
		for (__envvar = environ; *__envvar; __envvar++) {            \
			strncpy(__varbuf, *__envvar, sizeof(__varbuf) - 1);      \
			__varbuf[sizeof(__varbuf)  - 1] = '\0';                  \
			__enviter = strchr(__varbuf, '=');                       \
			if (!__enviter) continue;                                \
			*__enviter = '\0';                                       \
			key = __varbuf;                                          \
			value = __enviter + 1;                                   \
			if (strncmp(key, env_prefix, env_prefixlen))             \
				continue;                                            \
			key = &key[env_prefixlen];                               \
			__VA_ARGS__                                              \
		}                                                            \
	}
#define SETFMTPROP(name, format, val)                                        \
		{                                                                    \
			int __atom;                                                      \
			char __buf[1024];                                                \
			snprintf(__buf, sizeof(__buf), format, val);                     \
			__atom = XInternAtom(display, name, False);                      \
			XChangeProperty(display, window, __atom, XA_STRING, 8,           \
					PropModeReplace, (unsigned char*) __buf, strlen(__buf)); \
		}
#define SETPROP(name, type, size, val)                               \
		{                                                            \
			int __atom;                                              \
			__atom = XInternAtom(display, name, False);              \
			XChangeProperty(display, window, __atom, type, size,     \
					PropModeReplace, val, 1);                        \
		}

/* prototypes */
static void set_properties(Display *display, Window window);

int
monitor_by_pointer(XineramaScreenInfo *info, int mcount,
		Display *display, Window window)
{
	Window dummy;
	int x = 0, y = 0, di, i, screen;
	unsigned int dui;

	/* determine monitor by where the pointer is */
	XQueryPointer(display, window, &dummy, &dummy,
				&x, &y, &di, &di, &dui);

	for (screen = i = 0; i < mcount; i++) {
		if (x >= info[i].x_org && y >= info[i].y_org
				&& x < info[i].x_org + info[i].width
				&& y < info[i].y_org + info[i].height) {
			screen = i;
			break;
		}
	}

	if (i == mcount)
		return -1;
	else
		return screen;
}

void
set_properties(Display *display, Window window)
{
	int wx, wy, mx, my;
	unsigned int ww, wh, mw, mh;
	char *env = NULL;

	{
		Window root;
		unsigned int border, depth;

		/* get window geometry */
		if (!XGetGeometry(display, window, &root,
					&wx, &wy, &ww, &wh, &border, &depth))
			ERRQUIT("winpreload: unable to determine window geometry\n");

		/* use root geometry for monitor as default */
		if (!XGetGeometry(display, root, &root,
					&mx, &my, &mw, &mh, &border, &depth))
			ERRQUIT("winpreload: unable to determine screen geometry\n");
	}

	/* parse env for screen-related vars first */
	if (XineramaIsActive(display)) {
		XineramaScreenInfo *info;
		int mcount, screen, set;

		if (!(info = XineramaQueryScreens(display, &mcount)))
			goto cleanup_xinerama;

		/* NOTE:
		 * for dwm (tested ver 4.9), windows are contrained to the dimensions of
		 * their monitor (which is the selected monitor on creation). Because of
		 * this, it is not possible to init the screen position outside the
		 * selected monitor.. a patch of dwm or some kind of signal that the
		 * selected monitor should be changed is necessary
		 */
		screen = 0;
		set = 0;
		ENVPARSE(
			if (!strcmp("SCREEN_NUM", key)) {
				set = 1;
				screen = strtoul(value, NULL, 0);
				if (screen >= mcount)
					ERRQUIT("winpreload: invalid screen number specified\n");
			} else if (!strcmp("SCREEN_PTR", key)) {
				set = 1;
				screen = monitor_by_pointer(info, mcount, display, window);
				if (screen == -1)
					goto cleanup_xinerama;
			}
		)

		if (!set) {
			Window w, root, *dws, dw, pw;
			XWindowAttributes wa;
			int di, a, area, i;
			unsigned du;

			root = XDefaultRootWindow(display);
			screen = -1;

			/* check if a focussed window exists.. */
			XGetInputFocus(display, &w, &di);
			/* modified snippet from dmenu-4.9 source */
			if (w != root && w != PointerRoot && w != None) {
				do {
					if (XQueryTree(display, (pw = w), &dw, &w, &dws, &du) && dws)
						XFree(dws);
				} while (w != root && w != pw);
				/* find xinerama screen with which the window intersects most */
				if (XGetWindowAttributes(display, pw, &wa)) {
					for (i = 0; i < mcount; i++) {
						a = INTERSECT(wa.x, wa.y, wa.width, wa.height, info[i]);
						if (a > area) {
							area = a;
							screen = i;
						}
					}
				}
			}

			/* ..else try by pointer */
			if (screen == -1) {
				screen = monitor_by_pointer(info, mcount, display, window);
				if (screen == -1)
					goto cleanup_xinerama;
			}
		}

		mx = info[screen].x_org;
		my = info[screen].y_org;
		mw = info[screen].width;
		mh = info[screen].height;

cleanup_xinerama:
		XFree(info);
	}

	ENVPARSE(
		if (!strcmp(key, "POS_X")) {
			wx = strtoul(value, NULL, 0);
		} else if (!strcmp(key, "POS_Y")) {
			wy = strtoul(value, NULL, 0);
        } else if (!strcmp(key, "MPOS_X")) {
			wx = mx + strtoul(value, NULL, 0);
		} else if (!strcmp(key, "MPOS_Y")) {
			wy = my + strtoul(value, NULL, 0);
		} else if (!strcmp(key, "WIDTH")) {
			ww = strtoul(value, NULL, 0);
		} else if (!strcmp(key, "HEIGHT")) {
			wh = strtoul(value, NULL, 0);
		} else if (!strcmp(key, "POS_CENTER")) {
			wx = mx + (mw - ww) / 2.f;
			wy = my + (mh - wh) / 2.f;
		} else if (!strcmp(key, "DIALOG")) {
			Atom atom;

			atom = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
			SETPROP("_NET_WM_WINDOW_TYPE", XA_ATOM, 32, (void*) &atom);
		} else {
			SETFMTPROP(key, "%s", value);
		}
	)

	{
		uid_t uid;
		pid_t pid, ppid;
		
		uid = getuid();
		pid = getpid();
		ppid = getppid();

		/* set basic properties */
		SETPROP("_NET_WM_UID", XA_CARDINAL, 32, (void*) &uid);
		SETPROP("_NET_WM_PID", XA_CARDINAL, 32, (void*) &pid);
		SETPROP("_NET_WM_PPID", XA_CARDINAL, 32, (void*) &ppid);
	}

	/* update window pos and geometry */
	XMoveWindow(display, window, wx, wy);
	XResizeWindow(display, window, ww, wh);
}

/* XCreateWindow intercept */
Window
XCreateWindow(
	Display *display,
	Window parent,
	int x, int y,
	unsigned int width, unsigned int height,
	unsigned int border_width,
	int depth,
	unsigned int class,
	Visual *visual,
	unsigned long valuemask,
	XSetWindowAttributes *attributes)
{
	static Window (*func)(
		Display *display,
		Window parent,
		int x, int y,
		unsigned int width, unsigned int height,
		unsigned int border_width,
		int depth,
		unsigned int class,
		Visual *visual,
		unsigned long valuemask,
		XSetWindowAttributes *attributes) = NULL;
	int i;

	/* find the real Xlib and the real X function */
	if (!lib_xlib) lib_xlib = dlopen("libX11.so", RTLD_GLOBAL | RTLD_LAZY);
	if (!func) func = dlsym (lib_xlib, "XCreateWindow");

	/* multihead screen handling loop */
	for (i = 0; i < ScreenCount(display); i++) {
		/* if the window is created as a toplevel window */
		if (parent == RootWindow(display, i)) {
			Window window;

			/* create and set properties */
			window = (*func) (display, parent, x, y, width, height,
				border_width, depth, class, visual, valuemask, attributes);
			set_properties(display, window);

			return window;
		}
	}

	/* normal child window - create without properties */
	return (*func) (display, parent, x, y, width, height, border_width, depth,
			class, visual, valuemask, attributes);
}

/* XCreateSimpleWindow intercept */
Window
XCreateSimpleWindow(
	Display *display,
	Window parent,
	int x, int y,
	unsigned int width, unsigned int height,
	unsigned int border_width,
	unsigned long border,
	unsigned long background)
{
	static Window (*func)(
		Display *display,
		Window parent,
		int x, int y,
		unsigned int width, unsigned int height,
		unsigned int border_width,
		unsigned long border,
		unsigned long background) = NULL;
	int i;
	
	/* find the real Xlib and the real X function */
	if (!lib_xlib) lib_xlib = dlopen("libX11.so", RTLD_GLOBAL | RTLD_LAZY);
	if (!func) func = dlsym (lib_xlib, "XCreateSimpleWindow");
	
	/* multihead screen handling loop */
	for (i = 0; i < ScreenCount(display); i++) {
		/* if the window is created as a toplevel window */
		if (parent == RootWindow(display, i)) {
			Window window;

			/* create and set properties */
			window = (*func) (display, parent, x, y, width, height, 
				border_width, border, background);
			set_properties(display, window);
			return window;
		}
	}

	/* normal child window - create as usual */
	return (*func) (display, parent, x, y, width, height,
			border_width, border, background);
}

/* XReparentWindow intercept */
int
XReparentWindow(
	Display *display,
	Window window,
	Window parent,
	int x, int y)
{
	static int (*func)(
		Display *display,
		Window window,
		Window parent,
		int x, int y) = NULL;
	int i;
	
	/* find the real Xlib and the real X function */
	if (!lib_xlib) lib_xlib = dlopen("libX11.so", RTLD_GLOBAL | RTLD_LAZY);
	if (!func) func = dlsym (lib_xlib, "XReparentWindow");
	
	/* multihead screen handling loop */
	for (i = 0; i < ScreenCount(display); i++) {
		/* if the window is created as a toplevel window */
		if (parent == RootWindow(display, i)) {

			/* set properties and reparent */
			set_properties(display, window);
			return (*func) (display, window, parent, x, y);
		}
	}

	/* normal child window - reparent as usual */
	return (*func) (display, window, parent, x, y);
}

