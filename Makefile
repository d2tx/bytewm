# bytewm - retro tiling window manager
include config.mk

APPBINS = bytify bytelaunch bytesnap bytevol bytewm-exit bytekill bytelock bytepass bytefetch bytepick bytemenu

all: bytewm apps bytewdm bytekill bytelock bytepass bytefetch bytepick

bytewm: bytewm.c config.h
	$(CC) $(CFLAGS) -o $@ bytewm.c $(LDFLAGS)

config.h:
	cp config.def.h $@

APPS_CFLAGS = -std=c99 -pedantic -Wall -Os

apps:
	@for app in $(APPBINS); do \
		if [ -f apps/$$app.c ]; then \
			$(CC) $(APPS_CFLAGS) -o apps/$$app apps/$$app.c -lX11; \
		fi; \
	done

bytewdm: apps/bytewdm.c
	$(CC) $(APPS_CFLAGS) -o apps/$@ apps/bytewdm.c -lX11 -lpam

bytekill: apps/bytekill.s
	as -o apps/bytekill.o apps/bytekill.s
	ld -o apps/bytekill apps/bytekill.o

bytelock: apps/bytelock.s
	as -o apps/bytelock.o apps/bytelock.s
	$(CC) -o apps/bytelock apps/bytelock.o -lX11 -lpam

bytepass: apps/bytepass.s
	as -o apps/bytepass.o apps/bytepass.s
	ld -o apps/bytepass apps/bytepass.o

bytefetch: apps/bytefetch.s
	as -o apps/bytefetch.o apps/bytefetch.s
	ld -o apps/bytefetch apps/bytefetch.o

bytepick: apps/bytepick.s
	as -o apps/bytepick.o apps/bytepick.s
	$(CC) -o apps/bytepick apps/bytepick.o -lX11

clean:
	rm -f bytewm apps/bytewdm apps/bytekill.o apps/bytelock.o apps/bytepass.o apps/bytefetch.o apps/bytepick.o
	for app in $(APPBINS); do \
		rm -f apps/$$app 2>/dev/null; \
	done

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	mkdir -p $(DESTDIR)/etc/pam.d
	mkdir -p $(DESTDIR)/usr/share/xsessions
	mkdir -p $(DESTDIR)/usr/lib/systemd/system
	cp -f bytewm $(DESTDIR)$(PREFIX)/bin
	for app in $(APPBINS); do \
		cp -f apps/$$app $(DESTDIR)$(PREFIX)/bin; \
	done
	cp -f apps/bytewdm $(DESTDIR)$(PREFIX)/bin
	[ -f $(DESTDIR)/etc/pam.d/bytewdm ] || cp -f apps/bytewdm.pam $(DESTDIR)/etc/pam.d/bytewdm
	cp -f examples/bytewm.desktop $(DESTDIR)/usr/share/xsessions/bytewm.desktop
	cp -f examples/bytewdm.service $(DESTDIR)/usr/lib/systemd/system/bytewdm.service
	chmod 755 $(DESTDIR)$(PREFIX)/bin/bytewm

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/bytewm
	rm -f $(DESTDIR)$(PREFIX)/bin/bytewdm
	for app in $(APPBINS); do \
		rm -f $(DESTDIR)$(PREFIX)/bin/$$app; \
	done
	rm -f $(DESTDIR)/usr/share/xsessions/bytewm.desktop
	rm -f $(DESTDIR)/usr/lib/systemd/system/bytewdm.service
	rm -f $(DESTDIR)/etc/pam.d/bytewdm

.PHONY: all apps clean install uninstall
