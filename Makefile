# PREFIX should be only writable by the root to avoid privilege escalation with launchd or sudo
PREFIX ?= /opt/vde

# VDEPREFIX should be only writable by the root to avoid privilege escalation with launchd or sudo
VDEPREFIX ?= $(PREFIX)

CFLAGS ?= -O3

VERSION ?= $(shell git describe --match 'v[0-9]*' --dirty='.m' --always --tags)
CFLAGS += -I"$(VDEPREFIX)/include" -DVERSION=\"$(VERSION)\"

LDFLAGS += -L"$(VDEPREFIX)/lib" -lvdeplug -framework vmnet

# Interface name for bridged mode. Empty value (default) disables bridged mode.
BRIDGED ?=

all: vde_vmnet

OBJS = $(patsubst %.c, %.o, $(wildcard *.c))

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

vde_vmnet: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $(OBJS)

install.bin: vde_vmnet
	install vde_vmnet "$(DESTDIR)/$(PREFIX)/bin/vde_vmnet"

install.vde-2:
	git submodule update --init
	cd vde-2 && autoreconf -fis && CFLAGS="" LDFLAGS="" ./configure --prefix=$(VDEPREFIX) && make && make install

install.launchd.plist: launchd/*.plist
	sed -e "s@/opt/vde@$(PREFIX)@g" launchd/io.github.virtualsquare.vde-2.vde_switch.plist > "$(DESTDIR)/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.plist"
	sed -e "s@/opt/vde@$(PREFIX)@g" launchd/io.github.lima-vm.vde_vmnet.plist > "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.vde_vmnet.plist"

ifneq ($(BRIDGED),)
	sed -e "s@/opt/vde@$(PREFIX)@g" -e "s/en0/$(BRIDGED)/g" launchd/io.github.virtualsquare.vde-2.vde_switch.bridged.en0.plist > "$(DESTDIR)/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.bridged.$(BRIDGED).plist"
	sed -e "s@/opt/vde@$(PREFIX)@g" -e "s/en0/$(BRIDGED)/g" launchd/io.github.lima-vm.vde_vmnet.bridged.en0.plist > "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.vde_vmnet.bridged.$(BRIDGED).plist"
endif

install.launchd: install.launchd.plist
	launchctl load -w "$(DESTDIR)/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.plist"
	launchctl load -w "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.vde_vmnet.plist"
ifneq ($(BRIDGED),)
	launchctl load -w "$(DESTDIR)/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.bridged.$(BRIDGED).plist"
	launchctl load -w "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.vde_vmnet.bridged.$(BRIDGED).plist"
endif

install: install.vde-2 install.bin install.launchd

.PHONY: uninstall.bin
uninstall.bin:
	rm -f "$(DESTDIR)/$(PREFIX)/bin/vde_vmnet"

.PHONY: uninstall.vde-2
uninstall.vde-2:
	cd vde-2 && make uninstall

.PHONY: uninstall.launchd
uninstall.launchd:
	launchctl unload -w "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.vde_vmnet.plist"
	launchctl unload -w "$(DESTDIR)/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.plist"
ifneq ($(BRIDGED),)
	launchctl unload -w "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.vde_vmnet.bridged.$(BRIDGED).plist"
	launchctl unload -w "$(DESTDIR)/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.bridged.$(BRIDGED).plist"
endif

uninstall.launchd.plist: uninstall.launchd
	rm -f "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.vde_vmnet.plist"
	rm -f "$(DESTDIR)/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.plist"
ifneq ($(BRIDGED),)
	rm -f "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.vde_vmnet.bridged.$(BRIDGED).plist"
	rm -f "$(DESTDIR)/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.bridged.$(BRIDGED).plist"
endif

uninstall: uninstall.launchd.plist uninstall.bin uninstall.vde-2

.PHONY: clean
clean:
	rm -f vde_vmnet *.o
