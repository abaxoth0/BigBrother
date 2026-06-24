CC = x86_64-w64-mingw32-gcc

COMMON_FLAGS = -Wall -Wno-format-truncation -mconsole \
	-I$(CURDIR)/include/third-party/WinDivert-2.2.2-A/include \
	-I$(CURDIR)/../common

DEBUG_FLAGS = -DDEBUG -g3 -O0
RELEASE_FLAGS = -O2

LDFLAGS = -L$(CURDIR)/include/third-party/WinDivert-2.2.2-A/x64 -lWinDivert -lws2_32 -lwevtapi -static

t-units = src/firewall.c src/common.c src/dns.c src/dns_etw.c src/allowlist.c src/ipc.c \
	$(CURDIR)/../common/encoding/encoding.c \
	$(CURDIR)/../common/log/log.c

FW_NAME = BigBrother Firewall
all: debug

debug: CFLAGS = $(COMMON_FLAGS) $(DEBUG_FLAGS)
debug: $(FW_NAME)

release: CFLAGS = $(COMMON_FLAGS) $(RELEASE_FLAGS)
release: $(FW_NAME)

$(FW_NAME): $(t-units)
	mkdir -p build
	$(CC) $(CFLAGS) -o "build/$(FW_NAME).exe" $(t-units) $(LDFLAGS)

clean:
	rm -rf build

.PHONY: all debug release clean
