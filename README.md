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
| `data_structure/container_of` | the outer struct recovered by `container_of()` from members before, after and inside an embedded struct |
| `data_structure/list` | a `list_head` list: build, walk, look up, update, delete |
| `smp/percpu/percpu_parallel` | per-CPU counters, one hotplug-safe smpboot kthread per CPU |
| `interrupts/hardirq/hardirq` | a top-half handler on a simulated IRQ |
| `interrupts/hardirq/irq_none` | returning `IRQ_NONE`, how a shared-IRQ handler says "not mine" |
| `interrupts/hardirq/disable_irq` | five raises held back for 5 s while the IRQ is disabled, merged into one delivery by `enable_irq` |
| `interrupts/deferred/tasklet` | a bottom half in softirq context (no sleeping, `GFP_ATOMIC` only) |
| `interrupts/deferred/bh_workqueue` | the BH workqueue (6.9+) that replaces tasklets, also in softirq context |
| `interrupts/deferred/workqueue_sample` | a bottom half in process context (sleeping and `GFP_KERNEL` allowed) |
| `interrupts/deferred/threaded_irq` | a bottom half in a dedicated IRQ kthread, also in process context |
| `interrupts/deferred/timer_softirq` | a `timer_list` callback in `TIMER_SOFTIRQ` |
| `interrupts/deferred/net_rx_softirq` | TCP receive running in `NET_RX_SOFTIRQ`, seen from a netfilter hook |
| `interrupts/concurrency/interrupt_competition` | every CPU raising one shared IRQ, a counter kept gap-free by `spin_lock_irqsave` |
| `interrupts/danger/sleep_in_{hardirq,softirq}_danger` | the illegal case: sleeping in atomic context |

## Building

Prerequisites: headers for a 6.12 or later target kernel (`linux-headers` on
Arch, `linux-headers-$(uname -r)` on Debian/Ubuntu, `kernel-devel` on Fedora),
a C toolchain, python3, and root to load.

```
make                           # build and sign every sample (debug profile); one job per CPU
make -j4                       # the same with 4 jobs
make release                   # the same with release flags (also PROFILE=release), see below
make KDIR=/path/to/linux       # the same against another kernel tree, see below
make <theme>/<sample>          # one sample, e.g. make data_structure/container_of
make <theme>/<sample> SIGN=0   # the same, unsigned
make index                     # compile_commands.json with the PROFILE flags, tags and cscope;
                               # later builds rewrite compile_commands.json when its flags change
make clean-index               # remove the index files
make clean                     # remove modules, .ko.debug, build dirs and .kbuild/
make distclean                 # clean, clean-index, clangd caches, tool outputs and the signing key
make format                    # clang-format every source (format-check only lists the changes)
make list                      # list the registered samples
make help                      # the goals and the variables
```

Modules are signed with a local key, `MOK.priv`/`MOK.der` (git-ignored),
generated on the first build, kept by `make clean` and deleted by
`make distclean`; under Secure Boot the next key needs a new enrollment. The
signature is harmless without Secure Boot.
When `sign-file`, or `openssl` for a first key, is missing, the module stays
unsigned with a warning. Modules for a release other than the running kernel's
stay unsigned unless `SIGN=1`.

The running kernel is the default target, or the newest installed build tree
when the running kernel's is gone (after an upgrade, before a reboot).

### Debug and release builds

`PROFILE` sets the flags the samples build with: `debug` by default, or
`release`. The kernel and its configuration stay untouched. Both profiles keep
what the kernel provides for tracing: ftrace and kprobes, tracepoints, module
BTF, and dynamic debug (`-DDYNAMIC_DEBUG_MODULE` when the kernel has only
`CONFIG_DYNAMIC_DEBUG_CORE`). The sanitizers the kernel enables (KASAN, KMSAN,
UBSAN, KCSAN) instrument both. The optimization level stays the kernel's, because
kernel code does not build at `-O0` or `-Og`; `-Wshadow` stays out, because the
kernel's own headers trip it.

| | `debug` | `release` |
|---|---|---|
| `pr_debug()`, `dev_dbg()` | on (`-DDEBUG`) | off, switchable through dynamic debug |
| DWARF | `-g3` with macros, every type (`-fno-eliminate-unused-debug-types`) and the compiler options (`-grecord-gcc-switches`), also over a kernel built with reduced or no debug info | moved to `<mod>.ko.debug`; the `.ko` keeps its symbols and BTF, loses its debug sections (`--strip-debug`, as `INSTALL_MOD_STRIP=1`) and names the file in `.gnu_debuglink` |
| code shape | `-fno-inline`, `-fno-optimize-sibling-calls` and `-fno-builtin`: every function and library call stays a call, for ftrace, kprobes and gdb; frame pointers in every function (`-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer`) | the kernel's |
| stack checks | `-fstack-protector-all` where the kernel has a stack protector; stack variables start as a pattern (`-ftrivial-auto-var-init=pattern`), not zeroes or garbage | the kernel's |
| sanitizers | `KASAN_SANITIZE`, `KCSAN_SANITIZE`, `KMSAN_SANITIZE` and `UBSAN_SANITIZE` set to `y`, which take effect on a kernel built with that sanitizer | the kernel's |
| coverage | `GCOV_PROFILE` and `KCOV_INSTRUMENT`, on kernels with gcov or kcov | the kernel's |
| warnings | `W=1` | the kernel's |

Release modules are signed after the strip. `export PROFILE=release` makes
release the default of a shell.

### Other kernels and architectures

`KDIR` points the build at a kernel tree: the running kernel's headers by
default, or any tree built for a board, a VM or another architecture. The
modules, `compile_commands.json`, tags and cscope all follow it. Pass it per
invocation or export it; every variable of `make help` also comes from the
environment.

```
make KDIR=~/kernels/v6.18-arm64        # ARCH and the toolchain come from the tree
make KDIR=~/bsp/out VMLINUX=1          # configured or prepared only: vmlinux in .kbuild/
make KDIR=~/kernels/v6.18-x86_64 CC=gcc-16
```

`scripts/check-kernel-tree.py` reads the tree before anything runs and stops the build when
the tree is not usable. It checks only what kbuild itself defines and produces
(the version variables of the top `Makefile`, `scripts/subarch.include`, the
ARCH aliases, `auto.conf.cmd`, the generated headers and host programs), so a
vendor or BSP tree passes as long as kbuild produced it. Nothing in the tree is
executed except its version scripts, which run from a temporary directory; the
tree itself is never written.

| tree | what happens |
|---|---|
| built, with `Module.symvers` | modules build against it; modpost checks every export |
| vmlinux only, with `vmlinux.symvers` | the same, with exports checked against vmlinux |
| prepared (`modules_prepare`) or configured (`.config`) only | stops; `VMLINUX=1` builds vmlinux out of tree and the modules against that build |
| never configured, or not a kernel tree | stops with the reason |
| older than 6.12 | stops |

- `VMLINUX=1` works in `.kbuild/<version>-<arch>-<hash>/`. Kbuild builds out of
  tree only from a clean source, so a tree built in place is first cloned with
  `cp --reflink=auto` (free on btrfs); an `O=` tree uses its source directly.
  The `.config` is copied byte for byte, and the build is refused when
  `syncconfig` would change it. Later runs reuse the build while the tree's
  `.config` and release stay the same. `make clean` removes `.kbuild/`.
- Toolchain: the compiler recorded in the tree's configuration when it is
  installed (`aarch64-linux-gnu-gcc-16`), else the same prefix without the
  version (`aarch64-linux-gnu-gcc`), else plain `gcc` when the tree's
  architecture is the host's. A clang tree gets `LLVM=-<major>` when every LLVM
  tool carries that suffix, else `LLVM=1`. `ARCH=`, `CROSS_COMPILE=`, `LLVM=`
  and `CC=` override the choice; an `ARCH` that the tree's kbuild does not map
  to the tree's architecture stops the build.
- Tool versions: the tree's own `cc-version.sh`, `ld-version.sh`,
  `pahole-version.sh` and `rustc-version.sh` run against the chosen tools, and
  the result is compared with what the configuration recorded. A difference is
  shown in the banner for a module build and refuses a `VMLINUX=1` build, which
  would otherwise rewrite the configuration.
- Target check: the tree's `vmlinux` and `scripts/mod/empty.o` must agree on ELF
  class, byte order and machine, and every built `.ko` must match them. A
  mismatch stops the build and names both machines as `readelf` reports them;
  a tree with neither file is noted in the banner.
- A compile, link, modpost or target error stops `make`, and
  `compile_commands.json`, tags and cscope keep their previous contents.
- `KDIR_SYMLINK_DIRS` (default `/usr/src`) lists the directories whose symlinks the
  compile database may use in place of a versioned kernel path.
- Switching kernels or profiles rebuilds what changed: kbuild compares the
  command and the dependencies it recorded for each object.

### How the build works

A `.ko` is linked into the running kernel at `insmod` time, so it must match
that kernel's headers, `.config`, compiler flags and symbol versions (vermagic,
`modpost`). Every sample therefore builds through the target kernel's own
kbuild, and the repo ships sources, not prebuilt modules.

| stage | does |
|-------|------|
| `Makefile` | has `scripts/check-kernel-tree.py` check the tree and pick `ARCH` and the toolchain, builds vmlinux in `.kbuild/` on `VMLINUX=1`, and runs kbuild |
| `make -C $(KDIR) M=$(CURDIR) modules` | reads the top `Kbuild`, which lists the sample directories, and each directory's `Kbuild`, which lists its modules and includes the `PROFILE` flags of `scripts/profile.mk`; compiles and links every `.ko` next to its source, in parallel |
| `Makefile`, after kbuild | checks the target of every built `.ko`, splits it for `release` and signs it |

A directory's `Kbuild` skips the samples whose `CONFIG` the target kernel lacks
and says so in a `=>` line; kbuild itself stops when a source or the kernel
build tree is missing.

## Loading

Compiling is harmless; only loading is dangerous, so load only where a crash
costs nothing (see [In a VM](#in-a-vm)).

```
sudo insmod data_structure/container_of.ko   # the .ko sits next to its source
dmesg | tail
sudo rmmod container_of
```

At load, the data structure samples log their walk-through, and the
simulated-IRQ samples (`CONFIG_IRQ_SIM`) raise their IRQ and log the flow,
except the danger ones, which wait to be fired by hand. `percpu_parallel` and
`timer_softirq` log every second until `rmmod`. `net_rx_softirq` logs every
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

- The four simulated-IRQ samples in `interrupts/deferred/` share the same
  hardirq top half and differ only in the bottom-half mechanism. Tasklets are
  deprecated; the tasklet samples stay for the classic form.
- A module cannot register a softirq vector of its own: `open_softirq` is not
  exported, and the vector table is fixed at compile time. So `timer_softirq`
  owns only a bottom half, and its top half is the kernel's own timer tick,
  present on every machine.
- `net_rx_softirq` proves `NET_RX_SOFTIRQ` with its `in_serving_softirq=Y`
  field. `in_softirq()` alone is `softirq_count()`, which is also nonzero under
  `local_bh_disable()`.
- In `interrupt_competition`, raises that land together merge or drop rather
  than queue: irq_sim keeps one pending bit per line and one irq_work per
  domain, and genirq refuses re-entry while the handler is in progress. The
  handler thus runs one at a time, each softirq reports the hardirq it picked
  up, and every raise, hardirq and softirq line names its CPU.
- The simulated-IRQ samples act only on their own simulated line, never on a
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
0x<.text> -s .bss 0x<.bss>` gives source-level breakpoints from the DWARF in
the `.ko`, or in the `.ko.debug` that gdb finds through the debuglink of a
release module. The same DWARF decodes an oops frame
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
/* SPDX-License-Identifier: 0BSD */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static int __init init_demo_module(void)
{
	pr_info("loaded\n");
	return 0;
}

static void __exit exit_demo_module(void)
{
	pr_info("unloaded\n");
}

module_init(init_demo_module);
module_exit(exit_demo_module);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Minimal module skeleton");
MODULE_VERSION("1.0");
```

- `module_init`/`module_exit` register the hooks `insmod`/`rmmod` call. `init`
  returns 0, or a negative errno to abort loading. `__init` code is discarded
  after loading, and `__exit` code is dropped when the module is built in.
- `pr_fmt` prefixes every `pr_*()` line with the module name. The kernel log is
  the only output a module has.
- The SPDX line is the file's copyright license, 0BSD, and travels with a copied
  file. `MODULE_LICENSE("Dual BSD/GPL")` tells the kernel the same license: a
  BSD variant, named by the SPDX line, or GPL. The string is on the kernel's
  GPL-compatible list, so the module avoids the proprietary taint and keeps the
  GPL-only exports the samples call (e.g. `smpboot_register_percpu_thread()`
  and the irq_sim API; the kthread API itself,
  `kthread_create_on_node()`/`kthread_stop()`, is a plain `EXPORT_SYMBOL`).

Registration is explicit, as in the kernel: each directory's `Kbuild` names its
modules in `obj-m`, so work-in-progress files and userspace helpers can share a
directory. Add the object to the directory's `Kbuild`:

```make
# data_structure/Kbuild
include $(KBUILD_EXTMOD)/scripts/profile.mk

obj-m += container_of.o
obj-m += list.o
```

A new directory needs its own `Kbuild`, starting with the same `include`, and an
`obj-m += <dir>/` line in the top `Kbuild`. Everything else is plain kbuild:

| need | kbuild |
|------|--------|
| a module of several files | `obj-m += foo.o` with `foo-y := a.o b.o` |
| a kernel option | `ifdef CONFIG_KPROBES` around the `obj-m` line, with an `$(info => ...)` line in the `else` |
| an architecture | `ifeq ($(SRCARCH),x86)` around the `obj-m` line |
| a kernel version | `#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 15, 0)` with `#error` in the source |

## Coding style

The [c-programming C coding style][coding-style], with the kernel's indentation:
hard tabs, 8 columns wide. `.clang-format` and `.editorconfig` hold what tools
can check; `make format` applies `.clang-format` and `make format-check` lists
what it would change. The kernel API keeps its own conventions where the style
would contradict it: `pr_fmt`, `module_init`, `MODULE_*`, the kernel types
(`u64`, `size_t`, `atomic_t`) and `-errno` returns. What the files cannot express:

- `#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt` on the line after the SPDX tag.
- SPDX `0BSD` and `MODULE_LICENSE("Dual BSD/GPL")`, where the kernel uses
  `GPL-2.0` and `MODULE_LICENSE("GPL")`.

clangd needs the exact kbuild flags: `make compdb` has kbuild write
`compile_commands.json` (`make M=... compile_commands.json`), and
`scripts/clang-compdb.py` drops the GCC-only flags clang rejects and lists the
kernel headers the samples include in `cscope.files`.
Build a sample before opening it in an editor. Kernel paths go through a
version-less link to the build tree when the distribution keeps one (Arch's
`/usr/src/linux`), so the database survives kernel upgrades; elsewhere, rerun
`make compdb` once an upgrade removes the old tree.

[coding-style]: https://github.com/yugeun-song/c-programming/blob/main/docs/coding-style.md

## License

[0BSD](LICENSE): anyone may use, copy, modify and redistribute the code for any
purpose, commercial included, with no conditions, not even attribution. Every
source and build file carries `SPDX-License-Identifier: 0BSD`.
