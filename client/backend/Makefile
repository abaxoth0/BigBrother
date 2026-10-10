CC = x86_64-w64-mingw32-gcc
AR = x86_64-w64-mingw32-ar

ROOT = $(CURDIR)/../..
MBEDTLS_DIR = $(ROOT)/lib/mbedtls
COMMON_FLAGS = -Wall -Wno-format-truncation -mconsole \
	-I$(CURDIR)/include \
	-I$(ROOT)/common \
	-I$(MBEDTLS_DIR)/include

DEBUG_FLAGS = -g3 -O0
RELEASE_FLAGS = -O2

t-units = src/main.c src/common.c src/ipc_daemon.c src/ipc_client.c src/net_client.c src/tls_client.c src/client_key.c $(ROOT)/common/encoding/encoding.c $(ROOT)/common/log/log.c

# mbedTLS library sources (excluding the ARM-only AESCE accelerator).
MB_UNITS := $(wildcard $(MBEDTLS_DIR)/library/*.c)
MB_UNITS := $(filter-out %/aesce.c, $(MB_UNITS))
MBED_CFLAGS = -O2 -I$(MBEDTLS_DIR)/include
MBED_A = build/mbedtls/libmbedtls.a

CB_NAME = BigBrother Client Daemon
CLIENT_LIBS = $(MBED_A) -lws2_32 -liphlpapi -lbcrypt -lwinmm -ladvapi32
all: debug

debug: CFLAGS = $(COMMON_FLAGS) $(DEBUG_FLAGS)
debug: $(CB_NAME)

release: CFLAGS = $(COMMON_FLAGS) $(RELEASE_FLAGS)
release: $(CB_NAME)

$(MBED_A): $(MB_UNITS)
	mkdir -p build/mbedtls/obj
	@for f in $(MB_UNITS); do \
		o=build/mbedtls/obj/$$(basename $$f .c).o; \
		$(CC) $(MBED_CFLAGS) -c "$$f" -o "$$o" || exit 1; \
	done
	$(AR) rcs build/mbedtls/libmbedtls.a build/mbedtls/obj/*.o

$(CB_NAME): $(t-units) $(MBED_A)
	mkdir -p build
	$(CC) $(CFLAGS) -o "build/$(CB_NAME).exe" $(t-units) $(CLIENT_LIBS)

clean:
	rm -rf build

.PHONY: all debug release clean

# Run build/ipv6-setting-test/test.exe --write, then --read in a new process.
test-build: $(MBED_A)
	mkdir -p build/ipv6-setting-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/ipv6-setting-test/test.exe tests/ipv6_setting_test.c $(filter-out src/main.c,$(t-units)) $(CLIENT_LIBS)

.PHONY: test-build
