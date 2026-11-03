include ../../common/version.mk
include ../../common/build.mk

CC = x86_64-w64-mingw32-gcc
AR = x86_64-w64-mingw32-ar
WINDRES = x86_64-w64-mingw32-windres
ICON_OBJ = build/app-icon.o

ROOT = $(CURDIR)/../..
MBEDTLS_DIR = $(ROOT)/lib/mbedtls
COMMON_FLAGS = $(BB_VERSION_FLAGS) -Wall -Wno-format-truncation -mconsole \
	-I$(CURDIR)/include \
	-I$(ROOT)/common \
	-I$(MBEDTLS_DIR)/include

DEBUG_FLAGS = -DDEBUG -g3 -O0
RELEASE_FLAGS = -DNDEBUG -O2

t-units = src/main.c src/common.c src/ipc_daemon.c src/ipc_client.c src/net_client.c src/tls_client.c src/client_key.c src/certificate_pin.c $(ROOT)/common/encoding/encoding.c $(ROOT)/common/log/log.c
OBJECTS = $(addprefix build/$(BUILD_MODE)/,$(patsubst %.c,%.o,$(patsubst $(ROOT)/%,%,$(t-units))))

# mbedTLS library sources (excluding the ARM-only AESCE accelerator).
MB_UNITS := $(wildcard $(MBEDTLS_DIR)/library/*.c)
MB_UNITS := $(filter-out %/aesce.c, $(MB_UNITS))
MBED_CFLAGS = -O2 -I$(MBEDTLS_DIR)/include
MBED_A = build/mbedtls/libmbedtls.a
MB_OBJECTS = $(patsubst $(MBEDTLS_DIR)/library/%.c,build/mbedtls/obj/%.o,$(MB_UNITS))

CB_NAME = BigBrother Client Daemon
CB_EXE = build/$(BUILD_MODE)/BigBrother\ Client\ Daemon.exe
NOTIFY_DIR = $(ROOT)/notifications
NOTIFY_A = $(NOTIFY_DIR)/build/$(BUILD_MODE)/libbbnotify.a
CLIENT_LIBS = $(NOTIFY_A) -Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic $(MBED_A) -lws2_32 -liphlpapi -lbcrypt -lwinmm -ladvapi32
all: $(BUILD_MODE)

debug: CFLAGS = $(COMMON_FLAGS) $(DEBUG_FLAGS)
debug: $(CB_EXE)
	cp "$<" "build/$(CB_NAME).exe"

release: CFLAGS = $(COMMON_FLAGS) $(RELEASE_FLAGS)
release: $(CB_EXE)
	cp "$<" "build/$(CB_NAME).exe"

$(NOTIFY_A): $(NOTIFY_DIR)/src/notify.cpp $(ROOT)/common/notify.h $(wildcard $(NOTIFY_DIR)/include/*.h) $(BB_BUILD_DEPS)
	$(MAKE) -C $(NOTIFY_DIR) $(BUILD_MODE)

$(MBED_A): $(MB_OBJECTS)
	$(AR) rcs $@ $^

build/mbedtls/obj/%.o: $(MBEDTLS_DIR)/library/%.c
	mkdir -p $(@D)
	$(CC) $(MBED_CFLAGS) -MMD -MP -c $< -o $@

build/$(BUILD_MODE)/src/%.o: src/%.c $(BB_VERSION_DEPS) $(BB_BUILD_DEPS)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

build/$(BUILD_MODE)/common/%.o: $(ROOT)/common/%.c $(BB_VERSION_DEPS) $(BB_BUILD_DEPS)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(ICON_OBJ): $(BB_VERSION_DEPS) app.rc $(ROOT)/assets/icons/BigBrother_Client_Backend.ico
	mkdir -p build
	$(WINDRES) $(BB_RESOURCE_VERSION_FLAGS) -i app.rc -o $(ICON_OBJ)

$(CB_EXE): $(BB_VERSION_DEPS) $(OBJECTS) $(MBED_A) $(ICON_OBJ) $(NOTIFY_A)
	mkdir -p build
	$(CC) $(CFLAGS) -o "$@" $(OBJECTS) $(ICON_OBJ) $(CLIENT_LIBS)

-include $(OBJECTS:.o=.d) $(MB_OBJECTS:.o=.d)

clean:
	rm -rf build

.PHONY: all debug release clean

# Run build/ipv6-setting-test/test.exe --write, then --read in a new process.
test-build: $(MBED_A) $(NOTIFY_A)
	mkdir -p build/ipv6-setting-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/ipv6-setting-test/test.exe tests/ipv6_setting_test.c $(filter-out src/main.c,$(t-units)) $(CLIENT_LIBS)

.PHONY: test-build

token-test-build: $(MBED_A) $(NOTIFY_A)
	mkdir -p build/token-recovery-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/token-recovery-test/test.exe tests/token_recovery_test.c $(filter-out src/main.c src/ipc_daemon.c,$(t-units)) $(CLIENT_LIBS)

.PHONY: token-test-build

certificate-test-build: $(MBED_A) $(NOTIFY_A)
	mkdir -p build/certificate-state-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/certificate-state-test/test.exe tests/certificate_state_test.c $(filter-out src/main.c src/ipc_daemon.c,$(t-units)) $(CLIENT_LIBS)
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/certificate-probe-test.exe tests/certificate_probe_test.c src/certificate_pin.c src/tls_client.c $(ROOT)/common/log/log.c $(CLIENT_LIBS)

.PHONY: certificate-test-build

ini-test-build: $(MBED_A) $(NOTIFY_A)
	mkdir -p build/ini-sections-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/ini-sections-test/test.exe tests/ini_sections_test.c $(filter-out src/main.c src/ipc_daemon.c,$(t-units)) $(CLIENT_LIBS)

.PHONY: ini-test-build

permissions-test-build:
	mkdir -p build/secret-permissions-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/secret-permissions-test/test.exe tests/secret_permissions_test.c -ladvapi32

.PHONY: permissions-test-build

notifications-test-build: $(MBED_A) $(NOTIFY_A)
	mkdir -p build/notifications-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/notifications-test/test.exe tests/notifications_test.c $(filter-out src/main.c,$(t-units)) $(CLIENT_LIBS)

.PHONY: notifications-test-build

whitelist-test-build: $(MBED_A) $(NOTIFY_A)
	mkdir -p build/whitelist-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o "build/whitelist-test/BigBrother Client.exe" tests/whitelist_test.c $(filter-out src/main.c,$(t-units)) $(CLIENT_LIBS)

.PHONY: whitelist-test-build

policy-test-build: $(MBED_A) $(NOTIFY_A)
	mkdir -p build/policy-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/policy-test/test.exe tests/policy_test.c $(filter-out src/main.c src/ipc_daemon.c,$(t-units)) $(CLIENT_LIBS)

.PHONY: policy-test-build

whitelist-sync-test-build: $(MBED_A) $(NOTIFY_A)
	mkdir -p build/whitelist-sync-test
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/whitelist-sync-test/test.exe tests/whitelist_sync_test.c $(filter-out src/main.c src/ipc_daemon.c,$(t-units)) $(CLIENT_LIBS)

.PHONY: whitelist-sync-test-build
