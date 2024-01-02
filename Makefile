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
datadir = $(prefix)/share

exec_prefix = $(prefix)
bindir = $(exec_prefix)/bin
zshcompdir = $(datadir)/zsh/site-functions

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

LD_SET_SONAME = -Wl
INSTALL_FLAGS = -D -m

-include config.mak

SHARED_LIBS = $(LDSO_PATHNAME)
ALL_LIBS = $(SHARED_LIBS)
PXCHAINS = proxychains4
ALL_TOOLS = $(PXCHAINS)

CCFLAGS+=$(USER_CFLAGS) $(OS_CFLAGS)
LDFLAGS+=$(USER_LDFLAGS) $(OS_LDFLAGS)
CXXFLAGS+=$(CCFLAGS) $(USER_CFLAGS) $(OS_CFLAGS)
CFLAGS_MAIN=-DLIB_DIR=\"$(libdir)\" -DINSTALL_PREFIX=\"$(prefix)\" -DDLL_NAME=\"$(LDSO_PATHNAME)\" -DSYSCONFDIR=\"$(confdir)\"

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
	LDSO_PATHNAME = libproxychains.$(LDSO_SUFFIX)
    LDSO_SUFFIX = so.4
endif
ifeq ($(UNAME_S),Darwin)
	LDSO_PATHNAME = libproxychains4.$(LDSO_SUFFIX)
    LDSO_SUFFIX = dylib
endif


all: $(ALL_LIBS) $(ALL_TOOLS)

#install: $(ALL_LIBS:lib/%=$(DESTDIR)$(libdir)/%) $(DESTDIR)$(LDSO_PATHNAME)
install: install-exec install-config install-zsh-completion

install-exec:
	install -d $(DESTDIR)/$(bindir) $(DESTDIR)/$(libdir) $(DESTDIR)/$(confdir) $(DESTDIR)/$(includedir)
	install $(INSTALL_FLAGS) 755 $(ALL_TOOLS) src/proxychains src/proxyresolv $(DESTDIR)/$(bindir)/
	install $(INSTALL_FLAGS) 644 $(ALL_LIBS) $(DESTDIR)/$(libdir)/

install-config:
	install -d $(DESTDIR)/$(confdir)
	install $(INSTALL_FLAGS) 644 src/proxychains.conf $(DESTDIR)/$(confdir)/

install-zsh-completion:
	install -d $(DESTDIR)/$(zshcompdir)
	install $(INSTALL_FLAGS) 644 completions/zsh/_proxychains4 -t $(DESTDIR)/$(zshcompdir)

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

.PHONY: all clean install install-exec install-config install-zsh-completion

-include $(DEPS)
