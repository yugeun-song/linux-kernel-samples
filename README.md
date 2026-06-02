# linux-kernel-samples

> **DISCLAIMER — read this before you build or load anything here.**
>
> These are **out-of-tree Linux kernel modules** written for learning. A kernel
> module runs in kernel space with full privileges, so a single bug can take down
> the **entire machine**, not just one process.
>
> **Do NOT load these on your host / daily-driver OS.** Use a throwaway virtual
> machine or a spare/dev kernel you can afford to crash — not a system whose data
> or uptime you care about.
>
> **No warranty, no liability.** The code is provided strictly "as is" under 0BSD
> (see [`LICENSE`](LICENSE)). The author accepts **no responsibility whatsoever**
> for any damage or loss of any kind, including without limitation: security
> compromise, kernel panic or hang, data loss or filesystem corruption, hardware
> damage, a tainted or lockdown-restricted kernel, or a system left unbootable.
> **You build, load, and run these modules entirely at your own risk.**

Runnable Linux kernel modules for learning the kernel by doing. Each sample
isolates one **main theme** — a struct, function, or macro — as a loadable
module you build, `insmod`, observe in `dmesg`, and `rmmod`.

The build system, coding-style configuration, and conventions are in place;
sample modules are added on demand.
The sections below double as study notes and as context for any tooling
(including AI assistant) that needs to understand how the repo is
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
| `smp/` | per-cpu data, IPIs, cpumask, CPU-bound kthreads, CPU hotplug |
| `time/` | jiffies, timers, hrtimer, delays |
| `fs/` | file_operations, procfs/sysfs/debugfs, char devices |
| `net/` | sk_buff, netdev, netfilter |
| `arch/` | barriers, MSR/CR, arch-specific details |
| `lib/` | generic data structures: list, rbtree, hashtable, idr |
| `core/` | cross-cutting primitives: container_of, kref, ERR_PTR |

Placement rule: **one main theme per sample**, filed under the directory that
best represents that theme (its "primary owner"). Cross-cutting primitives with
no owner go in `lib/` (data structures) or `core/` (container_of, kref, ERR_PTR,
likely/unlikely, ...). Directories are created on demand as samples are added.

A sample is a single `.c` file under its theme, optionally grouped in a topic
sub-folder: `<theme>/[<group>/]<name>.c`. The module is named after the source
file (`<name>.ko`), and related samples share a topic folder (e.g.
`smp/percpu/percpu_parallel.c`), mirroring the kernel's own `samples/` layout. A sample
documents itself by printing to the kernel log, so `dmesg` after `insmod` is the
explanation.

Samples are registered explicitly, the way the kernel lists every object in its
Kbuild files: there is no globbing. The top-level `Makefile` holds a `SAMPLES`
list, and a sample is built only once it is added there (see "Registering a
sample"). Because only the registered module source (`<sample>.c`, or the
objects a `sample.mk` declares) is compiled, a userspace companion file next to
it may be named anything and is simply ignored by the module build.

## Anatomy of a sample (composition principle)

A kernel module is not a `main()` program. It is object code linked into the
running kernel, exposing two hooks the module loader calls. The minimal shape:

```c
// SPDX-License-Identifier: 0BSD
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
- `// SPDX-License-Identifier: 0BSD` on the first line is the file's copyright
  license — 0BSD: use, modify, and redistribute freely, no attribution required
  (full text in `LICENSE`). The tag rides with the file if a sample is copied out.
- `MODULE_LICENSE("GPL")` is the kernel-runtime tag, distinct from the SPDX
  copyright line: a non-GPL string taints the kernel and blocks access to
  GPL-only exported symbols, so it stays `"GPL"` to keep e.g. the kthread API
  usable. `MODULE_AUTHOR` and `MODULE_DESCRIPTION` are `modinfo` metadata.

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
  |    - reads the explicit SAMPLES list (no globbing)
  |    - applies defaults (host KVER/KDIR; ARCH/CROSS_COMPILE empty)
  |    - -include config.mk   (optional, PC-local persistent target)
  |
  +- scripts/kmod.mk   (per-sample driver; one recursive make per sample)
       - -include <topic dir>/sample.mk   (optional requirements)
       - module source: <name>.c, or the objects sample.mk declares
       - preflight: $(error) and stop if the target is unsupported
       - generates <topic dir>/Kbuild  (obj-m := <name>.o), git-ignored
       |
       +- make -C $(KDIR) [ARCH=.. CROSS_COMPILE=..] M=<abs topic dir> modules
            - kernel build: CC -> MODPOST -> CC mod.o -> LD .ko -> BTF
```

- **Explicit, not globbed.** Like the kernel's `obj-m`, the build target list is
  declared, never inferred from whatever `.c` files happen to be present. This is
  deterministic, keeps stray or work-in-progress files out of the build, and lets
  module sources, userspace helpers, and generated files share a directory
  safely.
- **One entry point.** The top `Makefile` is the only thing you run. The
  generated `Kbuild` is a one-line `obj-m` manifest that kbuild's `M=` interface
  requires, so it is created at build time rather than committed (the repo tracks
  sources and the build/doc files, never generated artifacts).
- **Hard-fail preflight.** Before invoking the kernel build, the driver stops
  with `$(error ...)` when: the kernel build tree is missing, a requested cross
  compiler is absent, a sample's required `CONFIG` is off, the arch is
  unsupported, or the kernel is older than a sample's minimum. An unsupported
  target becomes an immediate, explicit failure instead of a confusing deep error
  from inside the kernel build.

### Registering a sample

A sample is built only when it is listed, mirroring the kernel's per-directory
`obj-m`. Add the source at `<theme>/[<group>/]<name>.c`, then add the sample to
the `SAMPLES` list in the top-level `Makefile`:

```make
SAMPLES := \
	smp/percpu/percpu_parallel
```

Use a `sample.mk` only for a multi-file module or special build requirements
(see the contract below).

### Running the build

```
make                       # build every registered sample
make <theme>/<sample>      # build one, e.g. make smp/percpu/percpu_parallel
make clean                 # clean every registered sample
make list                  # list registered samples
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

Build prerequisites: matching kernel headers (`linux-headers-$(uname -r)` on
Debian/Ubuntu, `kernel-devel` on Fedora), a C toolchain (`build-essential` or
`@development-tools`), and root to load.

Compiling is harmless; only **loading** is dangerous. The recommended path for
everyone — and the only sane path for a newcomer — is a throwaway VM (next
section). To load directly on the running kernel, only on a machine you can
afford to crash:

```
sudo insmod smp/percpu/percpu_parallel.ko   # the .ko sits next to its source
dmesg | tail
sudo rmmod percpu_parallel
```

#### Try it safely in a VM (no host risk)

A module can only be loaded *into a kernel*, so the only safe way to test one is
to give it a disposable kernel — a virtual machine. Containers (Docker, Podman,
Fedora toolbox) do **not** help: they share the host kernel, so a bug still
crashes the host.

- **Beginner-friendly:** launch a throwaway Ubuntu/Fedora VM (`multipass`, GNOME
  Boxes, virt-manager, or VirtualBox), install the prerequisites inside it, then
  run the normal `make` + `insmod` + `dmesg` + `rmmod` there. If it crashes, you
  just discard the VM.
- **Fastest for kernel work:** [virtme-ng] (`vng`) boots your *current* kernel in
  a QEMU VM in seconds, with this directory available and no disk image to build.
  Install it (`sudo apt install qemu-system-x86 virtme-ng` on Ubuntu, `sudo dnf
  install qemu-system-x86 virtme-ng` on Fedora), run `vng` from the repo to get a
  throwaway VM on your own kernel, and do the normal `make` + `insmod` + `dmesg` +
  `rmmod` inside it. vermagic matches (same kernel), so no signing is needed, and a
  panic only kills the VM. See its docs for one-shot `vng -- <cmd>` usage.

[virtme-ng]: https://github.com/arighi/virtme-ng

#### Loading under Secure Boot (without turning it off)

On a Secure Boot system the kernel runs in lockdown and rejects unsigned
out-of-tree modules. You do **not** have to disable Secure Boot — sign the module
with your own Machine Owner Key (MOK):

```
# 1. Create a local signing key (once)
openssl req -new -x509 -newkey rsa:2048 -nodes -days 36500 \
    -keyout MOK.priv -outform DER -out MOK.der -subj "/CN=local module signing/"

# 2. Enroll the public key (once); reboot, then confirm in the blue MOK manager
sudo mokutil --import MOK.der

# 3. Sign the freshly built module (re-sign after every rebuild)
sudo /lib/modules/$(uname -r)/build/scripts/sign-file \
    sha256 MOK.priv MOK.der smp/percpu/percpu_parallel.ko
```

Once the key is enrolled and the module is signed, `insmod` accepts it under
Secure Boot. Signing only fixes *loadability* — the crash risk is unchanged, so
still prefer the VM above.

#### Why no prebuilt `.ko` is shipped

A `.ko` is bound to the exact kernel it was built against (its "vermagic"), so it
loads only there — which is why this repo ships **source** and builds per kernel.
The in-kernel API is also not stable across versions, so a sample may need
rebuilding (or a small fix) on a very different kernel.

## Per-sample build contract (`sample.mk`)

Only needed for a sample with special requirements; an ordinary sample needs
nothing but its `.c`. Recognized variables:

| variable | meaning |
|----------|---------|
| `SAMPLE_MODULE` | module name when it differs from the source file basename |
| `SAMPLE_OBJS` | object list for a multi-file module (`a.o b.o`) |
| `SAMPLE_REQUIRED_CONFIGS` | kernel configs that must be `=y`/`=m` (e.g. `CONFIG_KPROBES`) |
| `SAMPLE_SUPPORTED_ARCH` | allowed arches, in kbuild ARCH names (e.g. `x86 arm64`) |
| `SAMPLE_MIN_KVER` | minimum kernel version (e.g. `5.14`) |

## Coding style

Strict Linux kernel style (hard tabs, 8-column width; see `.clang-format` and
`.editorconfig`) with two deliberate exceptions: braces are never omitted, even
for a single statement, and the column limit is not strictly enforced. clangd is
intentionally not used for this repo — without the exact kernel build flags it
reports false errors on kernel headers and macros; tree-sitter syntax
highlighting still works.
