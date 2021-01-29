#
# Makefile for proxychains (requires GNU make), stolen from musl
#
# Use config.mak to override any of the following variables.
# Do not make changes here.
#

prefix = /usr/local
includedir = $(prefix)/include
libdir = $(prefix)/lib
confdir = $(prefix)/etc

exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin

SRCS = $(sort $(wildcard src/*.c))
OBJS = $(SRCS:.c=.o)
LOBJS = src/core.o src/common.o src/libproxychains.o
DEPS = $(OBJS:.o=.d)

CCFLAGS  = -MD -Wall -O2 -g -std=c99 -D_GNU_SOURCE -pipe -DTHREAD_SAFE -Werror
LDFLAGS = -shared -fPIC
INC     =
PIC     = -fPIC
AR      = $(CROSS_COMPILE)ar
RANLIB  = $(CROSS_COMPILE)ranlib

LDSO_SUFFIX = so
LD_SET_SONAME = -Wl
INSTALL_FLAGS = -D -m

-include config.mak

LDSO_PATHNAME = libproxychains4.$(LDSO_SUFFIX)

SHARED_LIBS = $(LDSO_PATHNAME)
ALL_LIBS = $(SHARED_LIBS)
PXCHAINS = proxychains4
ALL_TOOLS = $(PXCHAINS)

CCFLAGS+=$(USER_CFLAGS) $(OS_CFLAGS)
LDFLAGS+=$(USER_LDFLAGS) $(OS_LDFLAGS)
CXXFLAGS+=$(CCFLAGS) $(USER_CFLAGS) $(OS_CFLAGS)
CFLAGS_MAIN=-DLIB_DIR=\"$(libdir)\" -DINSTALL_PREFIX=\"$(prefix)\" -DDLL_NAME=\"$(LDSO_PATHNAME)\" -DSYSCONFDIR=\"$(confdir)\"

all: $(ALL_LIBS) $(ALL_TOOLS)

#install: $(ALL_LIBS:lib/%=$(DESTDIR)$(libdir)/%) $(DESTDIR)$(LDSO_PATHNAME)
install:
	install -d $(DESTDIR)/$(bindir) $(DESTDIR)/$(libdir) $(DESTDIR)/$(confdir) $(DESTDIR)/$(includedir)
	install $(INSTALL_FLAGS) 755 $(ALL_TOOLS) $(DESTDIR)/$(bindir)/
	install $(INSTALL_FLAGS) 644 $(ALL_LIBS) $(DESTDIR)/$(libdir)/

install-config:
	install -d $(DESTDIR)/$(confdir)
	install $(INSTALL_FLAGS) 644 src/proxychains.conf $(DESTDIR)/$(confdir)/

clean:
	rm -f $(ALL_LIBS)
	rm -f $(ALL_TOOLS)
	rm -f $(OBJS)
	rm -f $(DEPS)

%.o: %.c
	$(CC) $(CCFLAGS) $(CFLAGS_MAIN) $(INC) $(PIC) -c -o $@ $<

$(LDSO_PATHNAME): $(LOBJS)
	$(CC) $(LDFLAGS) $(LD_SET_SONAME)$(LDSO_PATHNAME) -o $@ $(LOBJS)

$(ALL_TOOLS): $(OBJS)
	$(CC) src/main.o src/common.o -o $(PXCHAINS)

.PHONY: all clean install install-config

-include $(DEPS)
