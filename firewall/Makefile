CC = x86_64-w64-mingw32-gcc
CFLAGS = -O2 -Wall -mwindows -I$(HOME)/proj/BigBrother/firewall/include/third-party/WinDivert-2.2.2-A/include -DDEBUG
LDFLAGS = -L$(HOME)/proj/BigBrother/firewall/include/third-party/WinDivert-2.2.2-A/x64 -lWinDivert -lws2_32 -static
t-units = src/firewall.c src/common.c src/dns.c src/allowlist.c

all: firewall-service.exe

firewall-service.exe: $(t-units)
	mkdir -p build
	$(CC) $(CFLAGS) -o build/$@ $(t-units) $(LDFLAGS)

clean:
	rm -f *.exe

.PHONY: all clean
