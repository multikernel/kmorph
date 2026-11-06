![kmorph logo](logo.png)

# kmorph: Transforming Kernels Without Reboots

kmorph transforms a running multikernel Linux machine from one kernel into
another without a reboot. A second kernel, the **successor**, is armed
beside the kernel running the machine, the **predecessor**, and takes the
machine over from it: its CPUs, its memory and its devices.

The takeover happens in two ways:

- **On a crash.** When the predecessor panics or hangs, the successor
  notices the silence, fences the predecessor's CPUs and carries on running
  the machine. No reboot, no operator, about one second from detection to a
  running successor, and the predecessor's memory can be kept as a vmcore
  for post-mortem inspection.
- **On request.** `kmorph upgrade` hands the machine to a successor running
  a new kernel: a kernel upgrade without a reboot.

Either way the successor then becomes the machine's kernel, and can arm a
successor of its own, so a machine can go from kernel to kernel for as many
generations as it runs.

kmorph consists of two programs and one configuration file:

- **kmorph** runs on the predecessor. It builds the successor's image,
  arms, disarms and reports on the successor, keeps the successor's copy of
  the network configuration current, and requests an upgrade.
- **kmorphd** runs inside the successor. It watches the predecessor and
  takes over when the predecessor falls silent or hands over.

kmorph is standalone. It drives the kernel's multikernel interfaces
directly and needs no other tool.

## Contents

- [Requirements](#requirements)
- [Building](#building)
- [Getting started](#getting-started)
- [Upgrading the kernel](#upgrading-the-kernel)
- [Configuration](#configuration)
- [Commands](#commands)
- [Standby services](#standby-services)
- [Network identity](#network-identity)
- [Successor console](#successor-console)
- [Crash dumps](#crash-dumps)
- [What happens at a takeover](#what-happens-at-a-takeover)
- [How it works](#how-it-works)
- [Limitations](#limitations)

## Requirements

- A multikernel Linux kernel built with `CONFIG_MULTIKERNEL=y` and
  `CONFIG_MULTIKERNEL_VSOCKETS=y`, with the host tree interface (the
  `multikernel,host-tree` node in instance-create), on x86-64 or arm64.
  `CONFIG_CRASH_DUMP=y` for registers and VMCOREINFO in the dump.
- In the successor kernel: the multikernel filesystem, the vsock
  transport, and the serial driver for the [console](#successor-console),
  built in or as modules the successor image carries.
- On the predecessor: root, and `gzip`, `xz`, `zstd` or `lz4` if the kernel
  image is a compressed bzImage rather than an ELF vmlinux.
- For [standby services](#standby-services): dracut and erofs-utils on the
  predecessor, and a successor kernel with `CONFIG_EROFS_FS=y` and
  `CONFIG_EROFS_FS_BACKED_BY_FILE=y`.

## Building

```bash
make                 # build/bin/kmorph, build/bin/kmorphd and a static kmorphd
make check           # unit tests
make install         # binaries to $(PREFIX)/bin, default /usr/local
```

The dependencies are libc, musl-gcc for the static kmorphd the successor
runs, and the libfdt sources vendored under lib/fdt. `make install`
honours `PREFIX` and `DESTDIR` and puts an annotated `kmorph.conf.example`
in `/etc/kmorph`.

To build for another architecture, set `CROSS_COMPILE` as for the kernel;
`STATIC=1` links both programs statically, against the cross toolchain's
own libc:

```bash
make CROSS_COMPILE=aarch64-linux-gnu- STATIC=1
```

## Getting started

There are two ways to give the successor its image. The minimal image holds
kmorphd alone, and is the quickest way to see a takeover. The distro image
is the distro's own initramfs with kmorphd inside, which brings the
machine's user space back after the takeover; it is the one to run in
production.

### Minimal image

1. Write `/etc/kmorph/kmorph.conf` on the predecessor:

   ```ini
   cpus    = 1
   memory  = 128MB
   kernel  = /boot/vmlinuz
   cmdline = earlyprintk=serial,ttyS0,115200 keep_bootcon
   devices = enp9s0
   console = ttyS0
   console_login = /usr/local/lib/kmorph/sh
   ```

   The login program here is a static busybox copied under the name `sh`,
   so that it runs as a shell.

2. Build the successor image, once per kmorph install:

   ```
   # kmorph init
   kmorph: successor image written to /var/lib/kmorph/successor.img (2563028 bytes)
   ```

   It holds kmorphd as the image's init and the login program. `kmorph arm`
   adds the config to it each time, so the image never goes stale when the
   config changes. The image has no module loader, so with it the
   successor kernel needs its drivers built in.

3. Arm on the predecessor and check:

   ```
   # kmorph arm
   kmorph: pool initialised with the successor's resources
   kmorph: host tree: 4 CPUs, 2047 MB in 2 ranges, 24 PCI devices
   kmorph: instance successor created with id 1
   kmorph: successor successor armed
   # kmorph status
   successor: active (instance successor, id 1)
   kmorphd: ARMED predecessor=alive last_probe=123ms error=0
   ```

From here the successor probes the predecessor ten times a second. If the
predecessor dies, the successor takes over on its own. To stand down, run
`kmorph disarm`.

### On a distro

The package route is the one kdump uses. `make install PREFIX=/usr`
installs, from `dist/`:

- a dracut module and an initramfs-tools hook that put kmorphd into an
  initramfs;
- `kmorph-mkimage`, which runs whichever of dracut or mkinitramfs the host
  has to write `/var/lib/kmorph/successor.img`, leaving the host's own
  boot initramfs alone;
- `kmorph.service`, which builds the image if it is missing or stale, arms
  at boot, keeps the successor's [network identity](#network-identity)
  current while armed, and disarms on stop;
- a kernel hook, for kernel-install and for Debian's `postinst.d`, that
  rebuilds the image for the kernel just installed and points the `kernel`
  and `cmdline` keys at it.

The hook writes `kernel` and `cmdline`; the operator writes the remaining
keys once:

```ini
cpus    = 47
memory  = 512MB
console = ttyS0
console_login = /bin/sh
```

and enables the service:

```
systemctl enable --now kmorph
```

`memory` must hold an unpacked distro initramfs; `kmorph arm` warns when it
cannot. `console_login` needs no static program here, since the initramfs
has libc. `kmorph init` is not involved.

In the successor, the initramfs's init starts kmorphd and holds its boot on
`kmorphd --wait` until the takeover is done. The successor has then adopted
every device of the predecessor, so the initramfs mounts the root disk and
starts the system's own init, which brings the network, the mounts and the
services back as at any boot: the machine continues as itself, its user
space restarted on a kernel already running. That init starts
`kmorph.service` too, which arms the next successor.

## Upgrading the kernel

`kmorph upgrade` hands the machine to the armed successor while the
predecessor is healthy. On a distro, the whole upgrade is:

```
# dnf upgrade kernel           # or apt; the kernel hook rebuilds the image
# systemctl restart kmorph     # arm a successor running the new kernel
# kmorph upgrade
```

`kmorph upgrade` refuses when no successor is armed, or when another kernel
runs under this one and would lose its host. Otherwise it sends kmorphd the
latest network identity, syncs, and freezes every writable filesystem on a
block device, so that the successor mounts them with nothing in flight and
no journal to replay. It then asks kmorphd to take over.

kmorphd accepts only when it is armed and its last probe found the
predecessor alive, so a handover cannot race a takeover that already
suspects a crash. Once it accepts, the takeover runs as after a crash,
except that nothing crashed and no dump is written. On success the
predecessor is fenced and the command never returns. If no fence comes
within ten seconds, the command thaws the filesystems and fails, and the
predecessor stays the machine's kernel.

The successor's `kmorph status` then reports `predecessor=handed-over`.

## Configuration

The file is `/etc/kmorph/kmorph.conf`, `key = value`, one per line, `#`
starts a comment. Both programs read the same file; each uses the keys for
its side. `kernel`, `cpus` and `memory` are required to arm, everything
else has a default. `kmorph.conf.example` lists every key.

### Successor (used by kmorph arm)

| Key | Default | Meaning |
|---|---|---|
| `name` | `successor` | Instance name in `/sys/fs/multikernel/instances`. |
| `cpus` | | CPUs for the successor, as physical ids the kernel uses (APIC ids on x86, MPIDRs on arm64), e.g. `1` or `12-15,20`. |
| `memory` | | Memory for the successor, e.g. `128MB`, `4GB`. |
| `kernel` | | Kernel image: an ELF vmlinux, or a bzImage from which the vmlinux is extracted; an Image on arm64. |
| `initrd` | `/var/lib/kmorph/successor.img` | Initramfs for the successor; the default is where `kmorph init` and `kmorph-mkimage` write. kmorph appends the config to whichever image is used. |
| `cmdline` | | Kernel command line for the successor. See [Successor console](#successor-console). With `dump` set, kmorph adds `iomem=relaxed`. |
| `devices` | | Devices handed to the successor, comma separated, as PCI addresses (`0000:09:00.0`) or as the names sysfs knows them by: a network interface (`enp61s0f1`), a block device (`nvme0n1`), a framebuffer (`fb0`), a USB bus (`usb1`). A name resolves to the PCI function behind it. The successor owns these from boot, a NIC to be reachable while armed for instance; it adopts every other device at the takeover. |
| `machine_cpus` | from the firmware | Every CPU on the machine. Set only on a machine with neither ACPI nor a firmware device tree. |
| `modules` | | Kernel modules the distro image builders add to the successor image, comma separated: the vsock transport when it is a module, drivers for devices in `devices`. Ignored by the `kmorph init` image, which has no module loader. |
| `standby` | | systemd units the successor runs before the takeover, comma separated. See [Standby services](#standby-services). |
| `standby_files` | | Further files or directories the standby image carries, such as plugins from a package other than the unit's. |
| `console` | | Serial line the successor takes over after the takeover, e.g. `ttyS0`. |
| `console_baud` | `115200` | Speed of that line. |
| `console_login` | | Program run on the serial line after the takeover, required with `console`. With the `kmorph init` image it must be a static executable, which `kmorph init` copies in; any other image carries its own. |

### Detection and takeover (used by kmorphd)

| Key | Default | Meaning |
|---|---|---|
| `probe_interval` | `100ms` | How often the predecessor is probed. A probe unanswered by the next one has timed out. |
| `probe_timeouts` | `5` | Consecutive timeouts before the predecessor is declared dead. |
| `fence_retries` | `3` | Retries of a fence that could not park every CPU. |
| `dump` | | File to receive the predecessor's memory as a vmcore before it is reclaimed. A FIFO works. Unset: reclaim at once. See [Crash dumps](#crash-dumps). |
| `reset_devices` | `yes` | Reset each adopted device before its driver binds, through its `reset` file in sysfs, or clear Bus Master Enable on a function the kernel has no way to reset. `no` leaves each device as the crash left it, for its driver to probe. |

## Commands

### kmorph, on the predecessor

```
kmorph init    [--config PATH]
kmorph arm     [--config PATH]
kmorph disarm  [--config PATH]
kmorph status  [--config PATH]
kmorph watch   [--config PATH]
kmorph upgrade [--config PATH]
```

**init** writes the minimal successor image, `/var/lib/kmorph/successor.img`:
an initramfs holding the kmorphd installed beside kmorph as its init and,
when `console` is set, the `console_login` program at its own path. Both
must be static executables, since the image has no libc; init refuses a
dynamic one. Run it once after installing kmorph and again after upgrading
it or changing `console_login`. The config is not in the image.

**arm** prepares the machine and boots the successor. If the machine has no
multikernel pool yet, kmorph mounts `/sys/fs/multikernel` and creates a
pool holding the successor's CPUs, its memory plus one memory granule of
headroom for the kernel's own use, its devices, and the serial device when
`console` is set. If a pool exists, kmorph uses it and adds any configured
PCI device the pool lacks. It then creates the instance, loads the kernel
and the image with the config appended to it, and boots the successor. The
config is appended as a second cpio archive the kernel unpacks after the
first, so the successor always boots with the config arm just read. arm
warns when the image is older than a file it was built from. The whole
command takes about a second.

**disarm** halts the successor, unloads its image and removes the instance,
returning its resources to the pool.

**status** prints the instance state from the kernel and, over vsock, the
daemon's own state:

```
successor: active (instance successor, id 1)
kmorphd: ARMED predecessor=alive last_probe=12ms error=0
```

**watch** stays in the foreground, listens to rtnetlink and sends the
successor the machine's [network identity](#network-identity) whenever it
changes. `kmorph.service` runs it once armed.

**upgrade** hands the machine over to the armed successor. See
[Upgrading the kernel](#upgrading-the-kernel).

### kmorphd, in the successor

```
kmorphd [--config PATH] [--foreground | --wait]
```

kmorphd refuses to run in a kernel that was not armed by kmorph, since it
finds nothing to take over. As the image's init, which is how `kmorph init`
packs it, it mounts proc, sysfs, devtmpfs and a tmpfs on `/run`, stays in
the foreground and reaps orphans. Started by another init it daemonises
unless `--foreground` is given; with `--wait` the command returns only once
the takeover is done, or fails when kmorphd stops before, so that an
initramfs can hold its boot on it.

Either way it logs to the kernel log and writes its state to
`/run/kmorph/state`:

```
TAKEN_OVER predecessor=silent last_probe=181ms error=0
```

The first word is the state, `predecessor` is the last probe result
(`alive`, `silent` or `handed-over`), `last_probe` the time since it,
`error` the errno of the last failed step or 0. The states are ARMED,
SUSPECT (one or more timeouts), FENCING, FENCE_FAILED, FENCED and
TAKEN_OVER. Once the takeover is done, kmorphd also creates
`/run/kmorph/taken-over`, for whatever runs next to wait for.

## Standby services

Restarting user space after the takeover costs as long as the slowest
service takes to start. Services named in `standby` are instead started in
the successor while it is armed, and keep running through the takeover:

```ini
standby = nginx.service
```

`kmorph-mkimage` then writes a different image: a standby root, built with
dracut from the host's own files, holding systemd, each unit with its
drop-ins, its programs and their libraries, everything the packages of the
unit and its main program ship, `/etc`, and `standby_files`, packed as an
EROFS image beside kmorphd.

kmorphd, as the successor's init, mounts the image under a writable layer
in memory, makes it the root, allows `ip_nonlocal_bind`, and forks: PID 1
becomes the image's systemd, which brings up the standby services, and the
child goes on probing the predecessor without a gap. The successor owns no
disk and no NIC at this point, so a standby service must start without
either: it binds its ports, the machine's addresses included, and what it
writes stays in memory.

At the takeover, a path unit on `/run/kmorph/taken-over` starts
`kmorph-switch.service`, which mounts the root named by `root=` on the
successor's command line, moves every process onto it with `pivot_root`,
re-executes systemd from the disk and starts `default.target`. The disk's
own units bring the rest of the machine up; the standby services were never
stopped, and they are reloaded so they reopen their logs on the disk.

Constraints:

- A standby service must neither write state that has to persist nor reach
  the network before the takeover. kmorph cannot check that, and the
  service should not be one that does.
- A unit that runs in a mount namespace of its own (`PrivateTmp=`,
  `ProtectSystem=` and the like) is not reached by `pivot_root`.
  `kmorph-mkimage` warns about it and `kmorph-switch` restarts it after the
  takeover, which costs it the warm start unless a drop-in relaxes its
  sandbox.
- The root must be a plain partition or disk with `/usr` on it.
- `memory` must hold the image and what the standby services use.

The image is a snapshot: `kmorph-mkimage` records the files it copied in
`successor.img.files`, `kmorph.service` rebuilds the image when one of
them changes, and `kmorph arm` warns about a stale image.

## Network identity

The machine is reachable again only once the adopted NICs have their
addresses back, and waiting for the disk's network manager to start, and
for DHCP, takes seconds. kmorph therefore records the name, MAC, MTU,
addresses with their remaining lease lifetimes, and routes of every PCI
network interface it does not hand to the successor. Right after adopting
the devices, before it even adopts the memory, kmorphd puts them back on
the same PCI functions over rtnetlink, without duplicate address
detection: the addresses were the machine's a moment ago. The MAC does not
change, so neighbours need no gratuitous ARP. A lease that ran out in
between is not restored.

A crash leaves no time to send the record, so it has to be in the
successor beforehand and current. `kmorph arm` forwards it beside the
config, and `kmorph watch` sends it again whenever an interface, address or
route changes, a DHCP renewal included. `kmorph.service` runs `kmorph
watch` once it armed; without the service, run it after `kmorph arm`.
`kmorph upgrade` sends it once more right before the handover.

The network manager on the disk then starts with the interfaces already
configured, and has to configure them as on any boot to take their leases
over; a DHCP server hands the same client the same address, so the address
never goes away. systemd-networkd does that by default. NetworkManager
instead leaves configured interfaces alone as external ones and renews no
lease on them; `kmorph-nm-handback.service`, enabled on a NetworkManager
host, runs before it only when kmorphd restored interfaces
(`/run/kmorph/network-restored`) and gives it a runtime drop-in with
`keep-configuration=no` for just those interfaces.

A physical NIC still renegotiates its link after the reset that
`reset_devices` gives it, which can take a second or more.

## Successor console

When `console` names a serial line, `kmorph arm` places the legacy serial
device in the pool and hands it to the successor, whose kernel registers it
at boot. After the takeover kmorphd switches the line to polling, since the
successor has no interrupt routing for it, and keeps the login program on
it, so the serial console that showed the predecessor now shows the
successor's shell.

Give the successor an early serial console on the same line in its
`cmdline`, `earlyprintk=serial,ttyS0,115200 keep_bootcon`, so its kernel
messages reach the serial console before and after the takeover. Do not use
`console=mktty0`: that console forwards messages to the predecessor and,
once the predecessor is dead, floods the successor with its own failure
reports.

On the serial console the whole event is one continuous stream: the
predecessor's messages, the successor's boot lines interleaved once armed,
the panic, the takeover, and the successor's login prompt.

## Crash dumps

With `dump` set, the predecessor's memory is written to that file before it
is reclaimed. The file is an ELF core in the format of kdump's
`/proc/vmcore`: one segment per memory range, the registers of every CPU at
the moment it stopped, and the crashed kernel's VMCOREINFO, so `crash` and
`makedumpfile` read it directly. Open it with the predecessor's vmlinux:

```
crash /boot/vmlinux /var/crash/predecessor.vmcore
crash> log
crash> bt -a
```

It is the size of the memory the successor took over, sparse where pages
were zero. To shrink it, filter and compress it afterwards; the embedded
VMCOREINFO lets makedumpfile do that without the vmlinux:

```
makedumpfile -c -d 31 predecessor.vmcore predecessor.small
```

A successor with a NIC but no disk can stream the dump instead of storing
it: make `dump` a FIFO and feed it to the network from the successor's
init, and the whole core leaves the machine as it is written:

```
mkfifo /tmp/vmcore
nc collector 9999 < /tmp/vmcore &
```

with `dump = /tmp/vmcore`. Zero pages are sent as zeros on a FIFO. Should
the reader go away, the takeover goes on as for any failed dump.

The successor reads the memory through `/dev/mem`. `kmorph arm` boots it
with `iomem=relaxed`, so that `IO_STRICT_DEVMEM` does not get in the way,
and the successor must not be booted locked down. If the dump fails, the
memory stays unclaimed and readable rather than being destroyed. An
upgrade writes no dump.

## What happens at a takeover

1. **Detection.** Probes time out `probe_timeouts` times in a row; with the
   defaults that is half a second. On an upgrade, the predecessor's
   handover request takes the place of the silence.
2. **Fence.** The successor kernel stops every CPU it does not own and
   confirms each one stopped. A CPU that never stops fails the fence, which
   is retried on the next probe up to `fence_retries` times; the takeover
   never proceeds with a CPU possibly still running the dead kernel.
3. **Devices.** Every PCI function the predecessor described in the host
   tree at arm time that the successor does not already own, bridges aside,
   is adopted in a transaction of its own, so one that cannot be taken
   holds up neither the others nor the takeover. The successor's kernel
   puts each on a root bus of its own and reaches its config space through
   the bus's ECAM window. With `reset_devices`, kmorphd holds drivers off
   the bus while the devices join, resets them all at once through sysfs,
   by FLR or a power state cycle, and clears Bus Master Enable on any it
   cannot reset, so that whatever DMA the predecessor left running stops
   before its memory is hot-added; only then are drivers probed, each
   meeting a clean device.
4. **IOMMUs.** On a machine whose IOMMUs still translate DMA through the
   dead host's page tables, kmorphd holds drivers off and clears Bus Master
   Enable even without `reset_devices`. Once every function is taken and
   quiet, it claims each unit its kernel describes under
   `/chosen/multikernel,iommus`: the successor's kernel switches the unit
   to tables of its own, with identity entries for its devices, and to the
   interrupt remapping it already kept in step with the host's. A function
   left untaken or live keeps the unit the dead host's.
5. **Network.** kmorphd restores the [network identity](#network-identity)
   on the adopted NICs.
6. **CPUs and memory.** The successor computes what is takeable, the
   machine's RAM minus its own minus the kernel's control regions, and
   every CPU it does not own, and claims it in one transaction. Fenced CPUs
   are woken; memory is hot-added in units of the `memory-granule` the
   kernel publishes in its multikernel device tree, 4 MB on x86, or in
   whole memory blocks, 128 MB on x86, from a kernel that does not.
7. **Dump and reclaim.** With `dump` set, memory is held back until it has
   been written to the dump file, then reclaimed.
8. **Console.** With `console` set, the login program is started on the
   serial line.

### What the successor ends up with

Every CPU, and all memory that forms whole memory granules. The granules
that contain the first megabyte, the top of RAM, the successor's own memory
or one of the kernel's control regions cannot be hot-added and stay
unclaimed; at 4 MB that is a few megabytes around each, at whole memory
blocks on a 2 GB machine about 500 MB. Unclaimed memory remains readable
through `/dev/mem`.

The successor keeps the devices it was armed with and gains every other PCI
function of the machine but the bridges. Each sits on a root bus of its own
with nothing of the bridges above it, so the successor has no secondary bus
reset, hotplug or AER for it. A device the machine gained after
`kmorph arm` is not in the host tree and stays as the crash left it.

### The next generation

Takeover is one-shot: afterwards the successor is the machine's kernel, and
is itself unprotected until a new successor is armed from it. `kmorph arm`
works there as on the first kernel; a kernel that took over booted from a
host tree rather than from ACPI, so arm describes the machine from that
host tree.

The memory a successor adopts becomes its kernel's own, and kernel memory
cannot be hot-removed once a page of it is pinned. So kmorphd keeps the
next successor's pool out of the adoption: as much memory as `memory` names
plus one granule, from the top of the largest adoptable range, recorded in
`/run/kmorph/pool-memory`. The next `kmorph arm` builds its pool from that
range, and every generation starts out as the first one did.

## How it works

Everything novel is in the kernel. kmorph is orchestration over the
kernel's multikernel interfaces: the `/sys/fs/multikernel` filesystem for
the pool and instances, `kexec_file_load()` and `reboot()` to load, boot,
halt and fence, and vsock for liveness and requests.

### Arming

`kmorph arm` runs three kernel operations on the predecessor:

1. **Create.** An overlay transaction written to
   `/sys/fs/multikernel/overlays/new` creates the instance with the
   successor's CPUs, memory and devices. The same transaction carries the
   host tree, a `chosen { multikernel,host-tree { ... } }` node naming every
   CPU on the machine, from the ACPI MADT, the firmware device tree or the
   kernel's own host tree, the RAM map from `/proc/iomem` and the PCI
   inventory from sysfs. With `dump` set, the tree also carries a `vmcore`
   node with where the kernel keeps its vmcoreinfo, its per-CPU crash note
   buffers and its direct map; the node also tells the predecessor kernel
   to save each CPU's registers when it stops, so the dump carries them.
   When the machine has no pool yet, a baseline written to
   `/sys/fs/multikernel/device_tree` creates one first.
2. **Load.** `kexec_file_load()` in multikernel mode loads the kernel and
   the initramfs for that instance. The initramfs is the image with the
   config and the network identity appended, assembled in memory; nothing
   on disk is changed.
3. **Exec.** The multikernel `reboot()` command boots it. At that moment
   the predecessor kernel writes the successor's boot device tree: the host
   tree goes into the successor's `/chosen` as it was given, and beside it
   the kernel adds what only it knows, the control regions the successor
   must never treat as free memory (`multikernel,reserved-memory`), the
   IOMMU units, and the park slot where fenced CPUs will wait.

The successor kernel registers every CPU named in the host tree at boot,
because a CPU can only be fenced or hot-added if it was enumerated then.
Everything the successor will need is therefore fixed before it boots; only
the network identity is refreshed at runtime.

### Watching

kmorphd probes the predecessor with a vsock `connect()` to CID 0 on a port
nobody listens on. A live predecessor kernel answers RST by itself, with no
user space involved, which the successor sees as `ECONNRESET`. Silence, a
probe still unanswered when the next one is due, is the only evidence of
death, and only `probe_timeouts` of them in a row; a probe the kernel
refuses at once because the host stopped answering counts as one. A slow
predecessor is therefore never mistaken for a dead one.

kmorphd also listens on vsock for the predecessor's requests: `status`,
the network identity, and the handover of `kmorph upgrade`.

### Takeover

From the successor's point of view the machine splits in two: what it
owns, tracked by its own accounting, and everything else, which after the
fence is one bag it absorbs regardless of how the predecessor had divided
it.

1. **Fence.** `reboot()` with the multikernel force-halt command and
   `mk_id 0`. The successor kernel stops every CPU it does not own, by NMI and
   INIT on x86 and by a stop IPI and PSCI on arm64, and confirms each
   target stopped; it returns an error rather than a partial fence.
2. **Adopt.** kmorphd reads the host tree and the reserved regions back
   from its own `/proc/device-tree`, subtracts its own RAM from the
   machine's, subtracts the reserved regions and the next pool, rounds to
   whole granules, and applies overlays against its own instance node with
   `device-add`, `iommu-add`, `cpu-add` and `memory-add` items. The kernel
   wakes fenced CPUs through the predecessor's park slot and hot-adds the
   memory.
3. **Reap.** With a dump configured, memory is held back from that overlay,
   written as a vmcore through `/dev/mem`, then adopted by a second overlay.

### The daemon's states

```
ARMED ── timeout ──▶ SUSPECT ── ECONNRESET ──▶ ARMED
  │                     │
  │ handover            ├─ probe_timeouts in a row
  ▼                     ▼
  └────────────────▶ FENCING ── lost CPU ──▶ FENCE_FAILED ──(retry)──▶ FENCING
                        │
                        ▼
                     FENCED ── adopt, dump, reap ──▶ TAKEN_OVER
```

Every transition is logged and written to `/run/kmorph/state`. A failed
adoption is retried on the next probe event; the fence is idempotent and
retried a bounded number of times.

## Limitations

- No automatic re-arming after a takeover, except through `kmorph.service`
  on a distro.
- The successor has no secondary bus reset for an adopted device, only FLR
  and power state resets; a function with neither is only quiesced.
- Memory is adopted in whole granules; the slivers around the successor's
  own memory and the control regions stay unclaimed.
- The dump is not filtered; makedumpfile can shrink it afterwards.
