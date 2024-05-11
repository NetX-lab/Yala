# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2022 NVIDIA Corporation

# binary name
APP = accbench

# all source are stored in SRCS-y
SRCS-y := src/*.c src/*/*.c

PKGCONF ?= pkg-config

# Build using pkg-config variables if possible
ifneq ($(shell $(PKGCONF) --exists libdpdk && echo 0),0)
$(error "no installation of DPDK found")
endif

all: static
.PHONY: shared static
shared: build/$(APP)-shared
	ln -sf $(APP)-shared build/$(APP)
static: build/$(APP)-static
	ln -sf $(APP)-static build/$(APP)

LDFLAGS += -lhs -lpcap -lstdc++ -lrxp_compiler
CFLAGS += -I/usr/local/include/hs
CFLAGS += -I/usr/include/hs

PC_FILE := $(shell $(PKGCONF) --path libdpdk 2>/dev/null)
# CFLAGS += -O3 $(shell $(PKGCONF) --cflags libdpdk)
CFLAGS += -O3 -g $(shell $(PKGCONF) --cflags libdpdk)
LDFLAGS_SHARED = $(shell $(PKGCONF) --libs libdpdk)
LDFLAGS_STATIC = $(shell $(PKGCONF) --static --libs libdpdk)

# DOCA IS ONLY INCLUDED DYNAMICALLY FOR NOW
CFLAGS += -I/opt/mellanox/doca/include
DOCA_LIB_DIR := $(shell $(PKGCONF) --libs-only-L doca-regex)
#LDFLAGS_SHARED += $(shell $(PKGCONF) --libs doca-regex)
#LDFLAGS_SHARED += $(shell $(PKGCONF) --libs doca-common)
# When statically linking against DPDK you should also link statically with DOCA
# Temporary workaround for static linkage to doca, manually call out the required libs until pkgconf definitions are fixed
#LDFLAGS_STATIC += $(DOCA_LIB_DIR) -l:libdoca_regex.a -l:libdoca_common.a -lstdc++ -lbsd -ljson-c
LDFLAGS_STATIC += -lstdc++ -lbsd -ljson-c

CFLAGS += -DALLOW_EXPERIMENTAL_API
CFLAGS += -DDOCA_ALLOW_EXPERIMENTAL_API
CFLAGS += -DUSE_HYPERSCAN

GIT_VERSION := "$(shell git rev-parse --short HEAD || echo "release")"
CFLAGS += -DGIT_SHA=\"$(GIT_VERSION)\"

build/$(APP)-shared: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_SHARED)

build/$(APP)-static: $(SRCS-y) Makefile $(PC_FILE) | build
	$(CC) $(CFLAGS) $(SRCS-y) -o $@ $(LDFLAGS) $(LDFLAGS_STATIC)

build:
	@mkdir -p $@

.PHONY: clean
clean:
	rm -f build/$(APP) build/$(APP)-static build/$(APP)-shared
	test -d build && rmdir -p build || true
