CC = x86_64-w64-mingw32-gcc
AR = x86_64-w64-mingw32-ar
WINDRES = x86_64-w64-mingw32-windres
ICON_OBJ = build/app-icon.o

ROOT = $(CURDIR)/../..
MBEDTLS_DIR = $(ROOT)/lib/mbedtls
COMMON_FLAGS = -Wall -Wno-format-truncation -mconsole \
	-I$(CURDIR)/include \
	-I$(ROOT)/common \
	-I$(MBEDTLS_DIR)/include

DEBUG_FLAGS = -g3 -O0
RELEASE_FLAGS = -O2

t-units = src/main.c src/common.c src/ipc_daemon.c src/ipc_client.c src/net_client.c src/tls_client.c src/client_key.c src/certificate_pin.c $(ROOT)/common/encoding/encoding.c $(ROOT)/common/log/log.c

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

$(ICON_OBJ): app.rc $(ROOT)/assets/icons/BigBrother_Client_Backend.ico
	mkdir -p build
	$(WINDRES) -i app.rc -o $(ICON_OBJ)

$(CB_NAME): $(t-units) $(MBED_A) $(ICON_OBJ)
	mkdir -p build
	$(CC) $(CFLAGS) -o "build/$(CB_NAME).exe" $(t-units) $(ICON_OBJ) $(CLIENT_LIBS)

clean:
	rm -rf build

.PHONY: all debug release clean

# Run build/ipv6-setting-test/test.exe --write, then --read in a new process.
test-build: $(MBED_A)
	mkdir -p build/ipv6-setting-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/ipv6-setting-test/test.exe tests/ipv6_setting_test.c $(filter-out src/main.c,$(t-units)) $(CLIENT_LIBS)

.PHONY: test-build

token-test-build: $(MBED_A)
	mkdir -p build/token-recovery-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/token-recovery-test/test.exe tests/token_recovery_test.c $(filter-out src/main.c src/ipc_daemon.c,$(t-units)) $(CLIENT_LIBS)

.PHONY: token-test-build

certificate-test-build: $(MBED_A)
	mkdir -p build/certificate-state-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/certificate-state-test/test.exe tests/certificate_state_test.c $(filter-out src/main.c src/ipc_daemon.c,$(t-units)) $(CLIENT_LIBS)
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/certificate-probe-test.exe tests/certificate_probe_test.c src/certificate_pin.c src/tls_client.c $(ROOT)/common/log/log.c $(CLIENT_LIBS)

.PHONY: certificate-test-build

ini-test-build: $(MBED_A)
	mkdir -p build/ini-sections-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/ini-sections-test/test.exe tests/ini_sections_test.c $(filter-out src/main.c src/ipc_daemon.c,$(t-units)) $(CLIENT_LIBS)

.PHONY: ini-test-build

permissions-test-build:
	mkdir -p build/secret-permissions-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/secret-permissions-test/test.exe tests/secret_permissions_test.c -ladvapi32

.PHONY: permissions-test-build
