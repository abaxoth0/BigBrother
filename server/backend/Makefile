include ../../common/version.mk

all: build

resources: cmd/app_windows_amd64.syso

cmd/app_windows_amd64.syso: cmd/app.rc $(BB_VERSION_DEPS) ../../assets/icons/BigBrother_Server_Backend.ico
	cd cmd && x86_64-w64-mingw32-windres $(BB_RESOURCE_VERSION_FLAGS) -i app.rc -o app_windows_amd64.syso

build: resources
	mkdir -p build
	CGO_ENABLED=1 CC=clang GOOS=windows GOARCH=amd64 go build -ldflags '-X main.productVersion=$(BB_VERSION)' -o 'build/BigBrother Server Daemon.exe' ./cmd

.PHONY: all resources build
