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

# Modules the config asks for: the vsock transport when it is one,
# drivers for devices handed to the successor.
installkernel() {
	local mods

	[ -f /etc/kmorph/kmorph.conf ] || return 0
	mods=$(sed -n 's/^modules[[:space:]]*=[[:space:]]*//p' /etc/kmorph/kmorph.conf | tr ',' ' ')
	[ -n "$mods" ] && hostonly='' instmods $mods
	return 0
}

# The root device can appear before the takeover is done, the
# predecessor's disk being adopted first; switching root then would kill
# kmorphd mid-takeover, so the wait unit holds initrd.target until then.
install() {
	local unit

	inst_binary /usr/bin/kmorphd
	inst_multiple grep sleep
	[ -f /etc/kmorph/kmorph.conf ] && inst_simple /etc/kmorph/kmorph.conf
	mkdir -p "$initdir$systemdsystemunitdir/initrd.target.wants"
	for unit in kmorphd-initrd.service kmorph-takeover-wait.service; do
		inst_simple "$moddir/$unit" "$systemdsystemunitdir/$unit"
		ln_r "$systemdsystemunitdir/$unit" \
			"$systemdsystemunitdir/initrd.target.wants/$unit"
	done
}
