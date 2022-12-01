# socket_vmnet: vmnet.framework support for rootless and VDE-less QEMU

`socket_vmnet` provides [vmnet.framework](https://developer.apple.com/documentation/vmnet) support for QEMU.

`socket_vmnet` does not require QEMU to run as the root user.

(But `socket_vmnet` itself has to run as the root, in most cases.)

`socket_vmnet` was forked from [`vde_vmnet`](https://github.com/lima-vm/vde_vmnet) v0.6.0.
Unlike `vde_vmnet`, `socket_vmnet` does not depend on VDE.

## Install

Requires macOS 10.15 or later.

Install from source:
```bash
sudo make PREFIX=/opt/socket_vmnet install
```

The `PREFIX` dir below does not necessarily need to be `/opt/socket_vmnet`, however, it is highly recommended
to set the prefix to a directory that can be only written by the root.

Note that `/usr/local` is typically chowned for a non-root user on Homebrew environments, so
`/usr/local` is *not* an appropriate prefix.

The following files will be installed:
- `/opt/socket_vmnet/bin/socket_vmnet`
- `/opt/socket_vmnet/bin/socket_vmnet_client`
- `/Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.plist`
  - Configured to use `192.168.105.0/24`. Modifiy the file if it conflicts with your local network.

See ["Testing without launchd"](#testing-without-launchd) if you don't prefer to use launchd.

## Usage

```console
/opt/socket_vmnet/bin/socket_vmnet_client /var/run/socket_vmnet qemu-system-x86_64 \
  -device virtio-net-pci,netdev=net0 -netdev socket,id=net0,fd=3 \
  -m 4096 -accel hvf -cdrom ubuntu-22.04-desktop-amd64.iso
```

The guest IP is assigned by the DHCP server provided by macOS.

The guest is accessible to the internet, and the guest IP is accessible from the host.

To confirm, run `sudo apt-get update && sudo apt-get install -y apache2` in the guest, and access the guest IP via Safari on the host.

### Lima integration

Lima (since v0.12.0) provides built-in support for `socket_vmnet`:

```console
$ limactl sudoers | sudo tee /etc/sudoers.d/lima
$ limactl start --name=default template://vmnet
```

See also https://github.com/lima-vm/lima/blob/master/docs/network.md

### Minikube integration

```console
$ minikube start --driver=qemu2 --network-plugin=socket_vmnet
```

### Multi VM
Multiple VMs can be connected to a single `socket_vmnet` instance.

Make sure to specify unique MAC addresses to VMs: `-device virtio-net-pci,netdev=net0,mac=de:ad:be:ef:00:01` .

NOTE: don't confuse MAC addresses of VMs with the MAC address of `socket_vmnet` itself that is printed as `vmnet_mac_address` in the debug log.
You do not need to configure (and you can't, currently) the MAC address of `socket_vmnet` itself.

### Bridged mode

Run `sudo make install BRIDGED=en0`.

The following additional file will be installed:
- `/Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.bridged.en0.plist`

Use `/var/run/socket_vmnet.bridged.en0` as the socket.

## Advanced usage

### Testing without launchd

```console
sudo make install.bin
```

```console
sudo socket_vmnet --vmnet-gateway=192.168.105.1 /tmp/socket_vmnet
```

Note: make sure to run `socket_vmnet` with root (`sudo`). See [FAQs](#FAQs) for the reason.

## FAQs

### Why does `socket_vmnet` require root?

Running `socket_vmnet` without root could be possible by signing the `socket_vmnet` binary with `com.apple.vm.networking` entitlement.

However, signing a binary with `com.apple.vm.networking` entitlement seems to require some contract with Apple.
:disappointed:

> This entitlement is restricted to developers of virtualization software. To request this entitlement, contact your Apple representative.

https://developer.apple.com/documentation/bundleresources/entitlements/com_apple_vm_networking

### Is it possible to run `socket_vmnet` with SETUID?

Yes, but discouraged, as it allows non-root users to write arbitrary files, by specifying certain CLI args and environment variables.

Instead, consider using launchd or sudo.

See [`./etc_sudoers.d/socket_vmnet`](./etc_sudoers.d/socket_vmnet) to allow running `sudo socket_vmnet` with reduced set of args and environment variables.

### How is socket_vmnet related to vde_vmnet?

`socket_vmnet` was forked from [`vde_vmnet`](https://github.com/lima-vm/vde_vmnet) v0.6.0.
Unlike `vde_vmnet`, `socket_vmnet` does not depend on VDE.

### How is socket_vmnet related to QEMU-builtin vmnet support?
There is a proposal to add builtin vmnet support for QEMU: [`[v22] Add vmnet.framework based network backend`](https://patchwork.kernel.org/project/qemu-devel/cover/20220317172839.28984-1-Vladislav.Yaroshchuk@jetbrains.com/).

However, QEMU-builtin vmnet requires running the entire QEMU process as root.

On the other hand, `socket_vmnet` does not require the entire QEMU process to run as root, though `socket_vmnet` has to run as root.

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
sudo /bin/launchctl kickstart -kp system/com.apple.bootpd
```

- Run QEMU with the MAC address: `-device virtio-net-pci,netdev=net0,mac=de:ad:be:ef:00:01` .

NOTE: don't confuse MAC addresses of VMs with the MAC address of `socket_vmnet` itself that is printed as `vmnet_mac_address` in the debug log.
You do not need to configure (and you can't, currently) the MAC address of `socket_vmnet` itself.

## Links
- https://developer.apple.com/documentation/vmnet
- https://developer.apple.com/documentation/bundleresources/entitlements/com_apple_vm_networking
- [`file:///Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/vmnet.framework/Versions/Current/Headers/vmnet.h`](file:///Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/vmnet.framework/Versions/Current/Headers/vmnet.h)

## Troubleshooting
- Set environment variable `DEBUG=1`
- See `/var/run/socket_vmnet.{stdout,stderr}` (when using launchd)
