#!/usr/bin/env python3
"""Post-build step: relink x86_64 ucrtbase.dll with the formatter stack probe.

pf_printf_a and pf_printf_w (dlls/msvcrt/printf.h, compiled into ucrtbase's wcs.o)
open with a group of register saves. Under Rosetta, a guard-page fault inside that
group cannot be emulated forward. This step compiles wcs.o to assembly with the build
tree's own recipe, inserts `movb -128(%rsp), %r11b` before the register saves of both
formatters (their frames exceed 128 bytes, so the load touches the frame's lowest
page first), reassembles, and relinks ucrtbase.dll with that object. No C source is
changed; the C cannot place an instruction ahead of the prologue.

usage: ucrt_wcs_probe.py BUILD_DIR [--output DLL] [-- MAKE_VARIABLE=VALUE ...]
The default output replaces BUILD_DIR/dlls/ucrtbase/x86_64-windows/ucrtbase.dll.
Extra MAKE_VARIABLE=VALUE pairs override Makefile variables (e.g. x86_64_CFLAGS).
"""
from pathlib import Path
import argparse, re, shlex, subprocess, sys

PROBE = '\n\t# Grow a current-stack guard before Rosetta groups register saves.\n\tmovb -128(%rsp), %r11b'


def recipe(makefile, variables, target):
    start = makefile.index(target + ':')
    start = makefile.index('\n\t', start) + 2
    end = makefile.index('\n', start)
    while makefile[end - 1] == '\\':
        end = makefile.index('\n', end + 1)
    value = makefile[start:end].replace('\\\n', ' ').replace('$@', target)
    for _ in range(8):
        value = re.sub(r'\$\((\w+)\)', lambda m: variables.get(m[1], m[0]), value)
    if '$' in value:
        raise ValueError('Unexpanded build variable in ' + target)
    return shlex.split(value)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('build', type=Path)
    ap.add_argument('--output', type=Path)
    ap.add_argument('overrides', nargs='*')
    args = ap.parse_args()
    build = args.build.resolve()
    makefile = (build / 'Makefile').read_text()
    variables = dict(re.findall(r'^(\w+) = (.*)$', makefile, re.M))
    variables.update(o.split('=', 1) for o in args.overrides)
    work = build / 'dlls/ucrtbase/x86_64-windows'
    compile_cmd = recipe(makefile, variables, 'dlls/ucrtbase/x86_64-windows/wcs.o')
    compile_cmd[compile_cmd.index('-c')] = '-S'
    compile_cmd[compile_cmd.index('-o') + 1] = str(work / 'wcs-probe.s')
    subprocess.run(compile_cmd, cwd=build, check=True)
    assembly = (work / 'wcs-probe.s').read_text()
    for name in ('pf_printf_a', 'pf_printf_w'):
        needle = '\t.seh_proc\t' + name + '\n' + name + ':'
        if assembly.count(needle) != 1:
            raise ValueError('Unexpected formatter assembly for ' + name)
        body = assembly.split(needle, 1)[1].split('.seh_endproc', 1)[0]
        frame = re.search(r'subq\s+\$(\d+),\s*%rsp', body)
        if not frame or int(frame[1]) < 128:
            raise ValueError('Formatter frame too small for the probe: ' + name)
        assembly = assembly.replace(needle, needle + PROBE)
    (work / 'wcs-probe.s').write_text(assembly)
    cc = compile_cmd[0]
    subprocess.run([cc, '-c', str(work / 'wcs-probe.s'), '-o', str(work / 'wcs-probe.o')], cwd=build, check=True)
    link = recipe(makefile, variables, 'dlls/ucrtbase/x86_64-windows/ucrtbase.dll')
    output = (args.output or work / 'ucrtbase.dll').resolve()
    link[link.index('-o') + 1] = str(output)
    link[link.index('dlls/ucrtbase/x86_64-windows/wcs.o')] = str(work / 'wcs-probe.o')
    subprocess.run(link, cwd=build, check=True)
    print('Relinked', output)


if __name__ == '__main__':
    sys.exit(main())
