#!/bin/bash
set -eux -o pipefail
cd "$(dirname "$0")"

os_major="$(sw_vers -productVersion | cut -d. -f1)"
if [ "$os_major" -lt 26 ]; then
	echo >&2 "SKIP: --vmnet-disable-dhcp requires macOS 26 (host is $(sw_vers -productVersion))"
	exit 0
fi

SOCKET=/var/run/socket_vmnet.disable-dhcp
GATEWAY=192.168.106.1

if [ ! -f ipxe.lkrn ]; then
	curl -fSL -O https://boot.ipxe.org/ipxe.lkrn
fi

sudo /opt/socket_vmnet/bin/socket_vmnet --vmnet-disable-dhcp \
	--vmnet-gateway="${GATEWAY}" --socket-group=staff "${SOCKET}" &
socket_vmnet_pid=$!
cleanup() {
	sudo kill "${socket_vmnet_pid}" 2>/dev/null || true
	sudo rm -f "${SOCKET}"
}
trap cleanup EXIT

for _ in $(seq 1 30); do
	[ -S "${SOCKET}" ] && break
	sleep 1
done
if [ ! -S "${SOCKET}" ]; then
	echo >&2 "ERROR: socket_vmnet did not create ${SOCKET}"
	exit 1
fi

rm -f serial.log
echo >&2 "===== QEMU BEGIN ====="
/opt/socket_vmnet/bin/socket_vmnet_client "${SOCKET}" qemu-system-x86_64 \
	-device virtio-net-pci,netdev=net0 \
	-netdev socket,id=net0,fd=3 \
	-kernel ipxe.lkrn \
	-initrd test-disable-dhcp.ipxe \
	-no-reboot \
	-nographic 2>&1 | tee serial.log
echo >&2 "===== QEMU FINISH ====="

if ! grep -q "dhcp-failed-as-expected" serial.log; then
	echo >&2 "ERROR: guest obtained a DHCP lease, but DHCP should be disabled"
	exit 1
fi

if grep -Eq "net0-ip=[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+" serial.log; then
	echo >&2 "ERROR: guest has an IPv4 address despite DHCP being disabled"
	exit 1
fi

echo >&2 "OK: guest obtained no DHCP lease (DHCP disabled as expected)"
rm -f serial.log
