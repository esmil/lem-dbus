CC         = gcc -std=gnu99
CFLAGS    ?= -O2 -pipe -Wall -Wextra
PKG_CONFIG = pkg-config
STRIP      = strip
INSTALL    = install
UNAME      = uname

OS         = $(shell $(UNAME))
CFLAGS    += $(shell $(PKG_CONFIG) --cflags lem)
lmoddir    = $(shell $(PKG_CONFIG) --variable=INSTALL_LMOD lem)
cmoddir    = $(shell $(PKG_CONFIG) --variable=INSTALL_CMOD lem)

ifeq ($(OS),Darwin)
SHARED     = -dynamiclib -Wl,-undefined,dynamic_lookup
STRIP     += -x
else
SHARED     = -shared
endif

llibs = lem/dbus.lua
clibs = lem/dbus/core.so

ifdef V
E=@\#
Q=
else
E=@echo
Q=@
endif

.PHONY: all debug amalg strip install clean

all: CFLAGS += -DNDEBUG
all: $(clibs)

debug: $(clibs)

%.o: %.c
	$E '  CC    $@'
	$Q$(CC) $(CFLAGS) -fPIC -nostartfiles -c $< -o $@

lem/dbus/core.so: CFLAGS += $(shell $(PKG_CONFIG) --cflags dbus-1)
lem/dbus/core.so: LIBS += -lexpat $(shell $(PKG_CONFIG) --libs dbus-1)
lem/dbus/core.so: lem/dbus/add.o lem/dbus/push.o lem/dbus/parse.o lem/dbus/core.o
	$E '  LD    $@'
	$Q$(CC) $(SHARED) $^ -o $@ $(LDFLAGS) $(LIBS)

amalg: CFLAGS += -DNDEBUG -DAMALG $(shell $(PKG_CONFIG) --cflags dbus-1)
amalg: LIBS += -lexpat $(shell $(PKG_CONFIG) --libs dbus-1)
amalg: lem/dbus/core.c lem/dbus/add.c lem/dbus/push.c lem/dbus/parse.c
	$E '  CCLD  $@'
	$Q$(CC) $(CFLAGS) -fPIC -nostartfiles $(SHARED) $< -o lem/dbus/core.so $(LDFLAGS) $(LIBS)

%-strip: %
	$E '  STRIP $<'
	$Q$(STRIP) $<

strip: $(clibs:%=%-strip)

$(DESTDIR)$(lmoddir)/% $(DESTDIR)$(cmoddir)/%: %
	$E '  INSTALL $@'
	$Q$(INSTALL) -d $(dir $@)
	$Q$(INSTALL) -m 644 $< $@

install: \
	$(llibs:%=$(DESTDIR)$(lmoddir)/%) \
	$(clibs:%=$(DESTDIR)$(cmoddir)/%)

clean:
	rm -f $(clibs) lem/dbus/*.o
