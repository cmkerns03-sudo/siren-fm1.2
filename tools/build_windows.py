#!/usr/bin/env python3
"""Generate assets on Windows, then compile with the JieLi toolchain in WSL.

No system packages are installed. --toolchain is an existing Linux path;
--sdk is a Windows directory containing cpu/wl82/tools from the pinned SDK.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--distro', required=True)
    ap.add_argument('--toolchain', required=True)
    ap.add_argument('--sdk', type=Path, required=True)
    ap.add_argument('--skip-generate', action='store_true')
    ap.add_argument('--slice', choices=['0', '1'], help='FELUCCA_SLICE for the compile (the SLICE engine); '
                    'the BREAK sample follows FELUCCA_SLICE in this process environment (tools/gen_samples.py)')
    ap.add_argument('--nvoice', choices=['8', '12', '16'], help='NVOICE for the compile (Clipper: the voices; default 8)')
    ap.add_argument('--siren', choices=['0', '1'], help='SIREN=1: the SIREN firmware (siren.c) instead of Clipper')
    ap.add_argument('--abyss', choices=['0', '1'], help='ABYSS=1 (with --siren 1): the ABYSS firmware (abyss.c)')
    args = ap.parse_args()
    os.chdir(ROOT)
    import build
    if not args.skip_generate:
        build.generate()
    wsl = ['wsl', '-d', args.distro, '--exec']
    def linux_path(p):
        return subprocess.check_output(wsl + ['wslpath', '-a', p.resolve().as_posix()], text=True).strip()
    src, sdk = linux_path(ROOT), linux_path(args.sdk)
    code = 'import sys;sys.path.insert(0,"tools");import build;build.generate=lambda:None;sys.exit(build.main())'
    extra = ['FELUCCA_SLICE=' + args.slice] if args.slice else []
    if args.nvoice:
        extra.append('NVOICE=' + args.nvoice)
    if args.siren:
        extra.append('SIREN=' + args.siren)
    if args.abyss:
        extra.append('ABYSS=' + args.abyss)
    subprocess.run(['wsl', '-d', args.distro, '--cd', src, '--exec', 'env',
                    'JIELI_TOOLCHAIN=' + args.toolchain, 'AC79_SDK=' + sdk, *extra,
                    'python3', '-c', code], check=True)

if __name__ == '__main__':
    main()
