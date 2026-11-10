#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libfdt.h"
#include "kmorph/fdtutil.h"
#include "kmorph/file.h"
#include "kmorph/screen.h"

static int join(char *out, const char *dir, const char *name)
{
	int n = snprintf(out, PATH_MAX, "%s/%s", dir, name);

	return n < 0 || n >= PATH_MAX ? -ENAMETOOLONG : 0;
}

static int read_attr(const char *dir, const char *attr, char **s)
{
	char path[PATH_MAX];
	int ret = join(path, dir, attr);

	return ret ? ret : file_read_string(path, s);
}

static int read_be(const char *dir, const char *prop, void *out, size_t want)
{
	char path[PATH_MAX];
	size_t len;
	void *buf;
	int ret;

	ret = join(path, dir, prop);
	if (!ret)
		ret = file_read(path, &buf, &len);
	if (ret)
		return ret;
	if (len == want)
		memcpy(out, buf, want);
	else
		ret = -EINVAL;
	free(buf);
	return ret;
}

static int read_be32(const char *dir, const char *prop, uint32_t *v)
{
	fdt32_t cell;
	int ret = read_be(dir, prop, &cell, sizeof(cell));

	if (!ret)
		*v = fdt32_to_cpu(cell);
	return ret;
}

/* A screen some kernel already described as a simple-framebuffer, a successor's own */
static int read_of_node(const char *dir, struct screen *out)
{
	fdt64_t reg[2];
	char *s;
	int ret;

	ret = read_attr(dir, "compatible", &s);
	if (ret)
		return ret;
	ret = strcmp(s, "simple-framebuffer") ? -EOPNOTSUPP : 0;
	free(s);
	if (!ret)
		ret = read_be(dir, "reg", reg, sizeof(reg));
	if (!ret)
		ret = read_be32(dir, "width", &out->width);
	if (!ret)
		ret = read_be32(dir, "height", &out->height);
	if (!ret)
		ret = read_be32(dir, "stride", &out->stride);
	if (!ret)
		ret = read_attr(dir, "format", &s);
	if (ret)
		return ret;
	if (strlen(s) < sizeof(out->format))
		strcpy(out->format, s);
	else
		ret = -EINVAL;
	free(s);
	out->addr = fdt64_to_cpu(reg[0]);
	out->size = fdt64_to_cpu(reg[1]);
	return ret;
}

static int read_uint(const char *dir, const char *attr, const char *fmt,
		     unsigned int *a, unsigned int *b)
{
	char *s;
	int ret = read_attr(dir, attr, &s);

	if (ret)
		return ret;
	ret = sscanf(s, fmt, a, b) == (b ? 2 : 1) ? 0 : -EINVAL;
	free(s);
	return ret;
}

static int read_layout(const char *fb, struct screen *out)
{
	unsigned int width, height, bpp, stride;
	int ret;

	ret = read_uint(fb, "virtual_size", "%u,%u", &width, &height);
	if (!ret)
		ret = read_uint(fb, "bits_per_pixel", "%u", &bpp, NULL);
	if (!ret)
		ret = read_uint(fb, "stride", "%u", &stride, NULL);
	if (ret)
		return ret;

	if (bpp == 32)
		strcpy(out->format, "x8r8g8b8");
	else if (bpp == 16)
		strcpy(out->format, "r5g6b5");
	else
		return -EOPNOTSUPP;
	out->width = width;
	out->height = height;
	out->stride = stride;
	out->size = (uint64_t)stride * height;
	return 0;
}

static int read_bar0(const char *dev, uint64_t *start, uint64_t *size)
{
	uint64_t end, flags;
	char *s;
	int ret;

	ret = read_attr(dev, "resource", &s);
	if (ret)
		return ret;
	ret = sscanf(s, "%" SCNx64 " %" SCNx64 " %" SCNx64, start, &end, &flags) == 3 &&
	      end > *start ? 0 : -EINVAL;
	free(s);
	*size = end - *start + 1;
	return ret;
}

/* The ast driver scans its picture out from the start of its VRAM, BAR 0 */
static int read_ast(const char *fb, const char *dev, struct screen *out)
{
	uint64_t vram, vram_size;
	int ret;

	ret = read_layout(fb, out);
	if (!ret)
		ret = read_bar0(dev, &vram, &vram_size);
	if (ret)
		return ret;
	if (out->size > vram_size)
		return -EINVAL;
	out->addr = vram;
	return 0;
}

int screen_read(const char *class_root, struct screen *out)
{
	char fb[PATH_MAX], dev[PATH_MAX], path[PATH_MAX], driver[PATH_MAX];
	const char *name;
	ssize_t n;
	int ret;

	ret = join(fb, class_root, "graphics/fb0");
	if (!ret)
		ret = join(dev, fb, "device");
	if (ret)
		return ret;
	if (access(dev, F_OK))
		return -ENOENT;

	ret = join(path, dev, "of_node");
	if (ret)
		return ret;
	if (!access(path, F_OK)) {
		ret = read_of_node(path, out);
		if (ret != -ENOENT && ret != -EOPNOTSUPP)
			return ret;
	}

	ret = join(path, dev, "driver");
	if (ret)
		return ret;
	n = readlink(path, driver, sizeof(driver) - 1);
	if (n < 0)
		return -EOPNOTSUPP;
	driver[n] = '\0';
	name = strrchr(driver, '/');
	name = name ? name + 1 : driver;
	if (!strcmp(name, "ast"))
		return read_ast(fb, dev, out);
	return -EOPNOTSUPP;
}

int screen_emit(void *fdt, const struct screen *s)
{
	int ret;

	ret = fdt_property_string(fdt, "compatible", "simple-framebuffer");
	if (!ret)
		ret = fdtutil_prop_reg(fdt, s->addr, s->size);
	if (!ret)
		ret = fdt_property_u32(fdt, "width", s->width);
	if (!ret)
		ret = fdt_property_u32(fdt, "height", s->height);
	if (!ret)
		ret = fdt_property_u32(fdt, "stride", s->stride);
	if (!ret)
		ret = fdt_property_string(fdt, "format", s->format);
	return ret;
}
