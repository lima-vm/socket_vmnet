# Example sudoers file for running `vde_vmnet`

To allow non-root users to run `vde_vmnet`, use [launchd](../launchd) *or*
install [the `vde_vmnet` file in this directory](./vde_vmnet) as `/etc/sudoers.d/vde_vmnet`.

At least you have to modify the `sha224` digests in [`/etc/sudoers.d/vde_vmnet`](./vde_vmnet).
See the comment lines in the file for the further information.
