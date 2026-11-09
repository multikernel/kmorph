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
	local conf=${KMORPH_CONF:-/etc/kmorph/kmorph.conf} mods

	[ -f "$conf" ] || return 0
	mods=$(sed -n 's/^modules[[:space:]]*=[[:space:]]*//p' "$conf" | tr ',' ' ')
	[ -n "$mods" ] && hostonly='' instmods $mods
	return 0
}

# The root device can appear before the takeover is done, the
# predecessor's disk being adopted first; switching root then would kill
# kmorphd mid-takeover, so kmorphd --wait returns, and its unit lets
# initrd.target through, only once the takeover is done.
install() {
	local unit=kmorphd-initrd.service conf=${KMORPH_CONF:-/etc/kmorph/kmorph.conf}

	inst_binary /usr/bin/kmorphd
	[ -f "$conf" ] && inst_simple "$conf" /etc/kmorph/kmorph.conf
	mkdir -p "$initdir$systemdsystemunitdir/initrd.target.wants"
	inst_simple "$moddir/$unit" "$systemdsystemunitdir/$unit"
	ln_r "$systemdsystemunitdir/$unit" "$systemdsystemunitdir/initrd.target.wants/$unit"
}
