CC = x86_64-w64-mingw32-gcc
CFLAGS = -O2 -Wall -mwindows -I$(HOME)/proj/BigBrother/include/third-party/npcap-sdk/Include
LDFLAGS = -L$(HOME)/proj/BigBrother/include/third-party/npcap-sdk/Lib/x64 -lwpcap -lpacket -lws2_32 -static

all: firewall-service.exe

firewall-service.exe: src/firewall.c
	mkdir -p build
	$(CC) $(CFLAGS) -o build/$@ $< $(LDFLAGS)

clean:
	rm -f *.exe

.PHONY: all clean
