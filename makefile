CFLAGS = -nostartfiles -fPIC -shared -Wl,-soname,xwrap.so --std=gnu99
LDLIBS = -ldl -lX11 -lXinerama

winpreload.so: winpreload.c
	$(CC) -o $@ $< $(CFLAGS) $(LDLIBS)
