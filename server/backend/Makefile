include ../../common/version.mk
include ../../common/build.mk

all: $(BUILD_MODE)

GO_BUILD_FLAGS = $(if $(filter debug,$(BUILD_MODE)),-tags debug -gcflags='all=-N -l',-trimpath)
GO_LINK_FLAGS = -X main.productVersion=$(BB_VERSION) $(if $(filter release,$(BUILD_MODE)),-s -w)

debug release: build

resources: cmd/app_windows_amd64.syso

cmd/app_windows_amd64.syso: cmd/app.rc $(BB_VERSION_DEPS) ../../assets/icons/BigBrother_Server_Backend.ico
	cd cmd && x86_64-w64-mingw32-windres $(BB_RESOURCE_VERSION_FLAGS) -i app.rc -o app_windows_amd64.syso

build: resources
	mkdir -p build
	CGO_ENABLED=1 CC=clang GOOS=windows GOARCH=amd64 GOMAXPROCS=$(BB_BUILD_JOBS) go build -p $(BB_BUILD_JOBS) $(GO_BUILD_FLAGS) -ldflags '$(GO_LINK_FLAGS)' -o 'build/BigBrother Server Daemon.exe' ./cmd

.PHONY: all debug release resources build
