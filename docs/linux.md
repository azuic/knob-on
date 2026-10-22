# Linux companion validation

Linux x64 build and runtime checks passed on Ubuntu 22.04 under Docker/Rosetta
emulation on Apple silicon with Rack Free 2.6.6. This includes actual Rack
application loading, sixteen-control writes, independent Fundamental VCO/VCF
feedback, reconnect and saved-patch restoration. See the
[validation record](linux-validation.json) for exact scope and hashes. Native
desktop, physical-device and older-distribution acceptance remain outstanding.
The published v2.3.1 release remains the Mac preview.

## Connection

Linux uses manual IPv4 connection. Bridge enumerates active non-loopback IPv4
interfaces that are up/running and prefers the interface of the lowest-metric default route in
`/proc/net/route`. Without a usable route it shows the first usable address;
without an address it asks the user to check network settings. It refreshes every
five seconds. It excludes unspecified, loopback, link-local, multicast and
broadcast addresses. A hostname ending in `.local` is not promised.

Linux does not register an mDNS service and does not require Avahi or Bonjour.
The panel says **connect by address**. With VPNs, multiple adapters, VMs or
containers, verify the address against `ip -4 address` and use the LAN address
reachable by the phone. A container's internal address is not the Mac's LAN
address. Automated container tests do not certify phone connectivity or mDNS.

## Existing Linux environment

Use a Linux x64 environment, official Rack SDK 2.6.6 for Linux x64, GCC/make,
Python 3, jq, zstd, binutils and Rack's required Linux libraries. The scripts
install nothing. Request approval before installing missing system software.
See VCV's [build dependencies](https://vcvrack.com/manual/Building) and
[runtime instructions](https://vcvrack.com/manual/Installing).

```bash
export RACK_LINUX_SDK=/absolute/path/to/Rack-SDK
bash scripts/build-rack-linux.sh
bash scripts/test-rack-linux.sh
```

The package and SHA-256 sidecar are in `build/linux/`. Builds stage source and
objects in `build/linux/rack`, leaving source-tree objects and installed plugins
alone. ELF imports and symbol versions are recorded alongside the package;
building on one distribution does not establish compatibility with every glibc
version. The scripts reject non-Linux and non-x64 environments.

Tests compile address-selection checks, production socket checks, and the Rack
engine harness, then run eleven Python TCP tests against that harness. The harness
uses real Rack `Engine`/`ParamQuantity` APIs and production Bridge code, without
opening a Rack GUI or audio device. Ports 18057 and 18058 are dedicated test
ports. Do not run overlapping suites. Tests fail if the listener cannot bind.

## Isolated Docker environment on Apple silicon

Install/start Docker only after approval. The test environment uses the official
Ubuntu 22.04 image and Docker's Linux x64 emulation. This is execution inside a
Linux kernel with emulated x64 instructions, and must be reported as such.

```bash
docker build --platform linux/amd64 -t knobon-linux-tests:2.6.6 tests/linux
docker run --rm --platform linux/amd64 --network none --cpus 2 --memory 4g \
  --mount type=bind,src="$PWD",dst=/work \
  knobon-linux-tests:2.6.6 bash scripts/build-rack-linux.sh
docker run --rm --platform linux/amd64 --network none --cpus 2 --memory 4g \
  --mount type=bind,src="$PWD",dst=/work \
  knobon-linux-tests:2.6.6 bash scripts/test-rack-linux.sh
```

To exercise the packaged plugin in the actual Rack application:

```bash
docker run --rm --platform linux/amd64 --network none --cpus 2 --memory 4g \
  --mount type=bind,src="$PWD",dst=/work \
  knobon-linux-tests:2.6.6 python3 scripts/test-rack-linux-app.py
```

The application test uses Xvfb and Mesa software OpenGL, a newly created Rack
profile and the package from `build/linux/`. It independently reads/writes the
real Fundamental VCO Frequency and VCF Cutoff parameters; fourteen other mapped
parameters, including a stepped switch, are provided by a test-only probe. The probe
also saves the patch and requests clean application shutdown. It is not included
in the product package. Test results/logs remain under `build/linux/app-smoke-*`.
An occupied port aborts before the test connects to Rack.

Run from the separate Linux checkout. Only this checkout is mounted; container
ports are not published to the Mac. Image downloads and package installation
happen inside the test image. Record the Ubuntu image digest, Docker version,
resource limits, compiler/glibc versions, Rack downloads' SHA-256 values, tested
source commit and package hash. `/opt/knobon/download-sha256.txt` records the Rack
download hashes. A SHA-256 recorded after download is identification evidence,
not an independently published vendor checksum.

## Application and device acceptance

For native desktop acceptance use a fresh user folder passed with Rack's `--user`
option, containing only the candidate plugin. Start with an empty patch and then
a Bridge-only patch. Capture
Rack's version, logs and the candidate hash. Verify plugin loading, TCP handshake,
disconnect/reconnect, shutdown and patch save/reload. An Xvfb/software-OpenGL
application test is an isolated runtime smoke test; it does not certify a native
desktop's graphics/audio drivers or human-operated GUI gestures.

On an approved Linux desktop with a phone on the same LAN, check manual Learn,
phone-to-Rack writes, independent Rack-to-phone feedback, save/reload, target
deletion, reconnect, all sixteen slots and stepped parameters. Record tested
Linux/Rack/device versions and any firewall action separately. Keep normal
patches/profiles out of automated testing. The application test uses port 8057
only inside its container; never publish it to the Mac. Do not claim these
device results from a harness or emulated application smoke test.

Update the existing [VCV submission](https://github.com/VCVRack/library/issues/988)
only after meaningful Linux execution passes, identifying the exact tested
commit/version and remaining limitations. Do not open a duplicate issue or
describe a submission comment as Library approval.
