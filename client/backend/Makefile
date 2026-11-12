include ../../common/version.mk
include ../../common/build.mk

CC = x86_64-w64-mingw32-gcc
CXX = x86_64-w64-mingw32-g++
AR = x86_64-w64-mingw32-ar
WINDRES = x86_64-w64-mingw32-windres
ICON_OBJ = build/app-icon.o

ROOT = $(CURDIR)/../..
MBEDTLS_DIR = $(ROOT)/lib/mbedtls
COMMON_FLAGS = $(BB_VERSION_FLAGS) -Wall -Wextra -mconsole \
	-I$(CURDIR)/include \
	-I$(ROOT)/common \
	-I$(MBEDTLS_DIR)/include

DEBUG_FLAGS = -DDEBUG -g3 -O0
RELEASE_FLAGS = -DNDEBUG -O2

t-units = src/backend_client.cpp src/runtime.cpp src/hub.cpp src/firewall.cpp src/logs.cpp src/updates.cpp src/main.cpp src/common.cpp src/ipc_daemon.cpp src/ipc_client.cpp src/net_client.cpp src/tls_client.cpp src/client_key.cpp src/certificate_pin.cpp $(ROOT)/common/encoding/encoding.c $(ROOT)/common/log/log.c
OBJECTS = $(addprefix build/$(BUILD_MODE)/,$(patsubst %.cpp,%.o,$(patsubst %.c,%.o,$(patsubst $(ROOT)/%,%,$(t-units)))))
CXXFLAGS = $(COMMON_FLAGS) $(if $(filter release,$(BUILD_MODE)),$(RELEASE_FLAGS),$(DEBUG_FLAGS)) -std=c++20

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
CLIENT_LIBS = $(NOTIFY_A) -Wl,-Bstatic -lstdc++ -lwinpthread -Wl,-Bdynamic $(MBED_A) -lws2_32 -liphlpapi -lbcrypt -lwinmm -ladvapi32 -luserenv
SHARED_OBJECTS = $(filter %/common/log/log.o %/common/encoding/encoding.o,$(OBJECTS))
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

build/$(BUILD_MODE)/src/%.o: src/%.cpp $(BB_VERSION_DEPS) $(BB_BUILD_DEPS)
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

build/$(BUILD_MODE)/common/%.o: $(ROOT)/common/%.c $(BB_VERSION_DEPS) $(BB_BUILD_DEPS)
	mkdir -p $(@D)
	$(CC) $(COMMON_FLAGS) $(if $(filter release,$(BUILD_MODE)),$(RELEASE_FLAGS),$(DEBUG_FLAGS)) -MMD -MP -c $< -o $@

$(ICON_OBJ): $(BB_VERSION_DEPS) app.rc $(ROOT)/assets/icons/BigBrother_Client_Backend.ico
	mkdir -p build
	$(WINDRES) $(BB_RESOURCE_VERSION_FLAGS) -i app.rc -o $(ICON_OBJ)

$(CB_EXE): $(BB_VERSION_DEPS) $(OBJECTS) $(MBED_A) $(ICON_OBJ) $(NOTIFY_A)
	mkdir -p build
	$(CXX) $(CXXFLAGS) -static-libgcc -static-libstdc++ -o "$@" $(OBJECTS) $(ICON_OBJ) $(CLIENT_LIBS)

-include $(OBJECTS:.o=.d) $(MB_OBJECTS:.o=.d)

clean:
	rm -rf build

.PHONY: all debug release clean

# Run build/ipv6-setting-test/test.exe --write, then --read in a new process.
test-build: $(MBED_A) $(NOTIFY_A) $(SHARED_OBJECTS)
	mkdir -p build/ipv6-setting-test
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) -std=c++20 -static-libgcc -static-libstdc++ -o build/ipv6-setting-test/test.exe tests/ipv6_setting_test.cpp $(filter %.cpp,$(filter-out src/main.cpp,$(t-units))) $(SHARED_OBJECTS) $(CLIENT_LIBS)

.PHONY: test-build

token-test-build: $(MBED_A) $(NOTIFY_A) $(SHARED_OBJECTS)
	mkdir -p build/token-recovery-test
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) -std=c++20 -static-libgcc -static-libstdc++ -o build/token-recovery-test/test.exe tests/token_recovery_test.cpp $(filter %.cpp,$(filter-out src/main.cpp src/ipc_daemon.cpp,$(t-units))) $(SHARED_OBJECTS) $(CLIENT_LIBS)

.PHONY: token-test-build

certificate-test-build: $(MBED_A) $(NOTIFY_A) $(SHARED_OBJECTS)
	mkdir -p build/certificate-state-test
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) -std=c++20 -static-libgcc -static-libstdc++ -o build/certificate-state-test/test.exe tests/certificate_state_test.cpp $(filter %.cpp,$(filter-out src/main.cpp src/ipc_daemon.cpp,$(t-units))) $(SHARED_OBJECTS) $(CLIENT_LIBS)
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) -std=c++20 -static-libgcc -static-libstdc++ -o build/certificate-probe-test.exe tests/certificate_probe_test.cpp src/certificate_pin.cpp src/tls_client.cpp $(SHARED_OBJECTS) $(CLIENT_LIBS)

.PHONY: certificate-test-build

ini-test-build: $(MBED_A) $(NOTIFY_A) $(SHARED_OBJECTS)
	mkdir -p build/ini-sections-test
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) -std=c++20 -static-libgcc -static-libstdc++ -o build/ini-sections-test/test.exe tests/ini_sections_test.cpp $(filter %.cpp,$(filter-out src/main.cpp src/ipc_daemon.cpp,$(t-units))) $(SHARED_OBJECTS) $(CLIENT_LIBS)

.PHONY: ini-test-build

permissions-test-build:
	mkdir -p build/secret-permissions-test
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) -std=c++20 -static-libgcc -static-libstdc++ -o build/secret-permissions-test/test.exe tests/secret_permissions_test.cpp -ladvapi32

.PHONY: permissions-test-build

notifications-test-build: $(MBED_A) $(NOTIFY_A) $(SHARED_OBJECTS)
	mkdir -p build/notifications-test
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) -std=c++20 -static-libgcc -static-libstdc++ -o build/notifications-test/test.exe tests/notifications_test.cpp $(filter %.cpp,$(filter-out src/main.cpp,$(t-units))) $(SHARED_OBJECTS) $(CLIENT_LIBS)

.PHONY: notifications-test-build

whitelist-test-build: $(MBED_A) $(NOTIFY_A) $(SHARED_OBJECTS)
	mkdir -p build/whitelist-test
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) -std=c++20 -static-libgcc -static-libstdc++ -o "build/whitelist-test/BigBrother Client.exe" tests/whitelist_test.cpp $(filter %.cpp,$(filter-out src/main.cpp,$(t-units))) $(SHARED_OBJECTS) $(CLIENT_LIBS)

.PHONY: whitelist-test-build

policy-test-build: $(MBED_A) $(NOTIFY_A) $(SHARED_OBJECTS)
	mkdir -p build/policy-test
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) -std=c++20 -static-libgcc -static-libstdc++ -o build/policy-test/test.exe tests/policy_test.cpp $(filter %.cpp,$(filter-out src/main.cpp src/ipc_daemon.cpp,$(t-units))) $(SHARED_OBJECTS) $(CLIENT_LIBS)

.PHONY: policy-test-build

whitelist-sync-test-build: $(MBED_A) $(NOTIFY_A) $(SHARED_OBJECTS)
	mkdir -p build/whitelist-sync-test
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) -std=c++20 -static-libgcc -static-libstdc++ -o build/whitelist-sync-test/test.exe tests/whitelist_sync_test.cpp $(filter %.cpp,$(filter-out src/main.cpp src/ipc_daemon.cpp,$(t-units))) $(SHARED_OBJECTS) $(CLIENT_LIBS)

.PHONY: whitelist-sync-test-build

hub-test-build: $(MBED_A) $(NOTIFY_A) $(SHARED_OBJECTS)
	mkdir -p build/hub-test
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) -std=c++20 -static-libgcc -static-libstdc++ -o build/hub-test/test.exe tests/hub_test.cpp $(filter %.cpp,$(filter-out src/main.cpp,$(t-units))) $(SHARED_OBJECTS) $(CLIENT_LIBS)

.PHONY: hub-test-build

lifecycle-test-build: debug
	mkdir -p build/service-lifecycle-test
	cp "build/debug/BigBrother Client Daemon.exe" build/service-lifecycle-test/
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) -std=c++20 -static -o "build/service-lifecycle-test/BigBrother Client.exe" tests/service_lifecycle_test.cpp -ladvapi32
	mkdir -p build/service-lifecycle-test/GUI
	cp "build/service-lifecycle-test/BigBrother Client.exe" build/service-lifecycle-test/GUI/

.PHONY: lifecycle-test-build

regression-build: test-build token-test-build certificate-test-build ini-test-build permissions-test-build notifications-test-build whitelist-test-build policy-test-build whitelist-sync-test-build hub-test-build

.PHONY: regression-build
