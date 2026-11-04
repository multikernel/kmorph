#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "kmorph/dmar.h"
#include "kmorph/file.h"

#define DMAR_HEADER_LEN 48
#define DMAR_TYPE_DRHD 0
#define DMAR_DRHD_LEN 16
#define DMAR_PAGE 0x1000ULL

static uint16_t le16(const unsigned char *p)
{
	return p[0] | p[1] << 8;
}

static uint32_t le32(const unsigned char *p)
{
	return le16(p) | (uint32_t)le16(p + 2) << 16;
}

static uint64_t le64(const unsigned char *p)
{
	return le32(p) | (uint64_t)le32(p + 4) << 32;
}

static int parse_units(const unsigned char *table, size_t len, struct rangelist *out)
{
	size_t off;
	int ret;

	ret = -EINVAL;
	if (len < DMAR_HEADER_LEN || memcmp(table, "DMAR", 4) || le32(table + 4) > len)
		goto out;
	len = le32(table + 4);

	for (off = DMAR_HEADER_LEN; off + 4 <= len; off += le16(table + off + 2)) {
		uint16_t slen = le16(table + off + 2);
		const unsigned char *s = table + off;

		if (slen < 4 || off + slen > len)
			goto out;
		if (le16(s) != DMAR_TYPE_DRHD)
			continue;
		if (slen < DMAR_DRHD_LEN)
			goto out;
		/* Size: 2^N pages of registers, in its low four bits */
		ret = rangelist_add(out, le64(s + 8), DMAR_PAGE << (s[5] & 0xf));
		if (ret)
			goto out;
		ret = -EINVAL;
	}
	ret = 0;
out:
	if (ret)
		rangelist_free(out);
	return ret;
}

int dmar_read(const char *path, struct dmar_info *out)
{
	int ret;

	out->units.r = NULL;
	out->units.count = 0;
	out->table = NULL;
	ret = file_read(path, &out->table, &out->len);
	if (ret)
		return ret;
	ret = parse_units(out->table, out->len, &out->units);
	if (ret)
		dmar_info_free(out);
	return ret;
}

void dmar_info_free(struct dmar_info *d)
{
	rangelist_free(&d->units);
	free(d->table);
	d->table = NULL;
	d->len = 0;
}
