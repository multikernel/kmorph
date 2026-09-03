CC      ?= gcc
CFLAGS  ?= -O2 -g
LDFLAGS ?=
PREFIX  ?= /usr/local
DESTDIR ?=
INSTALL ?= install

ifeq ($(STATIC),1)
CC      = musl-gcc
LDFLAGS += -static
endif

WARN     = -Wall -Wextra -Werror -Wshadow -Wmissing-prototypes
CPPFLAGS = -D_GNU_SOURCE -Iinclude -Ilib/fdt
ALL_CFLAGS = -std=gnu11 $(CFLAGS) $(CPPFLAGS)

BUILD = build

FDT_SRCS     = $(wildcard lib/fdt/*.c)
LIB_SRCS     = $(wildcard lib/*.c)
KMORPHD_SRCS = $(filter-out kmorphd/main.c,$(wildcard kmorphd/*.c))
KMORPH_SRCS  = $(filter-out kmorph/main.c,$(wildcard kmorph/*.c))
TEST_SRCS    = $(wildcard tests/test_*.c)

FDT_OBJS     = $(FDT_SRCS:%.c=$(BUILD)/%.o)
LIB_OBJS     = $(LIB_SRCS:%.c=$(BUILD)/%.o)
KMORPHD_OBJS = $(KMORPHD_SRCS:%.c=$(BUILD)/%.o)
KMORPH_OBJS  = $(KMORPH_SRCS:%.c=$(BUILD)/%.o)
TEST_BINS    = $(TEST_SRCS:tests/%.c=$(BUILD)/tests/%)

LIBKMORPH = $(BUILD)/libkmorph.a
BIN       = $(BUILD)/bin

all: $(BIN)/kmorphd $(BIN)/kmorph

$(BIN)/kmorphd: $(BUILD)/kmorphd/main.o $(KMORPHD_OBJS) $(LIBKMORPH)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^

$(BIN)/kmorph: $(BUILD)/kmorph/main.o $(KMORPH_OBJS) $(LIBKMORPH)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^

$(LIBKMORPH): $(LIB_OBJS) $(FDT_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/lib/fdt/%.o: lib/fdt/%.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $(WARN) -MMD -MP -c -o $@ $<

$(BUILD)/tests/%: tests/%.c $(KMORPHD_OBJS) $(KMORPH_OBJS) $(LIBKMORPH)
	@mkdir -p $(dir $@)
	$(CC) $(ALL_CFLAGS) $(WARN) -Itests -Ikmorphd -Ikmorph $(LDFLAGS) -o $@ $^

check: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "== $$t"; $$t || exit 1; done
	@echo "all tests passed"

install: all
	$(INSTALL) -d $(DESTDIR)$(PREFIX)/bin $(DESTDIR)/etc/kmorph
	$(INSTALL) -m 0755 $(BIN)/kmorph $(BIN)/kmorphd $(DESTDIR)$(PREFIX)/bin/
	$(INSTALL) -m 0644 kmorph.conf.example $(DESTDIR)/etc/kmorph/

clean:
	rm -rf $(BUILD)

-include $(LIB_OBJS:.o=.d) $(KMORPHD_OBJS:.o=.d) $(KMORPH_OBJS:.o=.d)
-include $(BUILD)/kmorphd/main.d $(BUILD)/kmorph/main.d

.PHONY: all check install clean
