CXX = x86_64-w64-mingw32-g++
AR = x86_64-w64-mingw32-ar
WINDRES = x86_64-w64-mingw32-windres
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O2 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00
LIBS = -lwtsapi32 -luserenv -lshell32 -lshlwapi -ladvapi32 -lole32 -lpropsys -lruntimeobject -luuid

all: build/BigBrother\ Notifications.exe build/libbbnotify.a

build/icon.o: app.rc ../../assets/icons/BigBrother_Client_Backend.ico ../../assets/icons/BigBrother_Server_Backend.ico
	mkdir -p build
	$(WINDRES) -i app.rc -o build/icon.o

build/BigBrother\ Notifications.exe: main.cpp diagnostics.cpp command_line.h toast.cpp toast.h registration.cpp app.h transport.h build/icon.o
	$(CXX) $(CXXFLAGS) -static -municode -mwindows -o "$@" main.cpp diagnostics.cpp toast.cpp registration.cpp build/icon.o $(LIBS)

build/notify.o: notify.cpp notify.h transport.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -c notify.cpp -o $@

build/libbbnotify.a: build/notify.o
	$(AR) rcs $@ $<

test-build: tests.cpp delivery_test.cpp diagnostics.cpp command_line.h toast.cpp toast.h registration.cpp transport.h app.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -static -mconsole -o build/tests.exe tests.cpp delivery_test.cpp diagnostics.cpp toast.cpp registration.cpp $(LIBS)

.PHONY: all test-build
