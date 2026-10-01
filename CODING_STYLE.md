# Coding style

Linux kernel style ([coding-style.rst]) applies unless this page says otherwise.
`.clang-format` and `.editorconfig` encode the layout; format C sources with
`clang-format -i` before committing.

## Differences from the kernel

| Topic | Kernel | This repo |
|---|---|---|
| Line length | 80 preferred | 100 (checkpatch's limit); user-visible strings and verbatim log captures in comments are never split |
| File-scope variables | descriptive names | `gs_` prefix when `static`, `g_` otherwise |
| Event counters | free-form | `_count` suffix, as in `gs_seen_count` |
| Function names | free-form | start with a verb or predicate: `get_`, `print_`, `should_` |
| Module entry points | `<name>_init`, `<name>_exit` | `<mod>_module_init`, `<mod>_module_exit`, where `<mod>` is the module name |
| Load and unload logs | free-form | init ends with `loaded` and exit with `unloaded` (`; <facts>` may follow); `interrupts/` samples open both with an `init:` / `exit:` context line |
| IRQ and CPU spelling | mixed | `IRQ` in prose, `irq` in log messages and identifiers; CPUs log as `CPU#<n>` |
| Minimum kernel | n/a in-tree | 6.12 for every sample; a `sample.mk` may raise it |
| License | SPDX `GPL-2.0`, `MODULE_LICENSE("GPL")` | SPDX `0BSD`, `MODULE_LICENSE("Dual BSD/GPL")` |
| `MODULE_VERSION` | usually omitted in-tree | `"1.0"` in every module |

Registered callbacks keep the kernel's role-suffix names instead of a leading
verb: `<prefix>_top_half`, `<prefix>_bottom_half`, `<prefix>_hook`, the fops
handler `<file>_write`, and the smpboot pair `<thread>_should_run` /
`run_<thread>`. The prefix names the sample or its role, as in
`hardirq_danger_top_half`.

## Kernel practice made mandatory

- `#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt` on the line after the SPDX tag.
- Include the header of every facility used; `<linux/*>` first, then other
  groups, each sorted.
- File order: includes, defines, types, variables, functions, `module_init`/
  `module_exit`, `MODULE_*`. An ops table follows the functions it points to.
- One `pr_*()` call per output line. A message starts lowercase unless it opens
  with an identifier or acronym.
- `MODULE_DESCRIPTION` is a sentence-case noun phrase without a final period.
- Comments only where the code cannot say it; `//` only for the SPDX line.
- Version-dependent code uses `LINUX_VERSION_CODE`.

[coding-style.rst]: https://www.kernel.org/doc/html/latest/process/coding-style.html
