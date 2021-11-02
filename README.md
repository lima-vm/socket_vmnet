# vde_vmnet: vmnet.framework support for rootless QEMU

`vde_vmnet` provides [vmnet.framework](https://developer.apple.com/documentation/vmnet) support for VDE applications such as QEMU.

`vde_vmnet` does not require QEMU to run as the root user.

(But `vde_vmnet` itself has to run as the root, in most cases.)

## Install

Requires macOS 10.15 or later.

### Step 1: Install vde-2 (`vde_switch`)

The version of `vde-2` must be [commit 50964c3f](https://github.com/virtualsquare/vde-2/tree/50964c3f) (2021-08-31) or later.

The `--prefix` dir below does not necessarily need to be `/opt/vde`, however, it is highly recommended
to set the prefix to a directory that can be only written by the root.

Note that `/usr/local` is typically chowned for a non-root user on Homebrew environments, so
`/usr/local` is *not* an appropriate prefix.

```bash
git clone https://github.com/virtualsquare/vde-2.git
cd vde-2
autoreconf -fis
./configure --prefix=/opt/vde
make
sudo make install
```

### Step 2: Install `vde_vmnet`
```bash
git clone https://github.com/lima-vm/vde_vmnet
cd vde_vmnet
make PREFIX=/opt/vde
sudo make PREFIX=/opt/vde install
```

The following files will be installed:
- `/opt/vde/bin/vde_vmnet`
- `/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.plist`
- `/Library/LaunchDaemons/io.github.lima-vm.vde_vmnet.plist`
  - Configured to use `192.168.105.0/24`. Modifiy the file if it conflicts with your local network.

See ["Testing without launchd"](#testing-without-launchd) if you don't prefer to use launchd.

## Usage

```console
qemu-system-x86_64 \
  -device virtio-net-pci,netdev=net0 -netdev vde,id=net0,sock=/var/run/vde.ctl \
  -m 4096 -accel hvf -cdrom ubuntu-21.04-desktop-amd64.iso
```

The guest IP is assigned by the DHCP server provided by macOS.

The guest is accessible to the internet, and the guest IP is accessible from the host.

To confirm, run `sudo apt-get update && sudo apt-get install -y apache2` in the guest, and access the guest IP via Safari on the host.

### Lima integration

See https://github.com/lima-vm/lima/blob/master/docs/network.md to learn how to use `vde_vmnet` with [Lima](https://github.com/lima-vm/lima).

### Multi VM
Multiple VMs can be connected to a single `vde_vmnet` instance.

Make sure to specify unique MAC addresses to VMs: `-device virtio-net-pci,netdev=net0,mac=de:ad:be:ef:00:01` .

NOTE: don't confuse MAC addresses of VMs with the MAC address of `vde_vmnet` itself that is printed as `vmnet_mac_address` in the debug log.
You do not need to configure (and you can't, currently) the MAC address of `vde_vmnet` itself.

### Bridged mode

Run `sudo make install BRIDGED=en0`.

The following additional files will be installed:
- `/Library/LaunchDaemons/io.github.virtualsquare.vde-2.vde_switch.bridged.en0.plist`
- `/Library/LaunchDaemons/io.github.lima-vm.vde_vmnet.bridged.en0.plist`

Use `/var/run/vde.bridged.en0.ctl` as the VDE socket path.

## Advanced usage

### Testing without launchd

```console
make

make install.bin
```

```console
vde_switch --unix /tmp/vde.ctl
```

```console
sudo vde_vmnet --vmnet-gateway=192.168.105.1 /tmp/vde.ctl
```

Note: make sure to run `vde_vmnet` with root (`sudo`). See [FAQs](#FAQs) for the reason.
`vde_switch` does not need to be executed as root.

### PTP mode (Switchless mode)

- Pros: doesn't require the `vde_switch` process to be running
- Cons: Only single QEMU process can connect to the socket. Multiple `vde_vmnet` processes need to be launched for multiple QEMU processes.

To enable PTP mode, append `[]` to the socket path argument of `vde_vmnet`.

```console
sudo vde_vmnet /tmp/vde.ptp[]
```

QEMU has to be launched with `port=65535` without `[]`.

```console
qemu-system-x86_64 -netdev vde,id=net0,sock=/tmp/vde.ptp,port=65535 ...
```

The "port" number here has nothing to do with TCP/UDP ports.

## FAQs
### Why does `vde_vmnet` require root?

Running `vde_vmnet` without root could be possible by signing the `vde_vmnet` binary with `com.apple.vm.networking` entitlement.

However, signing a binary with `com.apple.vm.networking` entitlement seems to require some contract with Apple.
:disappointed:

> This entitlement is restricted to developers of virtualization software. To request this entitlement, contact your Apple representative.

https://developer.apple.com/documentation/bundleresources/entitlements/com_apple_vm_networking

### Is it possible to run `vde_vmnet` with SETUID?

Yes, but discouraged, as it allows non-root users to write arbitrary files, by specifying certain CLI args and environment variables.

Instead, consider using launchd or sudo.

See [`./etc_sudoers.d/vde_vmnet`](./etc_sudoers.d/vde_vmnet) to allow running `sudo vde_vmnet` with reduced set of args and environment variables.

### How is vde_vmnet related to QEMU-builtin vmnet support?
There are proposal to add builtin vmnet support for QEMU:
- https://lore.kernel.org/qemu-devel/20210617143246.55336-1-yaroshchuk2000@gmail.com/
- https://lore.kernel.org/qemu-devel/20210708054451.9374-1-akihiko.odaki@gmail.com/

However, QEMU-builtin vmnet is highly likely to require running the entire QEMU process as root.

On the other hand, `vde_vmnet` does not require the entire QEMU process to run as root, though `vde_vmnet` has to run as root.

### How to use static IP addresses?
When `--vmnet-gateway=IP` is set to "192.168.105.1", the whole subnet (192.168.105.2-192.168.105.254) is used as the DHCP range.

To use static IP addresses, limit the DHCP range with `--vmnet-dhcp-end=IP`.

For example, `--vmnet-gateway=192.168.105.1 --vmnet-dhcp-end=192.168.105.100` allows using `192.168.105.101-192.168.105.254` 
as non-DHCP static addresses.

### How to reserve DHCP addresses?
- Decide a unique MAC address for the VM, e.g. `de:ad:be:ef:00:01`.

- Decide a reserved IP address, e.g., "192.168.105.100"

- Create `/etc/bootptab` like this. Make sure not to drop the "%%" header.
```
# bootptab
%%
# hostname      hwtype  hwaddr              ipaddr          bootfile
tmp-vm01        1       de:ad:be:ef:00:01   192.168.105.100
```

- Reload the DHCP daemon.
```
sudo /bin/launchctl unload -w /System/Library/LaunchDaemons/bootps.plist
sudo /bin/launchctl load -w /System/Library/LaunchDaemons/bootps.plist
```

- Run QEMU with the MAC address: `-device virtio-net-pci,netdev=net0,mac=de:ad:be:ef:00:01` .

NOTE: don't confuse MAC addresses of VMs with the MAC address of `vde_vmnet` itself that is printed as `vmnet_mac_address` in the debug log.
You do not need to configure (and you can't, currently) the MAC address of `vde_vmnet` itself.

## Links
- https://developer.apple.com/documentation/vmnet
- https://developer.apple.com/documentation/bundleresources/entitlements/com_apple_vm_networking
- [`file:///Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/vmnet.framework/Versions/Current/Headers/vmnet.h`](file:///Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/vmnet.framework/Versions/Current/Headers/vmnet.h)

## Troubleshooting
- Set environment variable `DEBUG=1`
- See `/var/run/vde_vmnet.{stdout,stderr}` (when using launchd)
