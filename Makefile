PREFIX ?= /usr/local

CFLAGS ?= -O3

LDFLAGS ?= -lvdeplug -framework vmnet

all: vde_vmnet

vde_vmnet: *.c
	$(CC) $(CFLAGS) -o $@ $(LDFLAGS) $<

install: vde_vmnet
	install vde_vmnet "$(DESTDIR)/$(PREFIX)/bin/vde_vmnet"

.PHONY: uninstall
uninstall:
	rm -f "$(DESTDIR)/$(PREFIX)/bin/vde_vmnet"

.PHONY: clean
clean:
	rm -f vde_vmnet
