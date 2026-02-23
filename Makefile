CC = x86_64-w64-mingw32-gcc
CFLAGS = -O2 -Wall -mwindows -I$(HOME)/proj/BigBrother/include/third-party/npcap-sdk/Include
LDFLAGS = -L$(HOME)/proj/BigBrother/include/third-party/npcap-sdk/Lib/x64 -lwpcap -lpacket -lws2_32 -static
t-units = src/firewall.c src/common.c

all: firewall-service.exe

firewall-service.exe: $(t-units)
	mkdir -p build
	$(CC) $(CFLAGS) -o build/$@ $(t-units) $(LDFLAGS)

clean:
	rm -f *.exe

.PHONY: all clean
