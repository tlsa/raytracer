#
# SPDX-License-Identifier: ISC
#
# Copyright (c) 2022 Michael Drake
#

PROJECT = raytrace
# For other build variants: `make VARIANT=debug` and `make VARIANT=sanitize`.
VARIANT = release

PREFIX ?= /usr/local

CC ?= gcc
MKDIR ?= mkdir -p
INSTALL ?= install -c
PKG_CONFIG ?= pkg-config

CPPFLAGS += -MMD -MP $(VERSION_FLAGS)
CFLAGS += -Isrc -std=c2x
CFLAGS += -Wall -Wextra -pedantic -Wwrite-strings -Wcast-align \
		-Wpointer-arith -Winit-self -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -Wredundant-decls -Wundef -Wvla \
		-Wdeclaration-after-statement
LDFLAGS += -lm

ifeq ($(VARIANT), debug)
	CFLAGS += -O0 -g
else ifeq ($(VARIANT), sanitize)
	CFLAGS += -O0 -g -fsanitize=address -fsanitize=undefined -fno-sanitize-recover
	LDFLAGS += -fsanitize=address -fsanitize=undefined -fno-sanitize-recover
else
	CFLAGS += -O2 -DNDEBUG
endif

PKG_DEPS := cgif
CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKG_DEPS))
LDFLAGS += $(shell $(PKG_CONFIG) --libs $(PKG_DEPS))

SRC := \
	src/rt.c

BUILDDIR := build/$(VARIANT)

all: $(BUILDDIR)/$(PROJECT)

OBJ := $(patsubst %.c,%.o, $(addprefix $(BUILDDIR)/,$(SRC)))
DEP := $(patsubst %.c,%.d, $(addprefix $(BUILDDIR)/,$(SRC)))

$(OBJ): $(BUILDDIR)/%.o : %.c
	$(Q)$(MKDIR) $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILDDIR)/$(PROJECT): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILDDIR)

-include $(DEP)

.PHONY: all clean
