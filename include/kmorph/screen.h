#ifndef KMORPH_SCREEN_H
#define KMORPH_SCREEN_H

#include <stdint.h>

/*
 * The picture a display controller scans out right now, as a
 * simple-framebuffer node describes it: a successor given this draws
 * its console where the host's is seen, a BMC's KVM included, with no
 * driver for the display controller.
 */
struct screen {
	uint64_t addr;
	uint64_t size;
	uint32_t width;
	uint32_t height;
	uint32_t stride;
	char format[16];
};

/*
 * Reads class_root/graphics/fb0. -ENOENT: no framebuffer; -EOPNOTSUPP:
 * a display driver whose picture kmorph cannot place.
 */
int screen_read(const char *class_root, struct screen *out);

/* The node's properties, into a node open on fdt. */
int screen_emit(void *fdt, const struct screen *s);

#endif
