# linux-kernel-samples

Runnable Linux kernel modules for learning the kernel by doing. Each sample
isolates one **main theme** — a struct, function, or macro — as a loadable
module you build, `insmod`, observe in `dmesg`, and `rmmod`.

This repository is currently **environment only**: the build system, coding-style
configuration, and conventions are in place; sample modules are added on demand.
The sections below double as study notes and as context for any tooling
(including future Claude Code sessions) that needs to understand how the repo is
built and how a sample is composed.

## Layout

Top-level directories are **themes**, named with kernel vocabulary where that is
the most recognizable term and a clear word where it is not:

| dir | theme |
|------|-------|
| `module/` | module mechanics: init/exit, params, printk, symbols |
| `process/` | `task_struct`, scheduling, fork, threads, namespaces |
| `mm/` | pages, slab, kmalloc/vmalloc, `mm_struct`, VMAs |
| `interrupts/` | IRQ, softirq, tasklet, threaded IRQ |
| `locking/` | spinlock, mutex, rwsem, completion, RCU, atomics |
| `time/` | jiffies, timers, hrtimer, delays |
| `fs/` | file_operations, procfs/sysfs/debugfs, char devices |
| `net/` | sk_buff, netdev, netfilter |
| `arch/` | per-cpu, barriers, MSR/CR, arch-specific details |
| `lib/` | generic data structures: list, rbtree, hashtable, idr |
| `core/` | cross-cutting primitives: container_of, kref, ERR_PTR |

Placement rule: **one main theme per sample**, filed under the directory that
best represents that theme (its "primary owner"). Cross-cutting primitives with
no owner go in `lib/` (data structures) or `core/` (container_of, kref, ERR_PTR,
likely/unlikely, ...). Directories are created on demand as samples are added.

A sample lives at `<theme>/<sample>/<sample>.c`; that single `.c` is all the repo
tracks for an ordinary sample. A sample documents itself by printing to the
kernel log, so `dmesg` after `insmod` is the explanation.

A userspace companion program is named `*.user.c`, or placed in a `user/`
subdirectory; both are excluded from kernel-module discovery.

## Anatomy of a sample (composition principle)

A kernel module is not a `main()` program. It is object code linked into the
running kernel, exposing two hooks the module loader calls. The minimal shape:

```c
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

static int __init demo_init(void)
{
	pr_info("demo: loaded\n");
	return 0;
}

static void __exit demo_exit(void)
{
	pr_info("demo: unloaded\n");
}

module_init(demo_init);
module_exit(demo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yugeun Song");
MODULE_DESCRIPTION("one line: which theme this demonstrates");
```

Why each piece exists:

- `module_init` / `module_exit` register the entry/exit hooks that `insmod` and
  `rmmod` invoke. `init` returns `0` on success, or a negative errno to abort
  loading.
- `__init` / `__exit` are section annotations: `__init` code is discarded once
  the module finishes loading; `__exit` is dropped when a module is built into
  the kernel rather than loaded.
- `pr_info` and friends write to the kernel ring buffer (`dmesg`). There is no
  stdout in kernel space, so this is how a sample shows what it does.
- `MODULE_LICENSE("GPL")` declares the license: a non-GPL string taints the
  kernel and blocks access to GPL-only exported symbols. `MODULE_AUTHOR` and
  `MODULE_DESCRIPTION` are metadata readable with `modinfo`.

## Build pipeline (how it works, and why)

### Why the kernel build system is mandatory

A `.ko` is loaded into the running kernel and linked against its symbols at
`insmod` time, so it must be binary-compatible with the exact target kernel.
That compatibility is owned entirely by that kernel's build tree:

- **headers + config** — compiled against `$(KDIR)`'s headers and `.config`;
  struct layouts (e.g. `task_struct`) differ across versions/configs, so
  mismatched headers mean silent corruption.
- **flags** — `-D__KERNEL__ -DMODULE`, freestanding, and the model /
  stack-protector / retpoline / CFI options the kernel Makefiles define.
- **modpost + vermagic** — after compiling, `modpost` emits `<mod>.mod.c` (whose
  vermagic string encodes version + key config) and resolves symbols against
  `Module.symvers`; `insmod` rejects a module whose vermagic/CRCs disagree.
- **two-stage link** — the real `.ko` is `<mod>.o` linked with `<mod>.mod.o`.

Reproducing all of this by hand (plain gcc/CMake) would mean reimplementing
kbuild and modpost, and would break on every kernel change. So every out-of-tree
module — including the kernel's own `samples/` — delegates to
`make -C $(KERNELDIR) M=$(PWD) modules`.

### Orchestration flow

```
make <theme>/<sample>
  |
  +- top Makefile
  |    - discovers samples (find *.c, excluding scripts/, .git, user/, *.user.c)
  |    - applies defaults (host KVER/KDIR; ARCH/CROSS_COMPILE empty)
  |    - -include config.mk   (optional, PC-local persistent target)
  |
  +- scripts/kmod.mk   (per-sample driver; one recursive make per sample)
       - -include <sample>/sample.mk   (optional requirements)
       - preflight: $(error) and stop if the target is unsupported
       - generates <sample>/Kbuild  (obj-m := <name>.o), git-ignored
       |
       +- make -C $(KDIR) [ARCH=.. CROSS_COMPILE=..] M=<abs sample> modules
            - kernel build: CC -> MODPOST -> CC mod.o -> LD .ko -> BTF
```

- **One entry point.** The top `Makefile` is the only thing you run. The
  per-sample `Kbuild` is a one-line `obj-m` manifest that kbuild's `M=`
  interface requires, so it is generated at build time rather than committed —
  the repo tracks only `.c`.
- **Hard-fail preflight.** Before invoking the kernel build, the driver stops
  with `$(error ...)` when: the kernel build tree is missing, a requested cross
  compiler is absent, a sample's required `CONFIG` is off, the arch is
  unsupported, or the kernel is older than a sample's minimum. An unsupported
  target becomes an immediate, explicit failure instead of a confusing deep
  error from inside the kernel build.

### Running the build

```
make                       # build every sample
make <theme>/<sample>      # build one, e.g. make process/task_struct_fields
make clean                 # clean every sample
make list                  # list discovered samples
```

### Targeting another kernel or architecture

The running host kernel is the default. Override per invocation:

```
make <theme>/<sample> \
    KVER=6.6.0-rpi \
    KDIR=/path/to/rpi/kernel/build \
    ARCH=arm64 \
    CROSS_COMPILE=aarch64-linux-gnu-
```

To pin a target without retyping, create a `config.mk` (git-ignored, since the
target is a per-machine choice, not part of the repo):

```make
KVER          := 6.6.0-rpi
KDIR          := /path/to/rpi/kernel/build
ARCH          := arm64
CROSS_COMPILE := aarch64-linux-gnu-
```

### Loading and observing

```
sudo insmod <theme>/<sample>/<sample>.ko
dmesg | tail
sudo rmmod <sample>
```

## Per-sample build contract (`sample.mk`)

Only needed for a sample with special requirements; an ordinary sample needs
nothing but its `.c`. Recognized variables:

| variable | meaning |
|----------|---------|
| `SAMPLE_MODULE` | module name when it differs from a single source file |
| `SAMPLE_OBJS` | object list for a multi-file module (`a.o b.o`) |
| `SAMPLE_REQUIRED_CONFIGS` | kernel configs that must be `=y`/`=m` (e.g. `CONFIG_KPROBES`) |
| `SAMPLE_SUPPORTED_ARCH` | allowed arches (e.g. `x86 x86_64`) |
| `SAMPLE_MIN_KVER` | minimum kernel version (e.g. `5.14`) |

## Coding style

Strict Linux kernel style (hard tabs, 8-column width; see `.clang-format` and
`.editorconfig`) with two deliberate exceptions: braces are never omitted, even
for a single statement, and the column limit is not strictly enforced. clangd is
intentionally not used for this repo — without the exact kernel build flags it
reports false errors on kernel headers and macros; tree-sitter syntax
highlighting still works.
