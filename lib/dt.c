#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libfdt.h"
#include "kmorph/dt.h"
#include "kmorph/fdtutil.h"

#define OVERLAY_COMPATIBLE "linux,multikernel-overlay"
#define INSTANCES_PATH "/instances"
#define POOL_PATH "/resources"
#define PCI_HOST_BRIDGE_COMPATIBLE "multikernel,pci-host-bridge"

/* Run a sequence of libfdt calls, stopping at the first error. */
#define TRY(expr) do { int _r = (expr); if (_r) return _r; } while (0)

static int fragment_begin(void *fdt, int index, const char *target_path)
{
	char node[32];

	snprintf(node, sizeof(node), "fragment@%d", index);
	TRY(fdt_begin_node(fdt, node));
	TRY(fdt_property_string(fdt, "target-path", target_path));
	return fdt_begin_node(fdt, "__overlay__");
}

static int fragment_end(void *fdt)
{
	TRY(fdt_end_node(fdt));	/* __overlay__ */
	return fdt_end_node(fdt);	/* fragment */
}

static int overlay_begin(void *fdt, const char *target_path)
{
	TRY(fdt_begin_node(fdt, ""));
	TRY(fdt_property_string(fdt, "compatible", OVERLAY_COMPATIBLE));
	return fragment_begin(fdt, 0, target_path);
}

static int overlay_end(void *fdt)
{
	TRY(fragment_end(fdt));
	return fdt_end_node(fdt);	/* root */
}

struct instance_args {
	const char *name;
	const struct cpulist *cpus;
	uint64_t memory_bytes;
	const struct strlist *devices;
	const struct strlist *platform_devices;
	const struct host_tree *host_tree;
};

static int emit_platform_device_nodes(void *fdt, const struct strlist *names);

static int emit_platform_devices(void *fdt, const struct strlist *names)
{
	TRY(fdt_begin_node(fdt, "devices"));
	TRY(emit_platform_device_nodes(fdt, names));
	return fdt_end_node(fdt);
}

static int emit_device_add(void *fdt, int fragment, const char *target,
			   const struct strlist *devices)
{
	size_t i;

	TRY(fragment_begin(fdt, fragment, target));
	TRY(fdt_begin_node(fdt, "device-add"));
	for (i = 0; i < devices->count; i++) {
		char node[32];

		snprintf(node, sizeof(node), "pci@%zu", i);
		TRY(fdt_begin_node(fdt, node));
		TRY(fdt_property_string(fdt, "pci-id", devices->items[i]));
		TRY(fdt_end_node(fdt));
	}
	TRY(fdt_end_node(fdt));
	return fragment_end(fdt);
}

static int build_instance_create(void *fdt, void *arg)
{
	const struct instance_args *a = arg;

	TRY(overlay_begin(fdt, INSTANCES_PATH));
	TRY(fdt_begin_node(fdt, "instance-create"));
	TRY(fdt_property_string(fdt, "instance-name", a->name));
	TRY(fdt_begin_node(fdt, "resources"));
	TRY(fdtutil_prop_cpulist(fdt, "cpus", a->cpus));
	TRY(fdt_property_u64(fdt, "memory-bytes", a->memory_bytes));
	if (a->platform_devices && a->platform_devices->count)
		TRY(emit_platform_devices(fdt, a->platform_devices));
	TRY(fdt_end_node(fdt));	/* resources */
	if (a->host_tree) {
		TRY(fdt_begin_node(fdt, "chosen"));
		TRY(fdt_begin_node(fdt, HOST_TREE_NODE));
		TRY(host_tree_emit(fdt, a->host_tree));
		TRY(fdt_end_node(fdt));
		TRY(fdt_end_node(fdt));
	}
	TRY(fdt_end_node(fdt));	/* instance-create */
	TRY(fragment_end(fdt));
	/* Devices join by a second fragment against the instance the first one creates. */
	if (a->devices && a->devices->count) {
		char target[256];

		snprintf(target, sizeof(target), INSTANCES_PATH "/%s", a->name);
		TRY(emit_device_add(fdt, 1, target, a->devices));
	}
	return fdt_end_node(fdt);	/* root */
}

int dt_build_instance_create(const char *name, const struct cpulist *cpus,
			     uint64_t memory_bytes, const struct strlist *devices,
			     const struct strlist *platform_devices,
			     const struct host_tree *host_tree,
			     void **dtbo, size_t *len)
{
	struct instance_args a = { name, cpus, memory_bytes, devices, platform_devices,
				   host_tree };

	if (!cpus->count || !memory_bytes)
		return -EINVAL;
	return fdtutil_build(build_instance_create, &a, dtbo, len);
}

static int build_instance_remove(void *fdt, void *arg)
{
	TRY(overlay_begin(fdt, INSTANCES_PATH));
	TRY(fdt_begin_node(fdt, "instance-remove"));
	TRY(fdt_property_string(fdt, "instance-name", arg));
	TRY(fdt_end_node(fdt));
	return overlay_end(fdt);
}

int dt_build_instance_remove(const char *name, void **dtbo, size_t *len)
{
	return fdtutil_build(build_instance_remove, (void *)name, dtbo, len);
}

struct adopt_args {
	const char *name;
	const struct cpulist *cpus;
	const struct rangeset *memory;
};

static int emit_cpu_add(void *fdt, const struct cpulist *cpus)
{
	size_t i;

	TRY(fdt_begin_node(fdt, "cpu-add"));
	for (i = 0; i < cpus->count; i++) {
		char node[32];

		snprintf(node, sizeof(node), "cpu@%llu", (unsigned long long)cpus->ids[i]);
		TRY(fdt_begin_node(fdt, node));
		TRY(fdt_property_u64(fdt, "reg", cpus->ids[i]));
		TRY(fdt_end_node(fdt));
	}
	return fdt_end_node(fdt);
}

static int emit_memory_add(void *fdt, const struct rangeset *memory)
{
	size_t i;

	TRY(fdt_begin_node(fdt, "memory-add"));
	for (i = 0; i < memory->count; i++) {
		char node[32];

		snprintf(node, sizeof(node), "memory@%zu", i);
		TRY(fdt_begin_node(fdt, node));
		TRY(fdtutil_prop_reg(fdt, memory->r[i].base, memory->r[i].size));
		TRY(fdt_end_node(fdt));
	}
	return fdt_end_node(fdt);
}

static int build_adopt(void *fdt, void *arg)
{
	const struct adopt_args *a = arg;
	char target[256];

	snprintf(target, sizeof(target), INSTANCES_PATH "/%s", a->name);
	TRY(overlay_begin(fdt, target));
	if (a->cpus->count)
		TRY(emit_cpu_add(fdt, a->cpus));
	if (a->memory->count)
		TRY(emit_memory_add(fdt, a->memory));
	return overlay_end(fdt);
}

int dt_build_adopt(const char *name, const struct cpulist *cpus,
		   const struct rangeset *memory, void **dtbo, size_t *len)
{
	struct adopt_args a = { name, cpus, memory };

	if (!cpus->count && !memory->count)
		return -EINVAL;
	return fdtutil_build(build_adopt, &a, dtbo, len);
}

struct pool_args {
	const struct cpulist *cpus;
	uint64_t memory_bytes;
	const struct pci_list *devices;
	const struct strlist *platform_devices;
};

static int emit_platform_device_nodes(void *fdt, const struct strlist *names)
{
	size_t i;

	for (i = 0; i < names->count; i++) {
		TRY(fdt_begin_node(fdt, names->items[i]));
		TRY(fdt_property_string(fdt, "device-type", "platform"));
		TRY(fdt_property_string(fdt, "device-name", names->items[i]));
		TRY(fdt_end_node(fdt));
	}
	return 0;
}

static int emit_pci_device_nodes(void *fdt, const struct pci_list *devices)
{
	size_t i;

	for (i = 0; i < devices->count; i++) {
		const struct pci_ids *d = &devices->devs[i];
		char node[32], *p;

		snprintf(node, sizeof(node), "pci_%s", d->id);
		for (p = node; *p; p++)
			if (*p == ':' || *p == '.')
				*p = '_';
		TRY(fdt_begin_node(fdt, node));
		TRY(fdt_property_string(fdt, "device-type", "pci"));
		TRY(fdt_property_string(fdt, "pci-id", d->id));
		TRY(fdt_property_u32(fdt, "vendor-id", d->vendor));
		TRY(fdt_property_u32(fdt, "device-id", d->device));
		TRY(fdt_end_node(fdt));
	}
	return 0;
}

int dt_emit_pci_devices(void *fdt, const struct pci_list *devices)
{
	TRY(fdt_begin_node(fdt, "devices"));
	TRY(emit_pci_device_nodes(fdt, devices));
	return fdt_end_node(fdt);
}

static int build_pool_baseline(void *fdt, void *arg)
{
	const struct pool_args *a = arg;

	TRY(fdt_begin_node(fdt, ""));
	TRY(fdt_begin_node(fdt, "resources"));
	TRY(fdtutil_prop_cpulist(fdt, "cpus", a->cpus));
	TRY(fdt_begin_node(fdt, "memory@0"));
	TRY(fdt_property_string(fdt, "device_type", "memory"));
	TRY(fdt_property_u64(fdt, "size", a->memory_bytes));
	TRY(fdt_end_node(fdt));
	if ((a->devices && a->devices->count) ||
	    (a->platform_devices && a->platform_devices->count)) {
		TRY(fdt_begin_node(fdt, "devices"));
		if (a->devices)
			TRY(emit_pci_device_nodes(fdt, a->devices));
		if (a->platform_devices)
			TRY(emit_platform_device_nodes(fdt, a->platform_devices));
		TRY(fdt_end_node(fdt));
	}
	TRY(fdt_end_node(fdt));
	return fdt_end_node(fdt);
}

int dt_build_pool_baseline(const struct cpulist *cpus, uint64_t memory_bytes,
			   const struct pci_list *devices,
			   const struct strlist *platform_devices, void **dtb, size_t *len)
{
	struct pool_args a = { cpus, memory_bytes, devices, platform_devices };

	if (!cpus->count || !memory_bytes)
		return -EINVAL;
	return fdtutil_build(build_pool_baseline, &a, dtb, len);
}

static int build_pool_device_add(void *fdt, void *arg)
{
	TRY(fdt_begin_node(fdt, ""));
	TRY(fdt_property_string(fdt, "compatible", OVERLAY_COMPATIBLE));
	TRY(emit_device_add(fdt, 0, POOL_PATH, arg));
	return fdt_end_node(fdt);
}

int dt_build_pool_device_add(const struct strlist *devices, void **dtbo, size_t *len)
{
	if (!devices->count)
		return -EINVAL;
	return fdtutil_build(build_pool_device_add, (void *)devices, dtbo, len);
}

/*
 * The foreign baseline names the fenced predecessor's resources by
 * physical CPU id and explicit reg, the shape the kernel-side design
 * specifies for a pool built from another kernel's memory.
 */
static int build_foreign_baseline(void *fdt, void *arg)
{
	const struct takeable *t = arg;
	size_t i;

	TRY(fdt_begin_node(fdt, ""));
	TRY(fdt_begin_node(fdt, "resources"));
	TRY(fdtutil_prop_cpulist(fdt, "cpus", &t->cpus));
	for (i = 0; i < t->memory.count; i++) {
		char node[32];

		snprintf(node, sizeof(node), "memory@%zu", i);
		TRY(fdt_begin_node(fdt, node));
		TRY(fdt_property_string(fdt, "device_type", "memory"));
		TRY(fdtutil_prop_reg(fdt, t->memory.r[i].base, t->memory.r[i].size));
		TRY(fdt_end_node(fdt));
	}
	TRY(fdt_end_node(fdt));
	return fdt_end_node(fdt);
}

int dt_build_foreign_baseline(const struct takeable *t, void **dtb, size_t *len)
{
	if (!t->cpus.count || !t->memory.count)
		return -EINVAL;
	return fdtutil_build(build_foreign_baseline, (void *)t, dtb, len);
}

static uint64_t sum_chunks(const void *fdt, int resources)
{
	uint64_t total = 0;
	int node;

	fdt_for_each_subnode(node, fdt, resources) {
		const char *name = fdt_get_name(fdt, node, NULL);
		uint64_t base, size;

		if (name && !strncmp(name, "memory@", 7) &&
		    !fdtutil_read_reg(fdt, node, &base, &size))
			total += size;
	}
	return total;
}

static uint64_t sum_instance_memory(const void *fdt)
{
	uint64_t total = 0;
	int instances, node;

	instances = fdt_path_offset(fdt, INSTANCES_PATH);
	if (instances < 0)
		return 0;
	fdt_for_each_subnode(node, fdt, instances) {
		int res = fdt_subnode_offset(fdt, node, "resources");
		uint64_t bytes;

		if (res >= 0 && !fdtutil_read_u64(fdt, res, "memory-bytes", &bytes))
			total += bytes;
	}
	return total;
}

static int strlist_push(struct strlist *l, const char *s)
{
	char **items = realloc(l->items, (l->count + 1) * sizeof(*items));

	if (!items)
		return -ENOMEM;
	l->items = items;
	items[l->count] = strdup(s);
	if (!items[l->count])
		return -ENOMEM;
	l->count++;
	return 0;
}

/*
 * The kernel describes pooled PCI devices as a bus topology under each
 * host bridge: bridges carry a bus-range, leaves a vendor-id, and reg's
 * first cell holds bus and devfn as in the PCI binding.
 */
static int collect_pci_leaves(const void *fdt, int parent, unsigned int domain,
			      struct strlist *out)
{
	int node;

	fdt_for_each_subnode(node, fdt, parent) {
		const fdt32_t *reg;
		int len, ret;

		if (fdt_getprop(fdt, node, "vendor-id", NULL)) {
			char id[16];
			uint32_t hi;

			reg = fdt_getprop(fdt, node, "reg", &len);
			if (!reg || len < (int)sizeof(*reg))
				return -EINVAL;
			hi = fdt32_to_cpu(reg[0]);
			snprintf(id, sizeof(id), "%04x:%02x:%02x.%x", domain,
				 (hi >> 16) & 0xff, (hi >> 11) & 0x1f, (hi >> 8) & 0x7);
			ret = strlist_push(out, id);
		} else {
			ret = collect_pci_leaves(fdt, node, domain, out);
		}
		if (ret)
			return ret;
	}
	return 0;
}

static int collect_pool_devices(const void *fdt, int parent, struct strlist *out,
				struct strlist *platform)
{
	int node;

	fdt_for_each_subnode(node, fdt, parent) {
		const char *type = fdt_getprop(fdt, node, "device-type", NULL);
		int ret;

		if (type && !strcmp(type, "platform")) {
			const char *name = fdt_getprop(fdt, node, "device-name", NULL);

			ret = strlist_push(platform, name ? name : fdt_get_name(fdt, node, NULL));
		} else if (!fdt_node_check_compatible(fdt, node, PCI_HOST_BRIDGE_COMPATIBLE)) {
			const fdt32_t *dom = fdt_getprop(fdt, node, "linux,pci-domain", NULL);

			ret = collect_pci_leaves(fdt, node, dom ? fdt32_to_cpu(*dom) : 0, out);
		} else {
			ret = collect_pool_devices(fdt, node, out, platform);
		}
		if (ret)
			return ret;
	}
	return 0;
}

int dt_read_pool(const void *fdt, size_t len, struct pool_view *v)
{
	uint64_t used;
	int resources, ret;

	memset(v, 0, sizeof(*v));
	if (fdtutil_check(fdt, len))
		return -EINVAL;
	resources = fdt_path_offset(fdt, "/resources");
	if (resources < 0)
		return -ENOENT;

	ret = fdtutil_read_cpulist(fdt, resources, "cpus", &v->free_cpus);
	if (ret == -ENOENT)
		ret = 0;
	if (ret)
		return ret;

	/* A kernel with no pool still describes itself under /resources; chunks mean a pool. */
	v->total_bytes = sum_chunks(fdt, resources);
	if (!v->total_bytes) {
		pool_view_free(v);
		return -ENOENT;
	}
	used = sum_instance_memory(fdt);
	v->free_bytes = used < v->total_bytes ? v->total_bytes - used : 0;

	ret = collect_pool_devices(fdt, resources, &v->devices, &v->platform_devices);
	if (ret)
		pool_view_free(v);
	return ret;
}

void pool_view_free(struct pool_view *v)
{
	cpulist_free(&v->free_cpus);
	strlist_free(&v->devices);
	strlist_free(&v->platform_devices);
}
