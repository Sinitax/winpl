CFLAGS = --std=gnu99 -Wmissing-prototypes -Wunused-variable -g
LIB_FLAGS = $(CFLAGS) -nostartfiles -fPIC -shared -Wl,-soname,xwrap.so
LOADER_FLAGS = $(CFLAGS)
LDLIBS = -ldl -lX11 -lXinerama

.PHONY: all clean

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


