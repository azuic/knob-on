#!/bin/bash
# Run in an existing Windows MSYS2 MinGW64 environment after the build.
set -euo pipefail
cd "$(dirname "$0")/.."
root="$PWD"
if [ -d "$root/rack/src" ]; then plugin="$root/rack"; else plugin="$root"; fi
case "$(uname -s)" in MINGW*|MSYS*) ;; *) echo 'Run this on Windows; cross-compiling is not runtime verification.' >&2; exit 1 ;; esac
sdk="${RACK_WINDOWS_SDK:-$PWD/.deps/Rack-SDK-win-x64}"
cxx="${CXX:-x86_64-w64-mingw32-g++}"
objdump="${OBJDUMP:-x86_64-w64-mingw32-objdump}"
python3 scripts/check-windows-abi.py toolchain "$cxx"
if [ ! -f "$sdk/libRack.dll.a" ]; then echo 'Set RACK_WINDOWS_SDK to the Windows Rack SDK.' >&2; exit 1; fi
sdk=$(cd "$sdk" && pwd)
app="${RACK_WINDOWS_APP_DIR:-/c/Program Files/VCV/Rack2Free}"
if [ ! -f "$app/libRack.dll" ]; then echo 'Set RACK_WINDOWS_APP_DIR to your Rack application folder (containing libRack.dll).' >&2; exit 1; fi
export PATH="$app:$PATH"
mkdir -p build/windows
"$cxx" -std=c++11 -I"$plugin/src" tests/socket_checks.cpp -static-libstdc++ -static-libgcc -static \
    -lws2_32 -liphlpapi -o build/windows/socket_checks.exe
python3 scripts/check-windows-abi.py binary build/windows/socket_checks.exe --objdump "$objdump" --role standalone
./build/windows/socket_checks.exe
"$cxx" -std=c++11 -march=nehalem -D_USE_MATH_DEFINES -Wno-deprecated-declarations \
    -I"$plugin/src" -I"$sdk/include" -I"$sdk/dep/include" tests/rack_harness.cpp \
    -L"$sdk" -lRack -static-libstdc++ -static-libgcc \
    -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic -lws2_32 -liphlpapi \
    -o build/windows/rack_harness.exe
python3 scripts/check-windows-abi.py binary build/windows/rack_harness.exe --objdump "$objdump" --role harness
./build/windows/rack_harness.exe --self-test
export KNOB_RACK_HARNESS="$PWD/build/windows/rack_harness.exe"
python3 -m unittest discover -s tests -p 'test_bridge.py' -v
