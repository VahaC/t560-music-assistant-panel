PREFIX ?= /usr
CC ?= cc
PKG_CONFIG ?= pkg-config

PACKAGES = gtk+-3.0 libsoup-3.0 json-glib-1.0
CFLAGS ?= -Os
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic $(shell $(PKG_CONFIG) --cflags $(PACKAGES))
LDLIBS += $(shell $(PKG_CONFIG) --libs $(PACKAGES))

.PHONY: all clean install

all: t560-panel

t560-panel: src/main.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f t560-panel

install: t560-panel
	install -Dm755 t560-panel "$(DESTDIR)$(PREFIX)/bin/t560-panel"
	install -Dm755 scripts/t560-panel-watchdog "$(DESTDIR)$(PREFIX)/bin/t560-panel-watchdog"
	install -Dm755 scripts/t560-open-panel "$(DESTDIR)$(PREFIX)/bin/t560-open-panel"
	install -Dm644 config/config.ini.example "$(DESTDIR)/etc/t560-music-panel/config.ini.example"
	install -Dm644 data/t560-music-panel.desktop "$(DESTDIR)$(PREFIX)/share/applications/t560-music-panel.desktop"
