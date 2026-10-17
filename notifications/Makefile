include ../common/version.mk

CXX = x86_64-w64-mingw32-g++
AR = x86_64-w64-mingw32-ar
WINDRES = x86_64-w64-mingw32-windres
CXXFLAGS = -std=c++17 -Wall -Wextra -Werror -O2 -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00
CPPFLAGS += -Iinclude -I../common $(BB_VERSION_FLAGS)
LIBS = -lwtsapi32 -luserenv -lshell32 -lshlwapi -ladvapi32 -lole32 -lpropsys -lruntimeobject -luuid
APP_SOURCES = src/main.cpp src/diagnostics.cpp src/toast.cpp src/registration.cpp
APP_HEADERS = $(wildcard include/*.h)
TEST_SOURCES = tests/main.cpp tests/delivery_test.cpp src/diagnostics.cpp src/toast.cpp src/registration.cpp

all: build/BigBrother\ Notifications.exe build/libbbnotify.a

build/icon.o: $(BB_VERSION_DEPS) resources/app.rc ../assets/icons/BigBrother_Client_Backend.ico ../assets/icons/BigBrother_Server_Backend.ico
	mkdir -p build
	$(WINDRES) $(BB_RESOURCE_VERSION_FLAGS) -I../assets/icons -i resources/app.rc -o build/icon.o

build/BigBrother\ Notifications.exe: $(APP_SOURCES) $(APP_HEADERS) $(BB_VERSION_DEPS) build/icon.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -static -municode -mwindows -o "$@" $(APP_SOURCES) build/icon.o $(LIBS)

build/notify.o: src/notify.cpp ../common/notify.h include/transport.h
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c src/notify.cpp -o $@

build/libbbnotify.a: build/notify.o
	$(AR) rcs $@ $<

test-build: build/tests.exe

build/tests.exe: $(TEST_SOURCES) $(APP_HEADERS)
	mkdir -p build
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -static -mconsole -o $@ $(TEST_SOURCES) $(LIBS)

.PHONY: all test-build
