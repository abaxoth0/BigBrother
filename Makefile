CC = x86_64-w64-mingw32-gcc
CFLAGS = -O2 -Wall -mwindows -I$(HOME)/proj/BigBrother/npcap-sdk/Include
LDFLAGS = -L$(HOME)/proj/BigBrother/npcap-sdk/Lib/x64 -lwpcap -lpacket -lws2_32 -static


all: firewall-service.exe

firewall-service.exe: firewall.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f *.exe

.PHONY: all clean
