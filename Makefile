PREFIX ?= /usr/local
BINDIR ?= /bin

CFLAGS = -Wunused-variable -Wunused-function -g
LIB_FLAGS = $(CFLAGS) -nostartfiles -fPIC -shared -Wl,-soname,xwrap.so
LOADER_FLAGS = $(CFLAGS)
LDLIBS = -ldl -lX11 -lXinerama

all: winpl

clean:
	rm -f winpl winpl.so winpl.so.o

winpl: loader.c winpl.so.o
	$(CC) -o $@ $^ $(LOADER_FLAGS)

winpl.so: winpl.c
	$(CC) -o $@ $< $(LIB_FLAGS) $(LDLIBS)

winpl.so.o: winpl.so
	objcopy --input binary --output elf64-x86-64 \
		--binary-architecture i386:x86-64 $< $@

install: winpl
	install -m755 winpl -t "$(DESTDIR)$(PREFIX)$(BINDIR)"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)$(BINDIR)/winpl"

.PHONY: all clean install uninstall
