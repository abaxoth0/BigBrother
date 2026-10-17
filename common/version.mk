BB_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
BB_VERSION_FILE := $(BB_ROOT)/VERSION
BB_VERSION := $(shell python3 "$(BB_ROOT)/scripts/version.py")
ifeq ($(BB_VERSION),)
$(error Cannot read the project VERSION)
endif
bb_comma := ,
BB_VERSION_FLAGS := -DBB_VERSION='"$(BB_VERSION)"' -DBB_VERSION_TUPLE=$(subst .,$(bb_comma),$(BB_VERSION)),0
BB_RESOURCE_VERSION_FLAGS := -DBB_VERSION='\"$(BB_VERSION)\"' -DBB_VERSION_TUPLE=$(subst .,$(bb_comma),$(BB_VERSION)),0
BB_VERSION_DEPS := $(BB_VERSION_FILE) $(BB_ROOT)/common/version.h $(BB_ROOT)/common/version-resource.rc $(BB_ROOT)/common/version.mk
