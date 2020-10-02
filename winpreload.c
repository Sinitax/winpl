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

extern char **environ;

static const char env_prefix[] = "WINPRELOAD_";
static const int env_prefixlen = sizeof(env_prefix) - 1;
/* dlopened xlib so we can find the symbols in the real xlib to call them */
static void *lib_xlib = NULL;

#define SETFMTPROP(name, format, val)                                       \
		{                                                                \
			char buf[1024];                                              \
			snprintf(buf, sizeof(buf), format, val);                     \
			atom = XInternAtom(display, name, False);                    \
			XChangeProperty(display, window, atom, XA_STRING, 8,         \
					PropModeReplace, (unsigned char*) buf, strlen(buf)); \
		}
#define SETPROP(name, type, size, val)                                   \
		{                                                                \
			atom = XInternAtom(display, name, False);                    \
			XChangeProperty(display, window, atom, type, size,           \
					PropModeReplace, val, 1); \
		}

/* prototypes */
static void set_properties(Display *display, Window window);

void
set_properties(Display *display, Window window)
{
	int wx, wy, dx, dy, atom = 0;
	unsigned int ww, wh, dw, dh;
	char *env = NULL;

	{
		Window root;
		unsigned int width, height, border, depth;
		Screen *screen = XDefaultScreenOfDisplay(display);
		dw = screen->width;
		dh = screen->height;

		XGetGeometry(display, screen->root,
				&root, &dx, &dy, &width, &height, &border, &depth);

		XGetGeometry(display, window,
				&root, &wx, &wy, &ww, &wh, &border, &depth);
	}

	printf("%i %i %i %i\n", dx, dy, dw, dh);

	char **s, *iter, *key, *value;
	for (s = environ; *s; s++) {
		/* parse key=value pair */
		iter = strchr(*s, '=');
		if (!iter)
			continue;
		*iter = '\0';
		key = *s;
		value = iter + 1;

		/* check if meant for us */
		if (strncmp(key, env_prefix, env_prefixlen))
			continue;

		key = &key[env_prefixlen];
		if (!strcmp(key, "POS_X")) {
			wx = dx + atoi(value);
		} else if (!strcmp(key, "POS_Y")) {
			wy = dy + atoi(value);
		} else if (!strcmp(key, "POS_CENTER")) {
			wx = dx + dw / 2.f;
			wy = dy + dh / 2.f;
		} else if (!strcmp(key, "DIALOG")) {
			Atom a;

			a = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
			SETPROP("_NET_WM_WINDOW_TYPE", XA_ATOM, 32, (void*) &a);
		} else {
			SETFMTPROP(key, "%s", value);
		}
	}

	{
		uid_t uid;
		pid_t pid, ppid;
		
		uid = getuid();
		pid = getpid();
		ppid = getppid();

		SETPROP("_NET_WM_UID", XA_CARDINAL, 32, (void*) &uid);
		SETPROP("_NET_WM_PID", XA_CARDINAL, 32, (void*) &pid);
		SETPROP("_NET_WM_PPID", XA_CARDINAL, 32, (void*) &ppid);
	}

	{
		XWindowChanges values = { .x = wx, .y = wy, .width = ww, .height = wh };
		int value_mask = CWX | CWY | CWWidth | CWHeight;

		XConfigureWindow(display, window, value_mask, &values);
	}
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

