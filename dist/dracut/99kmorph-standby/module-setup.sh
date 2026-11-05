#!/bin/bash
# dracut module: the kmorph side of a standby root. kmorph-mkimage stages
# that root with dracut, so that the host's own files and their libraries
# are what the successor runs, and then turns it into a regular system.

check() {
	return 255
}

depends() {
	echo systemd systemd-journald systemd-udevd systemd-sysctl systemd-tmpfiles udev-rules
}

install() {
	local unit

	inst_binary "${KMORPHD:-/usr/bin/kmorphd}" /usr/bin/kmorphd
	inst_multiple mount umount pivot_root udevadm systemctl sysctl \
		readlink sed tr cp mkdir cat
	inst_multiple -o e2fsck
	inst_simple "$moddir/kmorph-switch" /usr/lib/kmorph/kmorph-switch
	inst_simple "$moddir/90-kmorph-standby.conf" /etc/sysctl.d/90-kmorph-standby.conf
	for unit in kmorph-standby.target kmorphd.service kmorph-switch.service \
		kmorph-standby-var.service; do
		inst_simple "$moddir/$unit" "$systemdsystemunitdir/$unit"
	done
}
