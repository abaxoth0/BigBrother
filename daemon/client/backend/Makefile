CC = x86_64-w64-mingw32-gcc

ROOT = $(CURDIR)/../..
COMMON_FLAGS = -Wall -Wno-format-truncation -mconsole \
	-I$(CURDIR)/include \
	-I$(ROOT)/common

DEBUG_FLAGS = -g3 -O0
RELEASE_FLAGS = -O2

t-units = src/main.c src/common.c src/ipc_daemon.c src/ipc_client.c src/net_client.c $(ROOT)/common/encoding/encoding.c $(ROOT)/common/log/log.c

CB_NAME = BigBrother Client Daemon
CLIENT_LIBS = -lws2_32 -liphlpapi
all: debug

debug: CFLAGS = $(COMMON_FLAGS) $(DEBUG_FLAGS)
debug: $(CB_NAME)

release: CFLAGS = $(COMMON_FLAGS) $(RELEASE_FLAGS)
release: $(CB_NAME)

$(CB_NAME): $(t-units)
	mkdir -p build
	$(CC) $(CFLAGS) -o "build/$(CB_NAME).exe" $(t-units) $(CLIENT_LIBS)

clean:
	rm -rf build

.PHONY: all debug release clean
