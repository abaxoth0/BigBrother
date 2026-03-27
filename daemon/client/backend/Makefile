CC = x86_64-w64-mingw32-gcc

ROOT = $(CURDIR)/../..
COMMON_FLAGS = -Wall -mconsole \
	-I$(CURDIR)/include \
	-I$(ROOT)/common

DEBUG_FLAGS = -g3 -O0
RELEASE_FLAGS = -O2

t-units = src/main.c src/common.c src/ipc_daemon.c src/ipc_client.c $(ROOT)/common/encoding/encoding.c $(ROOT)/common/log/log.c

all: debug

debug: CFLAGS = $(COMMON_FLAGS) $(DEBUG_FLAGS)
debug: LDFLAGS = -lws2_32
debug: bb-client.exe

release: CFLAGS = $(COMMON_FLAGS) $(RELEASE_FLAGS)
release: LDFLAGS = -lws2_32
release: bb-client.exe

bb-client.exe: $(t-units)
	mkdir -p build
	$(CC) $(CFLAGS) -o build/$@ $(t-units) $(LDFLAGS)

clean:
	rm -rf build

.PHONY: all debug release clean
