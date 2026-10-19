include ../common/version.mk
include ../common/build.mk

CC = x86_64-w64-mingw32-gcc
WINDRES = x86_64-w64-mingw32-windres
ICON_OBJ = build/app-icon.o

COMMON_FLAGS = $(BB_VERSION_FLAGS) -Wall -Wno-format-truncation -mconsole \
	-I$(CURDIR)/include/third-party/WinDivert-2.2.2-A/include \
	-I$(CURDIR)/../common \
	-I$(CURDIR)/../lib/c

DEBUG_FLAGS = -DDEBUG -g3 -O0
RELEASE_FLAGS = -DNDEBUG -O2

LDFLAGS = -L$(CURDIR)/include/third-party/WinDivert-2.2.2-A/x64 -lWinDivert -lws2_32 -ladvapi32 -static

t-units = src/firewall.c src/common.c src/dns.c src/allowlist.c src/ipc.c \
	$(CURDIR)/../common/encoding/encoding.c \
	$(CURDIR)/../common/log/log.c
OBJECTS = $(addprefix build/$(BUILD_MODE)/,$(patsubst %.c,%.o,$(patsubst $(CURDIR)/../%,%,$(t-units))))

FW_NAME = BigBrother Firewall
FW_EXE = build/$(BUILD_MODE)/BigBrother\ Firewall.exe
all: $(BUILD_MODE)

debug: CFLAGS = $(COMMON_FLAGS) $(DEBUG_FLAGS)
debug: $(FW_EXE)
	cp "$<" "build/$(FW_NAME).exe"

release: CFLAGS = $(COMMON_FLAGS) $(RELEASE_FLAGS)
release: $(FW_EXE)
	cp "$<" "build/$(FW_NAME).exe"

build/$(BUILD_MODE)/src/%.o: src/%.c $(BB_VERSION_DEPS) $(BB_BUILD_DEPS)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

build/$(BUILD_MODE)/common/%.o: ../common/%.c $(BB_VERSION_DEPS) $(BB_BUILD_DEPS)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(ICON_OBJ): $(BB_VERSION_DEPS) app.rc ../assets/icons/BigBrother_Firewall.ico
	mkdir -p build
	$(WINDRES) $(BB_RESOURCE_VERSION_FLAGS) -i app.rc -o $(ICON_OBJ)

$(FW_EXE): $(BB_VERSION_DEPS) $(OBJECTS) $(ICON_OBJ)
	mkdir -p build
	$(CC) $(CFLAGS) -o "$@" $(OBJECTS) $(ICON_OBJ) $(LDFLAGS)

-include $(OBJECTS:.o=.d)

clean:
	rm -rf build

.PHONY: all debug release clean

# Run build/*_test.exe on Windows (or with Wine on Linux).
test-build:
	mkdir -p build
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/allowlist_test.exe tests/allowlist_test.c src/allowlist.c src/dns.c src/common.c -lws2_32 -static
	$(CC) $(COMMON_FLAGS) $(DEBUG_FLAGS) -o build/ipv6_block_test.exe tests/ipv6_block_test.c src/allowlist.c src/dns.c src/common.c $(CURDIR)/../common/encoding/encoding.c -lws2_32 -ladvapi32 -static

.PHONY: test-build
