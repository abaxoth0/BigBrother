BB_BUILD_JOBS := $(shell nproc)

ifeq ($(MAKELEVEL),0)
ifeq ($(filter -j% --jobs%,$(MAKEFLAGS)),)
MAKEFLAGS += -j$(BB_BUILD_JOBS)
endif
endif
