#!/bin/bash
set -eux -o pipefail
cd "$(dirname "$0")"

if [ "$#" -ne 1 ]; then
	echo >&2 "Usage: $0 SOCKET"
	exit 1
fi

SOCKET="$1"

if [ ! -f ipxe.lkrn ]; then
	curl -fSL -O https://boot.ipxe.org/ipxe.lkrn
fi

rm -f serial.log
echo >&2 "===== QEMU BEGIN ====="
/opt/socket_vmnet/bin/socket_vmnet_client "$SOCKET" qemu-system-x86_64 \
	-device virtio-net-pci,netdev=net0 \
	-netdev socket,id=net0,fd=3 \
	-kernel ipxe.lkrn \
	-initrd test.ipxe \
	-no-reboot \
	-nographic 2>&1 | tee serial.log
echo >&2 "===== QEMU FINISH ====="

if ! grep -q "net0/mac:hex = 52:54:00:12:34:56" serial.log; then
	echo >&2 "ERROR: net0/mac:hex has unexpected value"
	exit 1
fi

if ! grep -q "net0.dhcp/ip:ipv4 = 192.168.105." serial.log; then
	echo >&2 "ERROR: net0.dhcp/ip:ipv4 not found"
	exit 1
fi

if ! grep -q "net0.dhcp/gateway:ipv4 = 192.168.105.1" serial.log; then
	echo >&2 "ERROR: net0.dhcp/gateway:ipv4 not found"
	exit 1
fi

rm -f serial.log
