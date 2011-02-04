CC      = gcc
CFLAGS  ?= -O2 -pipe -Wall -Wextra -Wno-variadic-macros -Wno-strict-aliasing
STRIP   = strip
INSTALL = install

LUA_VERSION = 5.1
PREFIX      = /usr/local
LIBDIR      = $(PREFIX)/lib/lua/$(LUA_VERSION)/lem

programs = dbus.so
scripts  = dbus.lua

ifdef NDEBUG
DEFINES+=-DNDEBUG
endif

.PHONY: all strip install clean
.PRECIOUS: %.o

all: $(programs)

%.o: %.c
	@echo '  CC $@'
	@$(CC) $(CFLAGS) -fPIC -nostartfiles $(DEFINES) -c $< -o $@

dbus.so: override CFLAGS += $(shell pkg-config --cflags dbus-1)
dbus.so: add.o push.o parse.o dbus.o
	@echo '  LD $@'
	@$(CC) $(shell pkg-config --libs dbus-1) -lexpat -shared $(LDFLAGS) $^ -o $@

allinone:
	$(CC) $(CFLAGS) -fPIC $(DEFINES) -DALLINONE -shared $(shell pkg-config --cflags --libs dbus-1) -lexpat $(LDFLAGS) dbus.c -o dbus.so

%-strip: %
	@echo '  STRIP $<'
	@$(STRIP) $<

strip: $(programs:%=%-strip)

libdir-install:
	@echo "  INSTALL -d $(LIBDIR)"
	@$(INSTALL) -d $(DESTDIR)$(LIBDIR)/dbus

dbus.so-install: dbus.so libdir-install
	@echo "  INSTALL $<"
	@$(INSTALL) $< $(DESTDIR)$(LIBDIR)/dbus/core.so

%.lua-install: %.lua libdir-install
	@echo "  INSTALL $<"
	@$(INSTALL) $< $(DESTDIR)$(LIBDIR)/$<

install: $(programs:%=%-install) $(scripts:%=%-install)

clean:
	rm -f $(programs) *.o *.c~ *.h~
