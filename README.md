# vde_vmnet: vmnet.framework support for rootless QEMU

vde_vmnet provides [vmnet.framework](https://developer.apple.com/documentation/vmnet) support for VDE applications such as QEMU.

vde_vmnet does not require QEMU to run as the root user.

(But vde_vmnet itself has to run as the root, in most cases.)

## Install

```console
brew install vde

make

make install
```

## Usage

```console
vde_switch --unix /tmp/vde
```

```console
sudo vde_vmnet /tmp/vde
```

Note: make sure to run `vde_vmnet` with root (`sudo`). See [FAQs](#FAQs) for the reason.

```console
qemu-system-x86_64 \
  -device virtio-net-pci,netdev=net0 -netdev vde,id=net0,sock=/tmp/vde \
  -m 4096 -accel hvf -cdrom ubuntu-21.04-desktop-amd64.iso
```

The guest IP is assigned by the DHCP server provided by macOS.

Make sure that the guest is accessible to the internet, and the guest IP is accessible from the host.
e.g., Run `sudo apt-get update && sudo apt-get install -y apache2` in the guest, and access the guest IP via Safari on the host.

## FAQs
### Why does `vde_vmnet` require root?

Running `vde_vmnet` without root could be possible by signing the `vde_vmnet` binary with `com.apple.vm.networking` entitlement.

However, signing a binary with `com.apple.vm.networking` entitlement seems to require some contract with Apple.
:disappointed:

> This entitlement is restricted to developers of virtualization software. To request this entitlement, contact your Apple representative.

https://developer.apple.com/documentation/bundleresources/entitlements/com_apple_vm_networking

### How is vde_vmnet related to QEMU-builtin vmnet support?
There are proposal to add builtin vmnet support for QEMU:
- https://lore.kernel.org/qemu-devel/20210617143246.55336-1-yaroshchuk2000@gmail.com/
- https://lore.kernel.org/qemu-devel/20210708054451.9374-1-akihiko.odaki@gmail.com/

However, QEMU-builtin vmnet is highly likely to require running the entire QEMU process as root.

On ther otherhand, `vde_vmnet` does not require the entire QEMU process to run as root, though `vde_vmnet` has to run as root.

### How to use non-DHCP address?

(TODO)

## Links
- https://developer.apple.com/documentation/vmnet
- https://developer.apple.com/documentation/bundleresources/entitlements/com_apple_vm_networking
- file:///Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/vmnet.framework/Versions/Current/Headers/vmnet.h

## Troubleshooting
- Set environment variable `DEBUG=1`
