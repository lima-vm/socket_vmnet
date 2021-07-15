PREFIX ?= /usr/local

CFLAGS ?= -O3

VERSION ?= $(shell git describe --match 'v[0-9]*' --dirty='.m' --always --tags)
CFLAGS += -DVERSION=\"$(VERSION)\"

LDFLAGS += -lvdeplug -framework vmnet

all: vde_vmnet

OBJS = $(patsubst %.c, %.o, $(wildcard *.c))

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

vde_vmnet: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $(OBJS)

install.bin: vde_vmnet
	install vde_vmnet "$(DESTDIR)/$(PREFIX)/bin/vde_vmnet"

install.launchd.plist: launchd/*.plist
	install launchd/io.github.virtualsquare.vde-2.vde_switch.plist "$(DESTDIR)/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.plist"
	install launchd/io.github.AkihiroSuda.vde_vmnet.plist "$(DESTDIR)/Library/LaunchDaemons/io.github.AkihiroSuda.vde_vmnet.plist"

install.launchd: install.launchd.plist
	launchctl load -w "$(DESTDIR)/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.plist"
	launchctl load -w "$(DESTDIR)/Library/LaunchDaemons/io.github.AkihiroSuda.vde_vmnet.plist"

install: install.bin install.launchd

.PHONY: uninstall.bin
uninstall.bin:
	rm -f "$(DESTDIR)/$(PREFIX)/bin/vde_vmnet"

.PHONY: uninstall.launchd
uninstall.launchd:
	launchctl unload -w "$(DESTDIR)/Library/LaunchDaemons/io.github.AkihiroSuda.vde_vmnet.plist"
	launchctl unload -w "$(DESTDIR)/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.plist"

uninstall.launchd.plist: uninstall.launchd
	rm -f "$(DESTDIR)/Library/LaunchDaemons/io.github.AkihiroSuda.vde_vmnet.plist"
	rm -f "$(DESTDIR)/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.plist"

uninstall: uninstall.launchd.plist uninstall.bin

.PHONY: clean
clean:
	rm -f vde_vmnet *.o
