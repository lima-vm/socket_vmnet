# socket_vmnet: vmnet.framework support for rootless and VDE-less QEMU

`socket_vmnet` provides [vmnet.framework](https://developer.apple.com/documentation/vmnet) support for QEMU.

`socket_vmnet` does not require QEMU to run as the root user.

(But `socket_vmnet` itself has to run as the root, in most cases.)

`socket_vmnet` was forked from [`vde_vmnet`](https://github.com/lima-vm/vde_vmnet) v0.6.0.
Unlike `vde_vmnet`, `socket_vmnet` does not depend on VDE.

<!-- START doctoc generated TOC please keep comment here to allow auto update -->
<!-- DON'T EDIT THIS SECTION, INSTEAD RE-RUN doctoc TO UPDATE -->

- [Install](#install)
  - [From binary](#from-binary)
  - [From source](#from-source)
  - [From Homebrew](#from-homebrew)
  - [From MacPorts](#from-macports)
- [Usage](#usage)
  - [QEMU](#qemu)
  - [Lima](#lima)
- [Advanced usage](#advanced-usage)
  - [Multi VM](#multi-vm)
  - [Bridged mode](#bridged-mode)
- [FAQs](#faqs)
  - [Why does `socket_vmnet` require root?](#why-does-socket_vmnet-require-root)
  - [Is it possible to run `socket_vmnet` with SETUID?](#is-it-possible-to-run-socket_vmnet-with-setuid)
  - [How is socket_vmnet related to vde_vmnet?](#how-is-socket_vmnet-related-to-vde_vmnet)
  - [How is socket_vmnet related to QEMU-builtin vmnet support?](#how-is-socket_vmnet-related-to-qemu-builtin-vmnet-support)
  - [How to use static IP addresses?](#how-to-use-static-ip-addresses)
  - [How to reserve DHCP addresses?](#how-to-reserve-dhcp-addresses)
  - [IP address is not assigned](#ip-address-is-not-assigned)
- [Links](#links)
- [Troubleshooting](#troubleshooting)

<!-- END doctoc generated TOC please keep comment here to allow auto update -->

## Install

Requires macOS 10.15 or later.

### From binary

```bash
VERSION="$(curl -fsSL https://api.github.com/repos/lima-vm/socket_vmnet/releases/latest | jq -r .tag_name)"
FILE="socket_vmnet-${VERSION:1}-$(uname -m).tar.gz"

# Download the binary archive
curl -OSL "https://github.com/lima-vm/socket_vmnet/releases/download/${VERSION}/${FILE}"

# (Optional) Attest the GitHub Artifact Attestation using GitHub's gh command (https://cli.github.com)
gh attestation verify --owner=lima-vm "${FILE}"

# (Optional) Preview the contents of the binary archive
tar tzvf "${FILE}"

# Install /opt/socket_vmnet from the binary archive
sudo tar Cxzvf / "${FILE}" opt/socket_vmnet
```

This downloads and installs the latest release from <https://github.com/lima-vm/socket_vmnet/releases>.

<details>

<summary>Launchd (optional, not needed for Lima)</summary>

<p>

To install the launchd service:

```bash
SERVICE_ID=io.github.lima-vm.socket_vmnet
sudo cp "/opt/socket_vmnet/share/doc/socket_vmnet/launchd/$SERVICE_ID.plist" "/Library/LaunchDaemons/$SERVICE_ID.plist"
sudo launchctl bootstrap system "/Library/LaunchDaemons/$SERVICE_ID.plist"
sudo launchctl enable system/$SERVICE_ID
sudo launchctl kickstart -kp system/$SERVICE_ID
```

The launchd unit file will be installed as `/Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.plist`.

Default configuration:

| Config  | Value                          |
| ------- | ------------------------------ |
| Socket  | `/var/run/socket_vmnet`        |
| Stdout  | `/var/log/socket_vmnet/stdout` |
| Stderr  | `/var/log/socket_vmnet/stderr` |
| Gateway | 192.168.105.1                  |

To uninstall the launchd service:

```bash
SERVICE_ID=io.github.lima-vm.socket_vmnet
sudo launchctl bootout system "/Library/LaunchDaemons/$SERVICE_ID.plist" || true
sudo rm -f "/Library/LaunchDaemons/$SERVICE_ID.plist"
```

</p>

</details>

### From source

```bash
sudo make install.bin
```

This installs binaries using `PREFIX=/opt/socket_vmnet`:

- `/opt/socket_vmnet/bin/socket_vmnet`
- `/opt/socket_vmnet/bin/socket_vmnet_client`

You can customize the install location using the `PREFIX` environment variable,
however, it is highly recommended to set the prefix to a directory that can be
only written by the root. Note that `/usr/local/bin` is sometimes chowned for a
non-admin user, so `/usr/local` is _not_ an appropriate prefix.

Run the following command to start the daemon:

```bash
sudo /opt/socket_vmnet/bin/socket_vmnet --vmnet-gateway=192.168.105.1 /var/run/socket_vmnet
```

> [!TIP]
> `sudo make install` is also available in addition to `sudo make install.bin`.
> The former one installs the launchd service (see below) too.

<details>

<summary>Launchd (optional, not needed for Lima)</summary>

<p>

To install the launchd service:

```bash
sudo make install.launchd
```

The launchd unit file will be installed as `/Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.plist`.

Default configuration:

| Config  | Value                          |
| ------- | ------------------------------ |
| Socket  | `/var/run/socket_vmnet`        |
| Stdout  | `/var/log/socket_vmnet/stdout` |
| Stderr  | `/var/log/socket_vmnet/stderr` |
| Gateway | 192.168.105.1                  |

To uninstall the launchd service:

```bash
sudo make uninstall.launchd
```

</p>

</details>

### From Homebrew

<details>

<p>

```bash
brew install socket_vmnet
```

The binaries will be installed onto the following paths:

- `${HOMEBREW_PREFIX}/opt/socket_vmnet/bin/socket_vmnet`
- `${HOMEBREW_PREFIX}/opt/socket_vmnet/bin/socket_vmnet_client`

The `${HOMEBREW_PREFIX}` path defaults to `/opt/homebrew` on ARM, `/usr/local` on Intel.

The `${HOMEBREW_PREFIX}/opt/socket_vmnet` directory is usually symlinked to `../Cellar/socket_vmnet/${VERSION}`.

Run the following command to start the daemon:

```bash
mkdir -p ${HOMEBREW_PREFIX}/var/run
sudo ${HOMEBREW_PREFIX}/opt/socket_vmnet/bin/socket_vmnet --vmnet-gateway=192.168.105.1 ${HOMEBREW_PREFIX}/var/run/socket_vmnet
```

> [!WARNING]
> Typically, the `socket_vmnet` binary in the `${HOMEBREW_PREFIX}` can be replaced by any user in the `admin` group.

<details>

<summary>Launchd (optional, not needed for Lima)</summary>

<p>

To install the launchd service:

```bash
# sudo is necessary for the next line
sudo ${HOMEBREW_PREFIX}/bin/brew services start socket_vmnet
```

The launchd unit file will be installed as `/Library/LaunchDaemons/homebrew.mxcl.socket_vmnet.plist`.

Default configuration:

| Config  | Value                                            |
| ------- | ------------------------------------------------ |
| Socket  | `${HOMEBREW_PREFIX}/var/run/socket_vmnet`        |
| Stdout  | `${HOMEBREW_PREFIX}/var/log/socket_vmnet/stdout` |
| Stderr  | `${HOMEBREW_PREFIX}/var/log/socket_vmnet/stderr` |
| Gateway | 192.168.105.1                                    |

To uninstall the launchd service:

```bash
sudo ${HOMEBREW_PREFIX}/bin/brew services stop socket_vmnet
```

</p>

</details>

</p>

</details>

### From MacPorts

<details>

<p>

```bash
sudo port install socket_vmnet
```

The binaries will be installed onto the following paths:

- `/opt/local/bin/socket_vmnet`
- `/opt/local/bin/socket_vmnet_client`

Run the following command to start the daemon manually:

```bash
sudo /opt/local/bin/socket_vmnet --vmnet-gateway=192.168.105.1  /var/run/socket_vmnet
```

<details>

<summary>Launchd (optional, not needed for Lima)</summary>

<p>

To install the launchd service:

```bash
sudo port load socket_vmnet
```

The launchd unit file will be installed as
`/Library/LaunchDaemons/org.macports.socket_vmnet.plist`.

Default configuration:

| Config  | Value                       |
| ------- | --------------------------- |
| Socket  | `/var/run/socket_vmnet`     |
| Stdout  | `/var/log/socket_vmnet.log` |
| Gateway | 192.168.105.1               |

To uninstall the launchd service:

```bash
sudo port unload socket_vmnet
```

</p>

</details>

</p>

</details>

## Usage

### QEMU

Make sure that the `socket_vmnet` daemon is running, and execute QEMU via `socket_vmnet_client` as follows:

```console
${HOMEBREW_PREFIX}/opt/socket_vmnet/bin/socket_vmnet_client \
  ${HOMEBREW_PREFIX}/var/run/socket_vmnet \
  qemu-system-x86_64 \
  -device virtio-net-pci,netdev=net0 -netdev socket,id=net0,fd=3 \
  -m 4096 -accel hvf -cdrom ubuntu-22.04-desktop-amd64.iso
```

The guest IP is assigned by the DHCP server provided by macOS.

The guest is accessible to the internet, and the guest IP is accessible from the host.

To confirm, run `sudo apt-get update && sudo apt-get install -y apache2` in the guest, and access the guest IP via Safari on the host.

### Lima

Lima (since v0.12.0) provides built-in support for `socket_vmnet`:

```console
$ limactl sudoers | sudo tee /etc/sudoers.d/lima
$ limactl start --name=default template://vmnet
```

See also https://github.com/lima-vm/lima/blob/master/docs/network.md

## Advanced usage

### Multi VM

Multiple VMs can be connected to a single `socket_vmnet` instance.

Make sure to specify unique MAC addresses to VMs: `-device virtio-net-pci,netdev=net0,mac=de:ad:be:ef:00:01` .

NOTE: don't confuse MAC addresses of VMs with the MAC address of `socket_vmnet` itself that is printed as `vmnet_mac_address` in the debug log.
You do not need to configure (and you can't, currently) the MAC address of `socket_vmnet` itself.

### Bridged mode

See [`./launchd/io.github.lima-vm.socket_vmnet.bridged.en0.plist`](./launchd/io.github.lima-vm.socket_vmnet.bridged.en0.plist).

Install:

```bash
BRIDGED=en0
sed -e "s@/opt@${HOMEBREW_PREFIX}/opt@g; s@/var@${HOMEBREW_PREFIX}/var@g; s@en0@${BRIDGED}@g" ./launchd/io.github.lima-vm.socket_vmnet.bridged.en0.plist \
  | sudo tee /Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.bridged.${BRIDGED}.plist
sudo launchctl bootstrap system /Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.bridged.${BRIDGED}.plist
sudo launchctl enable system/io.github.lima-vm.socket_vmnet.bridged.${BRIDGED}
sudo launchctl kickstart -kp system/io.github.lima-vm.socket_vmnet.bridged.${BRIDGED}
```

Use `${HOMEBREW_PREFIX}/var/run/socket_vmnet.bridged.en0` as the socket.

Uninstall:

```bash
BRIDGED=en0
sudo launchctl bootout system /Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.bridged.${BRIDGED}.plist
sudo rm /Library/LaunchDaemons/io.github.lima-vm.socket_vmnet.bridged.${BRIDGED}.plist
```

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

QEMU 7.1 added [the built-in support for vmnet](https://github.com/qemu/qemu/blob/v7.1.0/qapi/net.json#L626-L631).

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

```bash
# bootptab
%%
# hostname      hwtype  hwaddr              ipaddr          bootfile
tmp-vm01        1       de:ad:be:ef:00:01   192.168.105.100
```

- Reload the DHCP daemon.

```bash
sudo /bin/launchctl kickstart -kp system/com.apple.bootpd
```

- Run QEMU with the MAC address: `-device virtio-net-pci,netdev=net0,mac=de:ad:be:ef:00:01` .

NOTE: don't confuse MAC addresses of VMs with the MAC address of `socket_vmnet` itself that is printed as `vmnet_mac_address` in the debug log.
You do not need to configure (and you can't, currently) the MAC address of `socket_vmnet` itself.

### IP address is not assigned

Try the following commands:

```console
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --remove /usr/libexec/bootpd
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add /usr/libexec/bootpd
/usr/libexec/ApplicationFirewall/socketfilterfw --unblock /usr/libexec/bootpd
```

## Links

- https://developer.apple.com/documentation/vmnet
- https://developer.apple.com/documentation/bundleresources/entitlements/com_apple_vm_networking
- [`file:///Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/vmnet.framework/Versions/Current/Headers/vmnet.h`](file:///Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks/vmnet.framework/Versions/Current/Headers/vmnet.h)

## Troubleshooting

- To enable verbose debug logs, set the environment variable `DEBUG=1`.
- When using launchd, logs are written to `/var/log/socket_vmnet/stderr`.
  `/var/log/socket_vmnet/stdout` is not used and expected to be empty.
