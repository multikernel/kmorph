#ifndef KMORPH_DEVNAME_H
#define KMORPH_DEVNAME_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Devices in the config may be named the way the operator knows them:
 * a network interface, a block device, a framebuffer or a USB bus.
 * sysfs leads from each of those to the PCI function behind it, which
 * is what the kernel hands over.
 */

#define DEVNAME_CLASS_ROOT "/sys/class"
#define DEVNAME_USB_ROOT "/sys/bus/usb/devices"

/* dddd:bb:dd.f */
bool devname_is_pci_id(const char *s);

/*
 * 0 with pci_id filled; -ENOENT: no such name; -EEXIST: the name exists in
 * more than one class; -ENODEV: the device has no PCI function above it.
 */
int devname_resolve(const char *class_root, const char *usb_root, const char *name,
		    char *pci_id, size_t len);

#endif
