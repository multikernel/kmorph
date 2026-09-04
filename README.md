![kmorph logo](logo.png)

# kmorph: Transforming Kernels Without Reboots

kmorph is crash auto-healing for multikernel Linux. A backup kernel, the
**successor**, is armed beside the kernel running the machine, the
**predecessor**. When the predecessor crashes, the successor fences its
CPUs, adopts its CPUs and memory, and carries on running the machine. No
reboot, no operator, about one second from detection to a running
successor, and the predecessor's memory can be preserved for post-mortem
inspection.

kmorph consists of two programs and one configuration file:

- **kmorph** runs on the predecessor and arms, disarms or reports on a
  successor. It exits when done; nothing stays resident on the predecessor.
- **kmorphd** runs inside the successor. It watches the predecessor and
  takes over when the predecessor falls silent.

kmorph is standalone. It drives the kernel's multikernel interfaces
directly and needs no other tool.

## Requirements

- A multikernel Linux kernel built with `CONFIG_MULTIKERNEL=y` and
  `CONFIG_MULTIKERNEL_VSOCKETS=y`, with the host tree interface (the
  `multikernel,host-tree` node in instance-create). x86-64 only.
  `CONFIG_CRASH_DUMP=y` for registers and VMCOREINFO in the dump.
- Nothing in the successor image beyond what `kmorph init` packs, unless
  the serial console is wanted: then `console_login` names a static
  program, busybox's `sh` for instance, and kmorph copies it in. The
  successor kernel needs the serial driver, the multikernel filesystem
  and the vsock transport built in, since that image carries no modules.
  The distro image carries the host's drivers plus whatever `modules`
  names, such as the vsock transport when it is a module.
- On the predecessor, root, and `gzip`, `xz`, `zstd` or `lz4` if the
  kernel image is a bzImage rather than an ELF vmlinux.

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

## Quick start

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
   config changes.

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

From here the successor probes the predecessor five times a second. If the
predecessor dies, the successor takes over on its own. To stand down, run
`kmorph disarm`.

## On a distro

The package route is the one kdump uses. `make install PREFIX=/usr`
places, from `dist/`, a dracut module and an initramfs-tools hook, a
helper that runs whichever of dracut or mkinitramfs the host has to
write `/var/lib/kmorph/successor.img` with kmorphd inside, a
`kmorph.service` that builds the image if it is missing, arms at boot
and disarms on stop, and a kernel hook, for kernel-install and for
Debian's `postinst.d`, that rebuilds the image for the kernel just
installed and points the `kernel` and `cmdline` keys at it.

The successor's image is thus the distro's own initramfs with kmorphd
inside it, at the path `kmorph arm` reads by default, while the host's
boot initramfs is left alone. In the successor, kmorphd is started by
the initramfs's init while it waits for a root device the host still
owns. The operator writes the remaining keys once:

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

`memory` must hold an unpacked distro initramfs; `kmorph arm` warns when
it cannot. `console_login` needs no static program here, since the
initramfs has libc. `kmorph init` is not involved.

After a takeover the successor sits in the initramfs with the console
until the kernel can hand it the host's disk, at which point the
initramfs mounts root and the machine continues as itself. That
handover is kernel work still to come.

## Configuration

The file is `key = value`, one per line, `#` starts a comment. Both
programs read the same file; each uses the keys for its side. `kernel`,
`cpus` and `memory` are required to arm, everything else has a default.

### Successor (used by kmorph arm)

| Key | Default | Meaning |
|---|---|---|
| `name` | `successor` | Instance name in `/sys/fs/multikernel/instances`. |
| `cpus` | | CPUs for the successor, as physical ids the kernel uses (APIC ids), e.g. `1` or `12-15,20`. |
| `memory` | | Memory for the successor, e.g. `128MB`, `4GB`. |
| `kernel` | | Kernel image: an ELF vmlinux, or a bzImage from which the vmlinux is extracted. |
| `initrd` | `/var/lib/kmorph/successor.img` | Initramfs for the successor; the default is what `kmorph init` writes. kmorph appends the config to whichever image is used. |
| `cmdline` | | Kernel command line for the successor. See [Successor console](#successor-console). With `dump` set, kmorph adds `iomem=relaxed`, which the successor needs to read the predecessor's memory through `/dev/mem` on kernels built with `IO_STRICT_DEVMEM`. |
| `devices` | | Devices handed to the successor, comma separated, as PCI addresses (`0000:09:00.0`) or as the names sysfs knows them by: a network interface (`enp61s0f1`), a block device (`nvme0n1`), a framebuffer (`fb0`), a USB bus (`usb1`). A name resolves to the PCI function behind it; the successor only ever sees the address. Give it its own NIC and disk. |
| `machine_cpus` | from the MADT | Every CPU on the machine. Set only on a machine without ACPI. |
| `modules` | | Kernel modules the distro image builders add to the successor image, comma separated: the vsock transport when it is a module, drivers for devices in `devices`. The `kmorph init` image has no module loader and ignores it. |
| `console` | | Serial line the successor takes over after the crash, e.g. `ttyS0`. |
| `console_baud` | `115200` | Speed of that line. |
| `console_login` | | Program run on the serial line after the takeover, required with `console`. With the default image it must be a static executable, which `kmorph init` copies in; a user image carries its own. |

### Detection and takeover (used by kmorphd)

| Key | Default | Meaning |
|---|---|---|
| `probe_interval` | `100ms` | How often the predecessor is probed. A probe unanswered by the next one has timed out. |
| `probe_timeouts` | `5` | Consecutive timeouts before the predecessor is declared dead. |
| `fence_retries` | `3` | Retries of a fence that could not park every CPU. |
| `dump` | | File to receive the predecessor's memory as a vmcore before it is reclaimed. A FIFO works. Unset: reclaim at once. |

The successor's probe never mistakes a slow predecessor for a dead one on
its own evidence: a live predecessor kernel answers every probe by itself,
with no user space involved, so only silence counts, and only for
`probe_timeouts` probes in a row.

## Operation

### kmorph, on the predecessor

```
kmorph init   [--config PATH]
kmorph arm    [--config PATH]
kmorph disarm [--config PATH]
kmorph status [--config PATH]
```

**init** writes the successor image, `/var/lib/kmorph/successor.img`: an
initramfs holding the kmorphd installed beside kmorph as its init and, when
`console` is set, the `console_login` program at its own path. Both must be
static executables, since the image has no libc; init refuses a dynamic one.
Run it once after installing kmorph and again after upgrading it or changing
`console_login`. The config is not in the image.

**arm** prepares the machine and boots the successor. If the machine has no
multikernel pool yet, kmorph mounts `/sys/fs/multikernel`, and creates a
pool holding the successor's CPUs, its memory plus one memory block of
headroom for the kernel's own use, its devices, and the serial device when
`console` is set. If a pool exists, kmorph uses it and adds any configured
PCI device the pool lacks. It then creates the instance, loads the kernel
and the image with the config appended to it, and boots the successor. The
image is the one init wrote unless `initrd` names another; the config is
appended either way, as a second cpio archive the kernel unpacks after the
first, so the successor always boots with the config arm just read. The
whole command takes about a second.

**disarm** halts the successor, unloads its image and removes the instance,
returning its resources to the pool.

**status** prints the instance state from the kernel and, over vsock, the
daemon's own state:

```
successor: active (instance successor, id 1)
kmorphd: ARMED predecessor=alive last_probe=12ms error=0
```

### kmorphd, in the successor

```
kmorphd [--config PATH] [--foreground]
```

kmorphd refuses to run in a kernel that was not armed by kmorph, since it
finds nothing to take over. As the image's init, which is how `kmorph init`
packs it, it mounts proc, sysfs, devtmpfs and a tmpfs on `/run`, stays in
the foreground and reaps orphans. Started by another init it daemonises
unless `--foreground` is given. Either way it logs to the kernel log and
writes its state to `/run/kmorph/state`:

```
TAKEN_OVER predecessor=silent last_probe=181ms error=0
```

The first word is the state, `predecessor` is the last probe result,
`last_probe` the time since it, `error` the errno of the last failed step
or 0. The states are ARMED, SUSPECT (one or more timeouts), FENCING,
FENCE_FAILED, FENCED and TAKEN_OVER.

## What happens at a crash

1. **Detection.** Probes time out `probe_timeouts` times in a row. With the
   defaults that is half a second.
2. **Fence.** The successor kernel NMI-parks every CPU it does not own and
   confirms each one parked. A CPU that never parks fails the fence, which
   is retried on the next probe up to `fence_retries` times; the takeover
   never proceeds with a CPU possibly still running the dead kernel.
3. **Adoption.** The successor computes what is takeable, the machine's RAM
   minus its own minus the kernel's control regions, and every CPU it does
   not own, and claims it in one transaction. Fenced CPUs are woken from the
   predecessor's park slot; memory is hot-added in whole memory blocks,
   128 MB on x86.
4. **Dump and reclaim.** With `dump` set, memory is left unclaimed until it
   has been written to the dump file through `/dev/mem`, then reclaimed.
   The file is an ELF core in the format of kdump's `/proc/vmcore`: one
   segment per memory range, the registers of every CPU at the moment it
   stopped, and the crashed kernel's VMCOREINFO, so `crash` and
   `makedumpfile` read it directly. The successor reads the memory through
   `/dev/mem`; `kmorph arm` boots it with `iomem=relaxed` so
   `IO_STRICT_DEVMEM` does not get in the way, and the successor must not be
   booted locked down. If the dump fails, the memory stays unclaimed and
   readable rather than being destroyed.
5. **Console.** With `console` set, the login program is started on the serial line.

Takeover is one-shot. Afterwards the successor is the machine's kernel and
is itself unprotected until an operator arms a new successor.

### What the successor ends up with

Every CPU, and all memory that forms whole memory blocks. Blocks that
contain the first megabyte, the top of RAM, the successor's own memory or
one of the kernel's control regions cannot be hot-added and stay unclaimed;
on a 2 GB machine that is about 500 MB, on a large machine a fixed few
hundred megabytes. Unclaimed memory remains readable through `/dev/mem`.

The successor keeps the devices it was armed with. Devices that belonged to
the predecessor are not adopted, so the successor needs its own NIC and
disk from the start.

### Inspecting the dump

The dump is a vmcore. Open it with the predecessor's vmlinux:

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

with `dump = /tmp/vmcore`. Zero pages are sent as zeros on a FIFO.

## Successor console

When `console` names a serial line, `kmorph arm` places the legacy serial
device in the pool and hands it to the successor, whose kernel registers it
at boot. After the takeover kmorphd switches the line to polling, since the
successor has no interrupt routing for it, and keeps the login program on
it, so the serial console that showed the predecessor now shows the
successor's shell. With the default image, `console_login` must be a static
executable, since the image carries no libc.

Give the successor an early serial console on the same line in its
`cmdline`, `earlyprintk=serial,ttyS0,115200 keep_bootcon`, so its kernel
messages reach the serial console before and after the takeover. Do not
use `console=mktty0`: that console forwards messages to the predecessor
and, once the predecessor is dead, floods the successor with its own
failure reports.

On the serial console the whole event is one continuous stream: the
predecessor's messages, the successor's boot lines interleaved once armed,
the panic, the takeover, and the successor's login prompt.

## Limitations

- Exactly two kernels: one predecessor, one successor.
- No automatic re-arming after a takeover.
- No device takeover; the successor runs on the devices it was armed with.
- Memory is adopted in whole 128 MB blocks; the remainder stays unclaimed.
- The dump is not filtered; makedumpfile can shrink it afterwards.
- `kmorph upgrade`, a planned takeover for a zero-downtime kernel upgrade,
  is not implemented.

## How it works

Everything novel is in the kernel. kmorph is orchestration over the
kernel's multikernel interfaces: the `/sys/fs/multikernel` filesystem for
the pool and instances, `kexec_file_load()` and `reboot()` to load, boot,
halt and fence, and vsock for liveness.

### Arming

`kmorph arm` runs three kernel operations on the predecessor:

1. **Create.** An overlay transaction written to
   `/sys/fs/multikernel/overlays/new` creates the instance with the
   successor's CPUs, memory and devices. The same transaction carries the
   host tree, a `chosen { multikernel,host-tree { ... } }` node naming
   every CPU on the machine from the ACPI MADT, the RAM map from
   `/proc/iomem` and the PCI inventory from sysfs. With `dump` set, the
   tree also carries a `vmcore` node with where the kernel keeps its
   vmcoreinfo, its per-CPU crash note buffers and its direct map; the
   node also tells the predecessor kernel to save each CPU's registers
   when it stops, so the dump carries them. When the machine has no pool
   yet, a baseline written to
   `/sys/fs/multikernel/device_tree` creates one first.
2. **Load.** `kexec_file_load()` in multikernel mode loads the kernel and
   the initramfs for that instance. The initramfs is the image with the
   config appended, assembled in memory; nothing on disk is changed.
3. **Exec.** The multikernel `reboot()` command boots it. At that moment the
   predecessor kernel writes the successor's boot device tree: the host tree
   goes into the successor's `/chosen` as it was given, and beside it the
   kernel adds what only it knows, the control regions the successor must
   never treat as free memory (`multikernel,reserved-memory`) and the park
   slot where fenced CPUs will wait.

The successor kernel registers every CPU named in the host tree at boot,
because a CPU can only be fenced or hot-added if its APIC id was
enumerated then. Everything the successor will need is therefore fixed
before it boots; nothing is published or refreshed at runtime, so nothing
can go stale.

### Watching

kmorphd probes the predecessor with a vsock `connect()` to CID 0 on a port
nobody listens on. A live predecessor kernel answers RST by itself, which
the successor sees as `ECONNRESET`. Silence, a probe still unanswered when
the next one is due, is the only evidence of death.

### Takeover

From the successor's point of view the machine splits in two: what it
owns, tracked by its own accounting, and everything else, which after the
fence is one bag it absorbs regardless of how the predecessor had divided
it.

1. **Fence.** `reboot()` with the multikernel force-halt command and
   `mk_id 0`. The successor kernel NMI-parks every CPU it does not own,
   sends INIT to stragglers, and confirms each target parked through the
   presence bitmap the park loop maintains; it returns an error rather than
   a partial fence.
2. **Adopt.** kmorphd reads the host tree and the reserved regions back
   from its own `/proc/device-tree`, subtracts its own RAM from the
   machine's, subtracts the reserved regions, rounds to whole memory
   blocks, and applies one overlay against its own instance node with
   `cpu-add` and `memory-add` items. The kernel wakes fenced CPUs through
   the predecessor's park slot and hot-adds the memory.
3. **Reap.** With a dump configured, memory is held back from that overlay,
   written as a vmcore through `/dev/mem`, then adopted by a second overlay.

### The daemon's states

```
ARMED ── timeout ──▶ SUSPECT ── ECONNRESET ──▶ ARMED
                        │
                        ├─ probe_timeouts in a row
                        ▼
                     FENCING ── lost CPU ──▶ FENCE_FAILED ──(retry)──▶ FENCING
                        │
                        ▼
                     FENCED ── adopt, dump, reap ──▶ TAKEN_OVER
```

Every transition is logged and written to `/run/kmorph/state`. A failed
adoption is retried on the next probe event; the fence is idempotent and
retried a bounded number of times.
