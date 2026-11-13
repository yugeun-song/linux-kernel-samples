#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
import argparse
import glob
import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))
SAVED = re.compile(r"^savedcmd_(\S+\.o) := (.*)$")
SOURCE = re.compile(r"^source_(\S+\.o) := (\S+\.c)$")
DEPS = re.compile(r"^deps_\S+ := \\$")
WITH_VALUE = {"-include", "-isystem", "-o", "-MF", "-x", "-idirafter", "-imacros"}
NO_PROBE = ("-I", "-D", "-U", "-Wp,")
KCONFIG = "/include/linux/kconfig.h"


def parse(cmdfile):
    cmd = src = None
    deps = []
    in_deps = False
    with open(cmdfile, errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if in_deps:
                path = line.strip().rstrip("\\").strip()
                if not path:
                    in_deps = False
                elif not path.startswith("$("):
                    deps.append(path)
                continue
            if DEPS.match(line):
                in_deps = True
                continue
            m = SAVED.match(line)
            if m:
                cmd = m.group(2)
            m = SOURCE.match(line)
            if m:
                src = m.group(2)
    return cmd, src, deps


def split(cmd):
    tokens = shlex.split(cmd.replace("\\#", "#").replace("$$", "$"))
    if ";" in tokens:
        tokens = tokens[: tokens.index(";")]
    return [t for t in tokens if not t.startswith("-Wp,-MMD") and not t.startswith("-Wp,-MD")]


def triple_of(args):
    name = os.path.basename(args[0])
    if name.endswith("-gcc"):
        return name[: -len("-gcc")]
    return next((a.split("=", 1)[1] for a in args if a.startswith("--target=")), None)


def probe(flag, triple):
    argv = ["clang", "-fsyntax-only", "-x", "c", "-c", os.devnull,
            "-Werror=unknown-warning-option", "-Werror=ignored-optimization-argument"]
    if triple:
        argv.append(f"--target={triple}")
    argv.append(flag)
    return subprocess.run(argv, stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL).returncode == 0


def candidates(args):
    skip = False
    for a in args[1:]:
        if skip:
            skip = False
            continue
        if a in WITH_VALUE:
            skip = True
            continue
        if a.startswith("-") and not a.startswith(NO_PROBE) and a not in ("-c", "-nostdinc"):
            yield a


def stable_tree(args, cwd, dirs):
    inc = next((a for a in args if a.endswith(KCONFIG)), None)
    if inc is None:
        return None
    tree = os.path.realpath(os.path.join(cwd, inc[: -len(KCONFIG)]))
    for link in sorted(p for d in dirs for p in glob.glob(os.path.join(os.path.expanduser(d), "*"))):
        if os.path.islink(link) and os.path.realpath(link) == tree:
            return tree, link
    return None


def in_repo(path):
    rel = os.path.relpath(path, ROOT)
    return not rel.startswith("..") and not any(p.startswith(".") for p in rel.split(os.sep))


def sources():
    for top, dirs, names in os.walk(ROOT):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for name in sorted(names):
            if name.endswith((".c", ".h")):
                yield os.path.join(top, name)


def write(path, text):
    with open(path + ".tmp", "w") as f:
        f.write(text)
    os.replace(path + ".tmp", path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kdir", required=True)
    ap.add_argument("--profile", default="")
    ap.add_argument("--link-dirs", default="")
    ap.add_argument("--map", action="append", default=[])
    ap.add_argument("--out", default=os.path.join(ROOT, "compile_commands.json"))
    ap.add_argument("--files", default=os.path.join(ROOT, "cscope.files"))
    opts = ap.parse_args()
    kdir = os.path.realpath(opts.kdir)

    raw = []
    for bdir in sorted(glob.glob(os.path.join(ROOT, "**", ".build-*"), recursive=True)):
        try:
            with open(os.path.join(bdir, ".kdir")) as f:
                if f.read().strip() != kdir:
                    continue
        except OSError:
            continue
        for cmdfile in sorted(glob.glob(os.path.join(bdir, ".*.o.cmd"))):
            cmd, src, deps = parse(cmdfile)
            if not cmd or not src:
                continue
            cwd = kdir if os.path.isabs(src) else bdir
            real = os.path.realpath(os.path.join(cwd, src))
            if not in_repo(real):
                continue
            args = split(cmd)
            deps = [os.path.normpath(os.path.join(cwd, d)) for d in deps]
            raw.append((cwd, src, real, args, triple_of(args), deps))

    wanted = {(f, t) for _, _, _, args, t, _ in raw for f in candidates(args)}
    with ThreadPoolExecutor(max_workers=len(os.sched_getaffinity(0))) as pool:
        verdicts = dict(zip(wanted, pool.map(lambda k: probe(*k), wanted)))
    rejected = sorted({f for (f, _), ok in verdicts.items() if not ok})

    entries = []
    headers = set()
    links = set()
    for cwd, src, real, args, triple, deps in raw:
        kept = [args[0]]
        for a in args[1:]:
            if a == src:
                kept.append(real)
            elif verdicts.get((a, triple), True):
                kept.append(a)
        if triple and not any(a.startswith("--target=") for a in kept):
            kept.insert(1, f"--target={triple}")
        kept.insert(1, "-Qunused-arguments")
        for old, new in (m.split("=", 1) for m in opts.map):
            tree = re.compile(re.escape(old) + r"(?=/|$)")
            cwd = tree.sub(new, cwd)
            kept = [tree.sub(new, a) for a in kept]
            deps = [tree.sub(new, d) for d in deps]
        alias = stable_tree(kept, cwd, opts.link_dirs.split())
        if alias:
            links.add(alias[1])
            tree = re.compile(re.escape(alias[0]) + r"(?=/|$)")
            cwd = tree.sub(alias[1], cwd)
            kept = [tree.sub(alias[1], a) for a in kept]
            deps = [tree.sub(alias[1], d) for d in deps]
        entries.append({"directory": cwd, "file": real, "arguments": kept})
        headers.update(d for d in deps if not in_repo(d))

    have = {e["file"] for e in entries}
    missing = [os.path.relpath(p, ROOT) for p in sources() if p.endswith(".c") and p not in have]
    if missing:
        print(f"compile_commands.json: not built for {kdir}: {' '.join(missing)}; run make")
        sys.exit(1)

    repo = [os.path.relpath(p, ROOT) for p in sources()]
    write(opts.out, json.dumps(entries, indent=1) + "\n")
    write(opts.files, "\n".join(repo + sorted(headers)) + "\n")

    via = f", kernel via {' '.join(sorted(links))}" if links else ""
    flags = f" with the {opts.profile} flags" if opts.profile else ""
    print(f"=> compile_commands.json: {len(entries)} entries{flags} for {kdir}{via}, "
          f"{len(rejected)} gcc-only flags dropped")
    for flag in rejected:
        print(f"  dropped {flag}")
    print(f"=> cscope.files: {len(repo)} repository files and {len(headers)} kernel headers")


if __name__ == "__main__":
    main()
