CC = x86_64-w64-mingw32-gcc

ROOT = $(CURDIR)/../..
COMMON_FLAGS = -Wall -mconsole -I$(ROOT)/common

DEBUG_FLAGS = -g3 -O0
RELEASE_FLAGS = -O2

t-units = src/main.c src/ipc.c $(ROOT)/common/encoding/encoding.c

all: debug

debug: CFLAGS = $(COMMON_FLAGS) $(DEBUG_FLAGS)
debug: bb-client.exe

release: CFLAGS = $(COMMON_FLAGS) $(RELEASE_FLAGS)
release: bb-client.exe

bb-client.exe: $(t-units)
	mkdir -p build
	$(CC) $(CFLAGS) -o build/$@ $(t-units)

clean:
	rm -rf build

.PHONY: all debug release clean
