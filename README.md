# linux-kernel-samples

> **Disclaimer.** These are out-of-tree Linux kernel modules written for
> learning. A module runs in kernel space with full privileges, so one bug can
> take down the whole machine. Load them only in a throwaway VM or on a spare
> kernel you can afford to crash, never on a host whose data or uptime you care
> about. The code is provided "as is" under 0BSD (see [`LICENSE`](LICENSE)), and
> the author accepts no responsibility for any damage or loss of any kind,
> including without limitation security compromise, kernel panic or hang, data
> loss or filesystem corruption, hardware damage, a tainted or
> lockdown-restricted kernel, or an unbootable system. You build, load, and run
> these modules entirely at your own risk.

Runnable Linux kernel modules for learning the kernel by doing. Each sample
isolates one theme (a struct, function, or macro) as a module you build,
`insmod`, observe in `dmesg`, and `rmmod`. A sample explains itself through its
kernel log, so `dmesg` after `insmod` is the lesson.

## Samples

| sample | shows |
|--------|-------|
| `data_structure/container_of` | member offsets and tail padding, and the outer struct recovered from a direct, a nested (in one or two steps), a tail and an offset-0 member |
| `data_structure/list` | a `list_head` list: build, walk, look up, update, delete |
| `smp/percpu/percpu_parallel` | per-CPU counters, one hotplug-safe smpboot kthread per CPU |
| `interrupts/hardirq/hardirq` | a top-half handler on a simulated irq |
| `interrupts/hardirq/irq_none` | returning `IRQ_NONE`, how a shared-irq handler says "not mine" |
| `interrupts/hardirq/disable_irq` | a raise held back while the line is masked, then let through by `enable_irq` |
| `interrupts/deferred/tasklet` | a bottom half in softirq context (no sleeping, `GFP_ATOMIC` only) |
| `interrupts/deferred/bh_workqueue` | the BH workqueue (6.9+) that replaces tasklets, also in softirq context |
| `interrupts/deferred/workqueue_sample` | a bottom half in process context (sleeping and `GFP_KERNEL` allowed) |
| `interrupts/deferred/threaded_irq` | a bottom half in a dedicated irq kthread, also in process context |
| `interrupts/deferred/timer_softirq` | a `timer_list` callback in `TIMER_SOFTIRQ` |
| `interrupts/deferred/tcp_softirq_log` | TCP receive running in `NET_RX_SOFTIRQ`, seen from a netfilter hook |
| `interrupts/concurrency/interrupt_competition` | every CPU raising one shared irq, a counter kept gap-free by `spin_lock_irqsave` |
| `interrupts/danger/sleep_in_{hardirq,softirq}_danger` | the illegal case: sleeping in atomic context |

`make data_structure/container_of KCFLAGS=-DCONTAINER_OF_TYPE_MISMATCH` passes a
mistyped pointer to both `container_of()` and `naive_container_of()`. Only
`container_of()` stops the build, on its `static_assert(__same_type(...))`.

## Building

Prerequisites: headers for the target kernel (`linux-headers` on Arch,
`linux-headers-$(uname -r)` on Debian/Ubuntu, `kernel-devel` on Fedora), a C
toolchain, and root to load.

```
make                           # build and sign every sample, then compdb, tags, cscope
make <theme>/<sample>          # one sample, e.g. make data_structure/container_of
make <theme>/<sample> SIGN=0   # the same, unsigned
make clean                     # clean every registered sample
make list                      # list the registered samples
make compdb                    # compile_commands.json for clangd
make tags                      # ctags index (also: make cscope)
```

Modules are signed with a local key, `MOK.priv`/`MOK.der` (git-ignored),
generated on the first build. The signature is harmless without Secure Boot.
When `sign-file`, or `openssl` for a first key, is missing, the module stays
unsigned with a warning.

The running kernel is the default target. Override it per invocation, or pin it
with the same variables (`KVER := ...`) in a git-ignored `config.mk`:

```
make <theme>/<sample> KVER=6.6.0-rpi KDIR=/path/to/rpi/kernel/build \
    ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
```

### How the build works

A `.ko` is linked into the running kernel at `insmod` time, so it must match
that kernel's headers, `.config`, compiler flags and symbol versions (vermagic,
`modpost`). Every sample therefore builds through the target kernel's own
kbuild, and the repo ships sources, not prebuilt modules.

| stage | does |
|-------|------|
| `Makefile` | reads `SAMPLE_DIRS` and each directory's `manifest.mk`, applies the target defaults and `config.mk` |
| `scripts/kmod.mk` | includes `sample.mk`, runs the preflight, symlinks the sources into `.build-<mod>/` and writes its `Kbuild` |
| `make -C $(KDIR) M=<dir>/.build-<mod> modules` | compiles and links `<mod>.ko`, which `kmod.mk` copies next to the source and signs |

The preflight stops with `$(error)` when the source, the kernel build tree or a
requested cross compiler is missing, or when the arch, a required `CONFIG` or
the kernel version does not fit. Each sample builds in its own git-ignored
`.build-<mod>/`, so `make -j` builds samples in parallel.

## Loading

Compiling is harmless; only loading is dangerous, so load only where a crash
costs nothing (see [In a VM](#in-a-vm)).

```
sudo insmod data_structure/container_of.ko   # the .ko sits next to its source
dmesg | tail
sudo rmmod container_of
```

At load, the data structure samples log their walk-through, and the
simulated-irq samples (`CONFIG_IRQ_SIM`) raise their irq and log the flow,
except the danger ones, which wait to be fired by hand. `percpu_parallel` and
`timer_softirq` log every second until `rmmod`. `tcp_softirq_log` logs every
16th inbound IPv4 TCP packet and never creates traffic itself: repeat
`curl http://127.0.0.1/` (`ping` is ICMP and does not count).

The `interrupts/danger/` samples are the only ones with a debugfs control, and a
plain load does nothing dangerous. Unarmed, `trigger` takes a safe mock path
that only logs the skipped work. Armed, it sleeps in atomic context and can hang
or crash the machine, so arm it only in a throwaway VM:

```
sudo insmod interrupts/danger/sleep_in_hardirq_danger.ko
echo 1 | sudo tee /sys/kernel/debug/sleep_in_hardirq_danger/danger_enabled
echo 1 | sudo tee /sys/kernel/debug/sleep_in_hardirq_danger/trigger
dmesg | tail
sudo rmmod sleep_in_hardirq_danger
```

Background for reading the logs:

- The four simulated-irq samples in `interrupts/deferred/` share the same
  hardirq top half and differ only in the bottom-half mechanism. Tasklets are
  deprecated; the tasklet samples stay for the classic form.
- A module cannot register a softirq vector of its own: `open_softirq` is not
  exported, and the vector table is fixed at compile time. So `timer_softirq`
  owns only a bottom half, and its top half is the kernel's own timer tick,
  present on every machine.
- `tcp_softirq_log` proves `NET_RX_SOFTIRQ` with its `in_serving_softirq=Y`
  field. `in_softirq()` alone is `softirq_count()`, which is also nonzero under
  `local_bh_disable()`.
- In `interrupt_competition`, raises that land together merge or drop rather
  than queue: irq_sim keeps one pending bit per line and one irq_work per
  domain, and genirq refuses re-entry while the handler is in progress. The
  handler thus runs one at a time, each softirq reports the hardirq it picked
  up, and every raise, hardirq and softirq line names its CPU.
- The simulated-irq samples act only on their own simulated line, never on a
  real system interrupt.

### In a VM

A module can only be tested safely in a disposable kernel. Containers (Docker,
Podman, toolbox) share the host kernel and do not help.

- Any throwaway VM (multipass, GNOME Boxes, virt-manager, VirtualBox) works:
  install the prerequisites inside it, then `make`, `insmod`, `dmesg`, `rmmod`.
- [virtme-ng] (`apt`/`dnf install qemu-system-x86 virtme-ng`) boots the running
  kernel in QEMU within seconds: run `vng -r` from the repo, with this directory
  available and no disk image. vermagic matches, so no signing is needed, and a
  panic only kills the VM. `vng -r -- <cmd>` runs a single command.

[virtme-ng]: https://github.com/arighi/virtme-ng

### Under Secure Boot

Under Secure Boot the kernel runs in lockdown and rejects unsigned out-of-tree
modules. `make` already signs them, so enroll the local key once instead of
disabling Secure Boot:

```
sudo mokutil --import MOK.der    # set a one-time password
# reboot, choose "Enroll MOK", enter the password
```

Re-run `make` after each change to re-sign. Signing fixes loadability only; the
crash risk is unchanged.

### Live addresses for debugging

The loader places a module at a random base, not at the zero-based link-time
addresses in the `.ko`. Read the live placement as root: the `sections/` files
are mode 0400, and `kptr_restrict` blanks the other two for unprivileged
readers.

```
sudo grep '\[hardirq\]' /proc/kallsyms       # each module symbol at its runtime address
sudo grep '^hardirq ' /proc/modules          # 6.4+: the hex field before the taint flags (OE) is the .text base
sudo cat /sys/module/hardirq/sections/.text  # the same base
ls -a /sys/module/hardirq/sections/          # dotfiles; only non-empty sections exist
```

Through a kernel gdb stub (KGDB, or QEMU's `-s`), `add-symbol-file hardirq.ko
0x<.text> -s .bss 0x<.bss>` gives source-level breakpoints, because the `.ko`
carries `CONFIG_DEBUG_INFO` DWARF. The same DWARF decodes an oops frame
`func+0x<off> [hardirq]` offline: `gdb hardirq.ko -ex 'list *(func+0x<off>)'`.
The base is re-randomized on every load, so re-read it each time.

## Writing a sample

A sample is one `.c` file at `<theme>/[<group>/]<name>.c` that builds
`<name>.ko` beside it. It covers one main theme and lives under the top-level
directory that owns that theme. Directories are created on demand:

| dir | theme |
|-----|-------|
| `module/` | module mechanics: init/exit, params, printk, symbols |
| `process/` | `task_struct`, scheduling, fork, threads, namespaces |
| `mm/` | pages, slab, kmalloc/vmalloc, `mm_struct`, VMAs |
| `interrupts/` | IRQ, softirq, tasklet, threaded IRQ |
| `locking/` | spinlock, mutex, rwsem, completion, RCU, atomics |
| `smp/` | per-cpu data, IPIs, cpumask, CPU-bound kthreads, CPU hotplug |
| `time/` | jiffies, timers, hrtimer, delays |
| `fs/` | file_operations, procfs/sysfs/debugfs, char devices |
| `net/` | sk_buff, netdev, netfilter |
| `arch/` | barriers, MSR/CR, arch-specific details |
| `data_structure/` | list, rbtree, hashtable, idr, and the `container_of` beneath them |
| `core/` | cross-cutting primitives: kref, ERR_PTR, likely/unlikely |

The minimal shape:

```c
// SPDX-License-Identifier: 0BSD
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>

static int __init demo_init(void)
{
	pr_info("loaded\n");
	return 0;
}

static void __exit demo_exit(void)
{
	pr_info("unloaded\n");
}

module_init(demo_init);
module_exit(demo_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Minimal module skeleton");
MODULE_VERSION("1.0");
```

- `module_init`/`module_exit` register the hooks `insmod`/`rmmod` call. `init`
  returns 0, or a negative errno to abort loading. `__init` code is discarded
  after loading, and `__exit` code is dropped when the module is built in.
- `pr_fmt` prefixes every `pr_*()` line with the module name. The kernel log is
  the only output a module has. As in mainline, log lines start lowercase unless
  they open with an identifier or acronym, and `MODULE_DESCRIPTION` is a
  sentence-case noun phrase without a final period.
- The SPDX line is the file's copyright license, 0BSD, and travels with a copied
  file. `MODULE_LICENSE("Dual BSD/GPL")` tells the kernel the same license: a
  BSD variant, named by the SPDX line, or GPL. The string is on the kernel's
  GPL-compatible list, so the module avoids the proprietary taint and keeps the
  GPL-only exports the samples call (e.g. `smpboot_register_percpu_thread()`
  and the irq_sim API; the kthread API itself,
  `kthread_create_on_node()`/`kthread_stop()`, is a plain `EXPORT_SYMBOL`).

Registration is explicit, like the kernel's per-directory `obj-m`: only listed
samples build, so work-in-progress files and userspace helpers can share a
directory. Add the name to the directory's `manifest.mk`:

```make
# data_structure/manifest.mk
samples := \
	container_of \
	list
```

A new directory also needs its own `manifest.mk` and an entry in `SAMPLE_DIRS`
in the top-level `Makefile`. The build stops when a listed `manifest.mk` is
missing or lists no samples.

Special build requirements go in an optional `sample.mk` beside the sources. It
is included for every sample in its directory, so it branches on
`$(notdir $(SAMPLE))` when the samples differ:

| variable | meaning |
|----------|---------|
| `SAMPLE_MODULE` | module name when it differs from the source file basename |
| `SAMPLE_OBJS` | object list for a multi-file module (`a.o b.o`) |
| `SAMPLE_REQUIRED_CONFIGS` | kernel configs that must be `=y`/`=m` (e.g. `CONFIG_KPROBES`) |
| `SAMPLE_SUPPORTED_ARCH` | allowed arches, in kbuild ARCH names (e.g. `x86 arm64`) |
| `SAMPLE_MIN_KVER` | minimum kernel version (e.g. `5.14`) |
| `SAMPLE_MAX_KVER` | maximum kernel version, for an API removed later (e.g. `6.7`) |

## Coding style

Strict kernel style (hard tabs, 8 columns; see `.clang-format` and
`.editorconfig`), with the column limit left unenforced. clangd needs the exact
kbuild flags: `make compdb` builds `compile_commands.json` from the `.cmd` files
in each `.build-<mod>/`, points every entry at the real source, and drops the
GCC-only flags clang rejects. Build a sample before opening it in an editor.

## License

[0BSD](LICENSE): anyone may use, copy, modify and redistribute the code for any
purpose, commercial included, with no conditions, not even attribution. Every
source and build file carries `SPDX-License-Identifier: 0BSD`.
