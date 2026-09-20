#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
import glob
import json
import os
import re
import shlex
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SAVED = re.compile(r"^savedcmd_(\S+\.o) := (.*)$")
SOURCE = re.compile(r"^source_(\S+\.o) := (\S+\.c)$")
WITH_VALUE = {"-include", "-isystem", "-o", "-MF", "-x", "-idirafter", "-imacros"}
NO_PROBE = ("-I", "-D", "-U", "-Wp,")


def parse(cmdfile):
    cmd = src = None
    with open(cmdfile, errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            m = SAVED.match(line)
            if m:
                cmd = m.group(2)
            m = SOURCE.match(line)
            if m:
                src = m.group(2)
            if cmd and src:
                break
    return cmd, src


def split(cmd):
    tokens = shlex.split(cmd.replace("\\#", "#").replace("$$", "$"))
    if ";" in tokens:
        tokens = tokens[: tokens.index(";")]
    return [t for t in tokens if not t.startswith("-Wp,-MMD") and not t.startswith("-Wp,-MD")]


def triple_of(driver):
    name = os.path.basename(driver)
    return name[: -len("-gcc")] if name.endswith("-gcc") else None


def probe(flag, triple):
    argv = ["clang", "-fsyntax-only", "-x", "c", "-c", os.devnull,
            "-Werror=unknown-warning-option", "-Werror=ignored-optimization-argument"]
    if triple:
        argv.append(f"--target={triple}")
    argv.append(flag)
    return subprocess.run(argv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


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


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "compile_commands.json")
    raw = []
    for bdir in sorted(glob.glob(os.path.join(ROOT, "**", ".build-*"), recursive=True)):
        if not os.path.isdir(bdir):
            continue
        for cmdfile in sorted(glob.glob(os.path.join(bdir, ".*.o.cmd"))):
            cmd, src = parse(cmdfile)
            if not cmd or not src:
                continue
            real = os.path.realpath(os.path.join(bdir, src))
            if not real.startswith(ROOT + os.sep):
                continue
            if any(p.startswith(".build-") for p in os.path.relpath(real, ROOT).split(os.sep)):
                continue
            args = split(cmd)
            raw.append((bdir, src, real, args, triple_of(args[0])))

    wanted = {(f, t) for _, _, _, args, t in raw for f in candidates(args)}
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        verdicts = dict(zip(wanted, pool.map(lambda k: probe(*k), wanted)))
    rejected = sorted({f for (f, _), ok in verdicts.items() if not ok})

    entries = []
    for bdir, src, real, args, triple in raw:
        kept = [args[0]]
        for a in args[1:]:
            if a == src:
                kept.append(real)
            elif verdicts.get((a, triple), True):
                kept.append(a)
        if triple:
            kept.insert(1, f"--target={triple}")
        kept.insert(1, "-Qunused-arguments")
        entries.append({"directory": bdir, "file": real, "arguments": kept})

    tmp = out + ".tmp"
    with open(tmp, "w") as f:
        json.dump(entries, f, indent=1)
        f.write("\n")
    os.replace(tmp, out)
    print(f"compile_commands.json: {len(entries)} entries, {len(rejected)} gcc-only flags dropped")
    for flag in rejected:
        print(f"  dropped {flag}")


if __name__ == "__main__":
    main()
