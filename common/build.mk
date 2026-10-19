BB_BUILD_JOBS := $(shell nproc)
BB_BUILD_DEPS := Makefile $(BB_ROOT)/common/build.mk
BUILD_MODE ?= $(if $(filter release,$(MAKECMDGOALS)),release,debug)
ifneq ($(BUILD_MODE),debug)
ifneq ($(BUILD_MODE),release)
$(error BUILD_MODE must be debug or release)
endif
endif
ifneq ($(filter debug,$(MAKECMDGOALS)),)
ifneq ($(BUILD_MODE),debug)
$(error The debug target requires BUILD_MODE=debug)
endif
ifneq ($(filter release,$(MAKECMDGOALS)),)
$(error Build debug and release separately)
endif
endif
ifneq ($(filter release,$(MAKECMDGOALS)),)
ifneq ($(BUILD_MODE),release)
$(error The release target requires BUILD_MODE=release)
endif
endif

ifeq ($(MAKELEVEL),0)
ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(BB_BUILD_JOBS)
endif
endif
