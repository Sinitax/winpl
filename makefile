CFLAGS = -nostartfiles -fPIC -shared -Wl,-soname,xwrap.so
LDLIBS = -ldl -lX11 -lXinerama

winpreload.so: winpreload.c
	$(CC) -o $@ $< $(CFLAGS) $(LDLIBS)
