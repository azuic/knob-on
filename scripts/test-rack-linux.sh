#!/bin/bash
# Run only inside Linux. Uses the SDK engine without GUI/audio or user patches.
set -euo pipefail
case "$(uname -s):$(uname -m)" in
    Linux:x86_64) ;;
    *) echo 'Run in Linux x64; cross-compilation is not runtime verification.' >&2; exit 1 ;;
esac
cd "$(dirname "$0")/.."
root="$PWD"
if [ -d "$root/rack/src" ]; then plugin="$root/rack"; else plugin="$root"; fi
sdk="${RACK_LINUX_SDK:-$root/.deps/Rack-SDK-lin-x64}"
cxx="${CXX:-g++}"
for tool in "$cxx" python3; do
    command -v "$tool" >/dev/null || { echo "Missing $tool; see docs/linux.md." >&2; exit 1; }
done
case "$("$cxx" -dumpmachine)" in
    x86_64*-linux*) ;;
    *) echo 'Use a Linux x64 GCC toolchain matching the Rack SDK.' >&2; exit 1 ;;
esac
if [ ! -f "$sdk/libRack.so" ] || [ ! -f "$sdk/include/rack.hpp" ]; then
    echo 'Set RACK_LINUX_SDK to the official Linux x64 Rack SDK.' >&2; exit 1
fi
sdk=$(cd "$sdk" && pwd)
mkdir -p build/linux
"$cxx" -std=c++11 -Wall -Wextra -Werror -I"$plugin/src" tests/linux_address_checks.cpp \
    -o build/linux/linux_address_checks
./build/linux/linux_address_checks
"$cxx" -std=c++11 -pthread -I"$plugin/src" tests/socket_checks.cpp -o build/linux/socket_checks
./build/linux/socket_checks
"$cxx" -std=c++11 -march=nehalem -pthread -Wno-deprecated-declarations \
    -I"$plugin/src" -I"$sdk/include" -I"$sdk/dep/include" tests/rack_harness.cpp \
    -L"$sdk" -lRack -Wl,-rpath,"$sdk" -o build/linux/rack_harness
./build/linux/rack_harness --self-test
export KNOB_RACK_HARNESS="$root/build/linux/rack_harness"
python3 -m unittest discover -s tests -p 'test_bridge.py' -v
echo 'Linux engine/socket/TCP tests passed. GUI and physical-device acceptance are separate.'
