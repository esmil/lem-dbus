CC         = gcc
CFLAGS    ?= -O2 -pipe -Wall -Wextra -Wno-variadic-macros -Wno-strict-aliasing
PKGCONFIG  = pkg-config
STRIP      = strip
INSTALL    = install
UNAME      = uname

OS         = $(shell $(UNAME))
CFLAGS    += $(shell $(PKGCONFIG) --cflags lem)
LUA_PATH   = $(shell $(PKGCONFIG) --variable=path lem)
LUA_CPATH  = $(shell $(PKGCONFIG) --variable=cpath lem)

ifeq ($(OS),Darwin)
SHARED       = -dynamiclib -Wl,-undefined,dynamic_lookup
else
SHARED       = -shared
endif

programs = dbus.so
scripts  = dbus.lua

ifdef NDEBUG
CFLAGS += -DNDEBUG
endif

.PHONY: all strip install clean
.PRECIOUS: %.o

all: $(programs)

%.o: %.c
	@echo '  CC $@'
	@$(CC) $(CFLAGS) -fPIC -nostartfiles -c $< -o $@

dbus.so: CFLAGS += $(shell pkg-config --cflags dbus-1)
dbus.so: add.o push.o parse.o dbus.o
	@echo '  LD $@'
	@$(CC) -lexpat $(shell pkg-config --libs dbus-1) $(SHARED) $(LDFLAGS) $^ -o $@

allinone:
	$(CC) $(CFLAGS) -fPIC -DALLINONE $(SHARED) $(shell pkg-config --cflags --libs dbus-1) -lexpat $(LDFLAGS) dbus.c -o dbus.so

%-strip: %
	@echo '  STRIP $<'
	@$(STRIP) $<

strip: $(programs:%=%-strip)

path-install:
	@echo "  INSTALL -d $(LUA_PATH)/lem"
	@$(INSTALL) -d $(DESTDIR)$(LUA_PATH)/lem

%.lua-install: %.lua path-install
	@echo "  INSTALL $<"
	@$(INSTALL) -m644 $< $(DESTDIR)$(LUA_PATH)/lem/$<

cpath-install:
	@echo "  INSTALL -d $(LUA_CPATH)/lem/dbus"
	@$(INSTALL) -d $(DESTDIR)$(LUA_CPATH)/lem/dbus

dbus.so-install: dbus.so cpath-install
	@echo "  INSTALL $<"
	@$(INSTALL) $< $(DESTDIR)$(LUA_CPATH)/lem/dbus/core.so

install: $(programs:%=%-install) $(scripts:%=%-install)

clean:
	rm -f $(programs) *.o *.c~ *.h~
