TOPDIR = $(CURDIR)/..
null =

out_bindir  = _out/bin
out_sbindir = _out/sbin

UEVENTD_WITH_LUA ?= 1

CFLAGS = $(warning_CFLAGS) \
	-D_FORTIFY_SOURCE=2 \
	-DPACKAGE=\"$@\" -DVERSION=\"$(VERSION)\"

CFLAGS += -I.
CFLAGS += -Os

bin_PROGS = \
	$(out_bindir)/halt \
	$(out_bindir)/replace \
	$(out_bindir)/environ

sbin_PROGS = \
	$(out_sbindir)/ueventd \
	$(out_sbindir)/monotonic-timestamp

ueventd_SRCS = \
	ueventd.events.c \
	ueventd.events.sh.c \
	ueventd.logging.c \
	ueventd.main.c \
	ueventd.memory.c \
	ueventd.pidfile.c \
	ueventd.pool.c \
	ueventd.queue.c \
	$(null)

ueventd_LIBS =

ifneq "$(UEVENTD_WITH_LUA)" ''
	ueventd_SRCS += ueventd.events.lua.c
	ueventd_LIBS += $(shell pkg-config --libs lua)
	CFLAGS       += $(shell pkg-config --cflags lua) -DWITH_LUA
endif

monotonic_timestamp_SRCS = monotonic-timestamp.c
halt_SRCS = halt.c
replace_SRCS = replace.c
environ_SRCS = environ.c

DEPS = $(call get_depends,$(bin_PROGS) $(sbin_PROGS),)
OBJS = $(call get_objects,$(bin_PROGS) $(sbin_PROGS),)

GARBAGE   = _out $(out_bindir) $(out_sbindir) $(DEPS) $(OBJS)
C_SOURCES = *.c *.h

all: $(bin_PROGS) $(sbin_PROGS)

include $(TOPDIR)/Makefile.common

prepare:
	$(MKDIR_P) -- $(out_bindir)
	$(MKDIR_P) -- $(out_sbindir)

$(out_sbindir)/ueventd: prepare $(call get_objects,ueventd)
	$(LINK) $(realpath $^) -o $@ $(ueventd_LIBS)

$(out_sbindir)/monotonic-timestamp: prepare $(call get_objects,monotonic_timestamp)
	$(LINK) $(realpath $^) -o $@

$(out_bindir)/halt: prepare $(call get_objects,halt)
	$(LINK) $(realpath $^) -o $@

$(out_bindir)/replace:  prepare $(call get_objects,replace)
	$(LINK) $(realpath $^) -o $@

$(out_bindir)/environ: prepare $(call get_objects,environ)
	$(LINK) $(realpath $^) -o $@

install: $(bin_PROGS) $(sbin_PROGS)
	$(MKDIR_P) -- $(DESTDIR)$(helper_bindir) $(DESTDIR)$(helper_sbindir)
	$(STRIP) $^
	$(INSTALL) -p -m755 $(bin_PROGS)  $(DESTDIR)$(helper_bindir)/
	$(INSTALL) -p -m755 $(sbin_PROGS) $(DESTDIR)$(helper_sbindir)/
	ln -s -- environ  $(DESTDIR)$(helper_bindir)/showenv
