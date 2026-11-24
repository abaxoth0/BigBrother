include ../../../lib/common/version.mk
include ../../../lib/common/build.mk

CXX = x86_64-w64-mingw32-g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Werror -D_WIN32_WINNT=0x0A00 -I../backend/include -I$(BB_ROOT)/lib/common $(BB_VERSION_FLAGS) $(if $(filter debug,$(BUILD_MODE)),-g3 -O0,-O2)
EXE = build/$(BUILD_MODE)/BigBrother\ Process\ Monitor.exe

all: $(BUILD_MODE)

debug release: $(EXE)
	cp "$<" "build/BigBrother Process Monitor.exe"

$(EXE): src/main.cpp src/process_policy.h ../backend/include/resource.h build/icon.o $(BB_BUILD_DEPS)
	mkdir -p build/$(BUILD_MODE)
	$(CXX) $(CXXFLAGS) -static -municode -o "$@" src/main.cpp build/icon.o -ladvapi32

build/icon.o: app.rc $(BB_VERSION_DEPS) ../../../assets/icons/BigBrother_Client_Backend.ico
	mkdir -p build
	x86_64-w64-mingw32-windres $(BB_RESOURCE_VERSION_FLAGS) -i app.rc -o $@

test-build: build/test.exe build/enforcement-test.exe

build/test.exe: tests/policy.cpp src/process_policy.h $(BB_BUILD_DEPS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) -static -o $@ tests/policy.cpp


build/enforcement-test.exe: tests/enforcement.cpp ../backend/include/resource.h $(BB_BUILD_DEPS)
	mkdir -p build
	$(CXX) $(CXXFLAGS) -static -municode -o $@ tests/enforcement.cpp -ladvapi32

.PHONY: all debug release test-build
