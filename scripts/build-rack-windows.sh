#!/bin/bash
# Native MSYS2 MinGW64 or a GCC MinGW-w64 cross-compiler. Installs nothing.
set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
if [ -d "$root/rack/src" ]; then plugin="$root/rack"; else plugin="$root"; fi
sdk="${RACK_WINDOWS_SDK:-$root/.deps/Rack-SDK-win-x64}"
cxx="${CXX:-x86_64-w64-mingw32-g++}"
cc="${CC:-x86_64-w64-mingw32-gcc}"
strip="${STRIP:-x86_64-w64-mingw32-strip}"
objdump="${OBJDUMP:-x86_64-w64-mingw32-objdump}"
for tool in "$cxx" "$cc" "$strip" "$objdump" make jq python3 zstd; do
    if ! command -v "$tool" >/dev/null; then
        echo "Missing $tool. See docs/windows.md; this script does not install tools." >&2
        exit 1
    fi
done
python3 scripts/check-windows-abi.py toolchain "$cxx" "$cc"
if [ ! -f "$sdk/include/rack.hpp" ] || [ ! -f "$sdk/libRack.dll.a" ]; then
    echo "Extract the Windows x64 Rack SDK into $sdk, or set RACK_WINDOWS_SDK." >&2
    exit 1
fi
sdk=$(cd "$sdk" && pwd)
for path in "$root" "$sdk"; do
    case "$path" in
        *' '*) echo 'Rack Makefiles require project and SDK paths without spaces.' >&2; exit 1 ;;
    esac
done
# Separate objects and dist from the existing Mac build. Replace only this
# script's generated staging folder, never the source tree or installed plugin.
stage="$root/build/windows/rack"
python3 - "$plugin" "$stage" <<'PY'
from pathlib import Path
import shutil, sys
source, stage = map(Path, sys.argv[1:])
if stage.exists():
    shutil.rmtree(stage)
stage.mkdir(parents=True)
for name in ('Makefile', 'plugin.json', 'LICENSE', 'src', 'res'):
    src, dst = source / name, stage / name
    if src.is_dir(): shutil.copytree(src, dst)
    else: shutil.copy2(src, dst)
PY
make -C "$stage" -j4 RACK_DIR="$sdk" CC="$cc" CXX="$cxx"
python3 scripts/check-windows-abi.py binary "$stage/plugin.dll" --objdump "$objdump"
"$strip" -s "$stage/plugin.dll"
# The SDK's non-Mac dist recipe needs GNU cp --parents. Package with Python
# instead, so the same script also works on a Mac with a cross-compiler.
python3 - "$stage" "$root/build/windows" <<'PY'
from pathlib import Path
import hashlib, json, subprocess, sys, tarfile
stage, output = map(Path, sys.argv[1:])
manifest = json.loads((stage / 'plugin.json').read_text())
slug, version = manifest['slug'], manifest['version']
package = output / f'{slug}-{version}-win-x64.vcvplugin'
tar = output / f'{slug}-win-x64.tar'
with tarfile.open(tar, 'w', format=tarfile.USTAR_FORMAT) as archive:
    for name in ('plugin.dll', 'plugin.json', 'LICENSE', 'res'):
        archive.add(stage / name, arcname=f'{slug}/{name}')
subprocess.run(['zstd', '-19', '-f', str(tar), '-o', str(package)], check=True)
tar.unlink()
(output / (package.name + '.sha256')).write_text(
    hashlib.sha256(package.read_bytes()).hexdigest() + '  ' + package.name + '\n')
print(f'Windows x64 package: {package}')
print('Build only. Loading in Rack and real-device validation on Windows are still required.')
PY
