#!/bin/bash
# dracut module: kmorphd inside the initramfs, so a successor booted
# from the distro's own image probes the host while dracut waits for
# the root device. Added only on request, by kmorph-mkimage; the host's
# boot initramfs never carries it.

check() {
	return 255
}

depends() {
	echo systemd
}

install() {
	inst_binary /usr/bin/kmorphd
	[ -f /etc/kmorph/kmorph.conf ] && inst_simple /etc/kmorph/kmorph.conf
	inst_simple "$moddir/kmorphd-initrd.service" \
		"$systemdsystemunitdir/kmorphd-initrd.service"
	mkdir -p "$initdir$systemdsystemunitdir/initrd.target.wants"
	ln_r "$systemdsystemunitdir/kmorphd-initrd.service" \
		"$systemdsystemunitdir/initrd.target.wants/kmorphd-initrd.service"
}
