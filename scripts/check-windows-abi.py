#!/usr/bin/env python3
"""Reject incompatible Rack 2 Windows builds before packaging or execution."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys


def run(arguments, **kwargs):
    return subprocess.run(arguments, text=True, check=True, capture_output=True, **kwargs).stdout


def check_toolchain(compilers):
    for compiler in compilers:
        target = run([compiler, '-dumpmachine']).strip()
        if target != 'x86_64-w64-mingw32':
            raise ValueError(f'{compiler}: expected Windows x64 MinGW-w64, got {target}')
        macros = run([compiler, '-dM', '-E', '-x', 'c++', '-'], input='#include <_mingw.h>\n')
        if re.search(r'^#define _UCRT(?:\s|$)', macros, re.M):
            raise ValueError(f'{compiler}: UCRT is incompatible with Rack 2.6.6 Windows. '
                             'Use a complete MSVCRT MinGW-w64 toolchain (MSYS2 MINGW64 or VCV toolchain). '
                             'Changing a macro or linker flag does not rebuild the compiler runtime.')
        print(f'{compiler}: Windows x64 / MSVCRT preflight passed')


def check_binary(path, objdump, role):
    if not Path(path).is_file():
        raise ValueError(f'Binary not found: {path}')
    dump = run([objdump, '-p', str(path)])
    if 'file format pei-x86-64' not in dump:
        raise ValueError('Expected a Windows x64 PE binary')
    imports = re.findall(r'DLL Name:\s*(\S+)', dump)
    lower = {name.lower() for name in imports}
    ucrt = sorted(name for name in lower if name.startswith('api-ms-win-crt-') or name.startswith('ucrtbase'))
    if ucrt:
        raise ValueError('Wrong CRT: binary imports UCRT (' + ', '.join(ucrt) + '). '
                         'Rack uses MSVCRT; do not package or load this DLL.')
    if 'msvcrt.dll' not in lower:
        raise ValueError('Binary does not explicitly import Rack-compatible msvcrt.dll')
    if role != 'standalone' and 'librack.dll' not in lower:
        raise ValueError('Binary does not import libRack.dll')
    if role == 'plugin' and not re.search(r'\binit\s*$', dump, re.M):
        raise ValueError('Missing Rack init export')
    extra = sorted(name for name in lower if name.startswith(('libgcc', 'libstdc++', 'libwinpthread')))
    if extra:
        raise ValueError('Unexpected compiler runtime DLLs: ' + ', '.join(extra))
    print(json.dumps({'binary': str(path), 'role': role, 'imports': imports,
                      'staticChecks': 'passed', 'runtimeVerification': 'not performed'}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    toolchain = commands.add_parser('toolchain')
    toolchain.add_argument('compilers', nargs='+')
    binary = commands.add_parser('binary')
    binary.add_argument('path')
    binary.add_argument('--objdump', default='x86_64-w64-mingw32-objdump')
    binary.add_argument('--role', choices=['plugin', 'harness', 'standalone'], default='plugin')
    args = parser.parse_args()
    try:
        if args.command == 'toolchain':
            check_toolchain(args.compilers)
        else:
            check_binary(args.path, args.objdump, args.role)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f'Windows ABI check failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
