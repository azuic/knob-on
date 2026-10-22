#!/bin/bash
# Native or emulated Linux x64 only. Installs nothing and preserves source objects.
set -euo pipefail
case "$(uname -s):$(uname -m)" in
    Linux:x86_64) ;;
    *) echo 'Run in Linux x64; a Mac cross-build is not Linux runtime verification.' >&2; exit 1 ;;
esac
cd "$(dirname "$0")/.."
root="$PWD"
if [ -d "$root/rack/src" ]; then plugin="$root/rack"; else plugin="$root"; fi
sdk="${RACK_LINUX_SDK:-$root/.deps/Rack-SDK-lin-x64}"
cxx="${CXX:-g++}"
cc="${CC:-gcc}"
for tool in "$cxx" "$cc" make jq python3 zstd strip readelf sha256sum; do
    command -v "$tool" >/dev/null || { echo "Missing $tool; see docs/linux.md." >&2; exit 1; }
done
for compiler in "$cc" "$cxx"; do
    case "$("$compiler" -dumpmachine)" in
        x86_64*-linux*) ;;
        *) echo 'Use a Linux x64 GCC toolchain matching the Rack SDK.' >&2; exit 1 ;;
    esac
done
if [ ! -f "$sdk/include/rack.hpp" ] || [ ! -f "$sdk/libRack.so" ]; then
    echo 'Set RACK_LINUX_SDK to the official Linux x64 Rack SDK.' >&2; exit 1
fi
sdk=$(cd "$sdk" && pwd)
for path in "$root" "$sdk"; do
    case "$path" in *' '*) echo 'Rack Makefiles require paths without spaces.' >&2; exit 1 ;; esac
done
stage="$root/build/linux/rack"
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
make -C "$stage" -j"${JOBS:-2}" dist RACK_DIR="$sdk" CC="$cc" CXX="$cxx"
readelf -h -d "$stage/plugin.so" > "$root/build/linux/plugin-elf.txt"
readelf --version-info "$stage/plugin.so" > "$root/build/linux/plugin-symbol-versions.txt"
slug=$(jq -r .slug "$plugin/plugin.json")
version=$(jq -r .version "$plugin/plugin.json")
package="$slug-$version-lin-x64.vcvplugin"
cp "$stage/dist/$package" "$root/build/linux/$package"
(cd "$root/build/linux" && sha256sum "$package" > "$package.sha256")
echo "Linux x64 package: build/linux/$package"
echo 'Build only. Run Linux tests and isolated Rack loading before claiming runtime support.'
