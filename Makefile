# PREFIX should be only writable by the root to avoid privilege escalation with launchd or sudo
PREFIX ?= /opt/socket_vmnet

CFLAGS ?= -O3

VERSION ?= $(shell git describe --match 'v[0-9]*' --dirty='.m' --always --tags)

CFLAGS += -DVERSION=\"$(VERSION)\"

LDFLAGS ?=
VMNET_LDFLAGS = -framework vmnet

# ARCH support arm64 and x86_64
ARCH ?=

ifneq (,$(findstring arm64,$(ARCH)))
	HOST ?= --host arm-apple-darwin
	CFLAGS += -arch arm64
else ifneq (,$(findstring x86_64,$(ARCH)))
	HOST ?= --host x86_64-apple-darwin
	CFLAGS += -arch x86_64
endif

# Interface name for bridged mode. Empty value (default) disables bridged mode.
BRIDGED ?=

all: socket_vmnet socket_vmnet_client

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $< -o $@

socket_vmnet: $(patsubst %.c, %.o, $(wildcard *.c))
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $(VMNET_LDFLAGS) $^

socket_vmnet_client: $(patsubst %.c, %.o, $(wildcard client/*.c))
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $^

install.bin: socket_vmnet socket_vmnet_client
	mkdir -p "$(DESTDIR)/$(PREFIX)/bin"
	install socket_vmnet "$(DESTDIR)/$(PREFIX)/bin/socket_vmnet"
	install socket_vmnet_client "$(DESTDIR)/$(PREFIX)/bin/socket_vmnet_client"

install.launchd.plist: launchd/*.plist
	sed -e "s@/opt/socket_vmnet@$(PREFIX)@g" launchd/io.github.lima-vm.socket_vmnet.plist > "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.plist"

ifneq ($(BRIDGED),)
	sed -e "s@/opt/socket_vmnet@$(PREFIX)@g" -e "s/en0/$(BRIDGED)/g" launchd/io.github.lima-vm.socket_vmnet.bridged.en0.plist > "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.bridged.$(BRIDGED).plist"
endif

install.launchd: install.launchd.plist
	launchctl load -w "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.plist"
ifneq ($(BRIDGED),)
	launchctl load -w "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.bridged.$(BRIDGED).plist"
endif

install: install.bin install.launchd

.PHONY: uninstall.bin
uninstall.bin:
	rm -f "$(DESTDIR)/$(PREFIX)/bin/socket_vmnet"
	rm -f "$(DESTDIR)/$(PREFIX)/bin/socket_vmnet_client"

.PHONY: uninstall.launchd
uninstall.launchd:
	launchctl unload -w "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.plist"
ifneq ($(BRIDGED),)
	launchctl unload -w "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.bridged.$(BRIDGED).plist"
endif

uninstall.launchd.plist: uninstall.launchd
	rm -f "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.plist"
ifneq ($(BRIDGED),)
	rm -f "$(DESTDIR)/Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.bridged.$(BRIDGED).plist"
endif

.PHONY: uninstall.run
uninstall.run:
	rm -f /var/run/socket_vmnet*

uninstall: uninstall.launchd.plist uninstall.bin uninstall.run

.PHONY: clean
clean:
	rm -f socket_vmnet socket_vmnet_client *.o client/*.o
