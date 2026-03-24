PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

CFLAGS ?= -Wall -Wextra -O2
LDFLAGS ?=
PKG_CFLAGS := $(shell pkg-config --cflags libevdev)
PKG_LIBS := $(shell pkg-config --libs libevdev)

das-debounce: das-debounce.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $@ $< $(LDFLAGS) $(PKG_LIBS)

install: das-debounce
	install -Dm755 das-debounce $(DESTDIR)$(BINDIR)/das-debounce
	install -Dm644 das-debounce.service $(DESTDIR)/usr/lib/systemd/system/das-debounce.service
	install -Dm644 90-das-keyboard.rules $(DESTDIR)/usr/lib/udev/rules.d/90-das-keyboard.rules

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/das-debounce
	rm -f $(DESTDIR)/usr/lib/systemd/system/das-debounce.service
	rm -f $(DESTDIR)/usr/lib/udev/rules.d/90-das-keyboard.rules

clean:
	rm -f das-debounce

.PHONY: install uninstall clean
