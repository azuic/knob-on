# Windows companion

Status on 2026-10-22: basic Windows x64 testing has passed on a physical PC with Rack Free 2.6.6. The corrected test2 package was cross-compiled on macOS, then separately executed and tested on Windows. Automated socket and Rack allocator checks passed; the user confirmed GUI loading, physical iPhone/manual IPv4 connection, two-way control, and saved mappings/sync after reopening a patch. Normal-profile installation was verified against all five package-file hashes, and the user confirmed loading/sync there too. See the [validation record](windows-validation.json) for tested identities, source hashes, and limits.

**The first Windows PC test kit remains withdrawn after a reported crash. Do not load it again.** It used UCRT while official Rack 2.6.6 uses MSVCRT. Test2 uses GCC 13.2.0 / MinGW-w64 11.0.1 MSVCRT and corrects JSON buffer ownership. The original log did not establish an exact fault stack, so the precise crash cause remains unproven. The published v2.3.1 release download remains the Mac preview; no new release assets are published with this source update. Windows ARM64 is outside this change.

## What changed

The companion uses Winsock with nonblocking I/O, full-width socket handles, balanced startup/cleanup, Windows error handling, and exclusive port ownership. It keeps the existing sixteen-control protocol, UI-thread network budgets, mapping, persistence, heartbeat, and reconnection behavior. The iPhone/iPad app needs no Windows-specific transport or desktop helper.

Windows shows an active Ethernet/Wi-Fi IPv4 address. It prefers an adapter with a gateway and refreshes every five seconds. With multiple adapters, VPNs, or virtual Ethernet, check `ipconfig` and use the IPv4 address of the network shared with the phone. No `.local` name is promised for Windows.

Nearby discovery is optional on Windows. If a compatible 64-bit Bonjour runtime is already present at the Windows system directory's `dnssd.dll` and its service is running, the plugin registers `_knobon._tcp` through that API. It loads no DLL from the patch or working directory. If Bonjour is absent or registration fails, the panel says **connect by address** and TCP still works. Nothing installs Bonjour, edits the firewall, or changes services. Restart Rack after any separately approved Bonjour installation. This optional Windows path still needs runtime testing.

macOS retains its system Bonjour registration and local hostname behavior.

## Build using existing tools

Use Windows x64 Rack 2.6.6 or later and the official [Windows x64 Rack SDK 2.6.6](https://vcvrack.com/downloads/Rack-SDK-2.6.6-win-x64.zip). Extract the archive's `Rack-SDK` folder as `.deps/Rack-SDK-win-x64` without replacing a Mac SDK. Alternatively, set `RACK_WINDOWS_SDK` to its absolute path. The SDK contains `libRack.dll.a`; the runtime DLL is supplied by the installed Rack application.

On Windows, use an existing **MSYS2 MINGW64 / MSVCRT** shell with GCC, make, jq, Python 3, and zstd. Do not use UCRT64. VCV's [build guide](https://vcvrack.com/manual/Building) describes the toolchain. If these tools are missing, obtain approval to install them first. Keep antivirus enabled. This project's scripts install nothing. Use a project and SDK path without spaces, such as `C:\dev\knob-on`; the installed Rack application can remain in Program Files.

```bash
export RACK_WINDOWS_SDK=/c/dev/knob-on/.deps/Rack-SDK-win-x64
bash scripts/build-rack-windows.sh
export RACK_WINDOWS_APP_DIR='/c/Program Files/VCV/Rack2Free'
bash scripts/test-rack-windows.sh
```

Adjust the application path for Rack Pro or a different installation. Tests use dedicated ports 18057 and 18058; do not run two test suites at once. The Windows suite compiles and runs the production socket checks and Rack engine harness, then runs ten TCP tests against that harness. It does not compile the iOS app.

After a successful compatible build, the package will be `build/windows/KnobOn-2.3.1-win-x64.vcvplugin`, with a SHA-256 sidecar. **Existing packages from the first UCRT build are not valid replacements.** Staging uses `build/windows/rack`; it does not overwrite Mac objects or an installed plugin. The ABI checker requires MSVCRT and rejects UCRT, missing Rack exports, and additional compiler runtime DLLs. Bonjour remains optional. Version 2.3.1 is retained for this pending Library submission update so the source version matches the tested candidate; the original release tag and its Mac preview assets are unchanged.

On a Mac, the script needs a complete **MSVCRT GCC MinGW-w64** cross-compiler and the Windows SDK. The system Homebrew compiler is UCRT and is rejected. A project-local MSVCRT compiler is at `.deps/msvcrt-mingw/mingw-w64/11.0.1_1/toolchain-x86_64/bin`; set `CXX`, `CC`, `STRIP`, and `OBJDUMP` to its corresponding prefixed tools. It was extracted from the official Homebrew arm64 Sonoma bottle and verified against SHA-256 `166737c970ee5a025dc5ecfc36afd0850ed16a4a3b13ac7a1a706b7d98eb20b7`; relocation changes are recorded under `build/windows-crash/`. It does not replace the system compiler. A target triple alone does not establish CRT compatibility, and changing a macro/linker flag does not rebuild the C++ runtime. VCV's [reference toolchain](https://github.com/VCVRack/rack-plugin-toolchain/tree/v2) is another build option. Cross-compilation and static ABI checks do not establish runtime safety. Run the Windows checks in an isolated test patch before broader device testing.

## Real-PC acceptance

Do not repeat these steps with the withdrawn first kit. Test2 includes empty and Bridge-only diagnostic patches plus instructions for a fresh temporary Rack user folder. First preserve the original crash log, then validate the candidate in that isolated folder. Stop and capture its log if either phase fails. Keep the user's normal patch out of the initial crash investigation.

1. Quit Rack. Open Rack's user folder (Help → Open user folder), then place the built `.vcvplugin` in `plugins-win-x64` inside it. This is normally `%LOCALAPPDATA%\Rack2\plugins-win-x64`. Remove only an older KnobOn test copy if necessary; preserve other plugins. Reopen Rack and add **Knob On → Bridge**. If loading fails, retain `log.txt` from the user folder and record the package hash.
2. Put the iPhone/iPad and PC on the same trusted LAN. If Windows asks whether Rack may accept connections, approve only the network you intend to use. If a firewall rule is needed, obtain approval for that specific rule; do not disable the firewall. On the phone, allow Local Network access, choose **enter address manually**, and enter the panel's IPv4 address, without `http://` or a port suffix. Use `ipconfig` if the panel picked another adapter. TCP uses port 8057.
3. Hold a phone dial, then click a VCO Frequency knob in Rack. Drag the phone control, then move the Rack knob independently. Confirm the native name/value, smooth two-way feedback, no idle echo, and no pull under the finger while dragging. Repeat for a filter slider and a stepped parameter.
4. Exercise all sixteen slots on iPad (twelve dials plus four sliders); on iPhone check eight dials plus four sliders. Confirm independent mappings and values, save/reload the patch, delete a mapped target, and remap it. Test simultaneous touches and automation.
5. Interrupt and restore Wi-Fi, background/foreground the app, and restart Rack. Verify reconnection reads Rack's current values. A second controller must not replace the first. A second Bridge must report the port conflict, then recover when the first is removed.
6. If Bonjour was already installed, test nearby discovery, then repeat manual connection in an environment without Bonjour. Record missing-DLL behavior, service failure, Wi-Fi/Ethernet switching, and any VPN/multiple-adapter ambiguity. A missing Bonjour runtime must not prevent the plugin from loading.

Record Windows/Rack versions, device/iOS versions, package SHA-256, whether Bonjour was present, each check's result, and `log.txt` on failure. Neither native harness tests nor cross-compilation replace this checklist.

## Checks completed on the Mac

The Mac suite passed after the correction: socket lifecycle/budget checks, Rack engine and JSON allocator checks, ten TCP tests, eleven Swift tests, and a Simulator build. A negative control reproducing the original wrong-deallocator code fails the new ownership test as expected. The first Windows plugin compiled as PE32+ x86-64 and had an init export, but those checks missed CRT compatibility and the user subsequently reported a crash. That compilation is historical evidence, not a successful Windows validation. The new checks reject the original DLL and the system UCRT compiler; direct source builds also reject UCRT. Test2 passes static import/export checks using MSVCRT; all 95 Rack imports exist in the official runtime.

## Checks completed on Windows

The exact test2 ZIP SHA-256 is `c019e11b001c3858f18227e74474a5e4138a508bab97651c2778908a4d3c3a64` (885,144 bytes); its inner package SHA-256 is `c3c025edb5116d358991a9de9ecaf217d526e017cff28172b0a8825e53757972`, and its DLL SHA-256 is `8efba6268e07b5ad794c7692d6b33c492e31edd2b4713c13ac0b4d1501011b09`.

- Automated socket checks passed with exit 0 on dedicated port 18057, covering startup, conflicts, single-client behavior, framing, feedback, queue limits, reconnect, and cleanup.
- The Rack engine/JSON allocator self-test passed with exit 0. Automated clients did not connect to the user's live Rack.
- The user confirmed empty-patch and Bridge-only GUI loading. Logs corroborated the corrected init marker and module/widget creation, with no logged error for these checks.
- The user confirmed physical iPhone connection by manual IPv4 and two-way controls, Save As/reopen with restored mappings/sync, and loading/sync in the normal profile. These are user-operated confirmations, with logs and installation hashes providing corroborating evidence; they are not automated UI observations.

Windows Bonjour, physical iPad/all-slot coverage, broader OS/device coverage, and a separately confirmed physical Wi-Fi interruption/reconnect remain outstanding. The full ten-test Python TCP suite ran on macOS, not Windows. These basic results do not establish full release readiness.

## Crash investigation

The official Windows Rack 2.6.6 installer was downloaded and inspected on the Mac without executing or installing it there. Its `libRack.dll` and C++ runtime import `msvcrt.dll`; the withdrawn plugin imports `api-ms-win-crt-*` instead. Bridge exchanges `std::string` objects with Rack and previously freed buffers returned by Rack's `json_dumps` through its own `free()`. Test2 releases those buffers through Jansson's configured deallocator. Mixing CRT allocation/free across a DLL boundary is unsafe ([Microsoft explanation](https://learn.microsoft.com/en-us/cpp/c-runtime-library/potential-errors-passing-crt-objects-across-dll-boundaries)). These are confirmed compatibility/ownership defects and plausible crash causes, not confirmation of the user's exact fault. The original Windows log confirmed plugin loading but contained no fault stack. No corrected-build crash was observed in the recorded basic Windows checks.

The first kit SHA-256 is `37ad75d1f49350a1d7350dbfc886b1ac3f991b8fc3bdf1af01fbab921801d1fe`; its inner package SHA-256 is `da778e6aec4a47a78c9d4f462b4d6a0c9ae87f40c5b9839a48bb65edf29308d1`. Retain these only to identify the withdrawn build.

The protocol still lacks authentication and encryption. Use a trusted LAN and do not forward port 8057 to the internet. Existing manual Learn, physical iPad, and broader security/device release checks remain applicable.

Linux remains separate work and a VCV Library eligibility requirement. [Andrew Belt's requirement](https://github.com/VCVRack/library/issues/900#issuecomment-4339750362) includes both Linux and Windows. The [existing KnobOn submission](https://github.com/VCVRack/library/issues/988) is being updated with the Windows source and evidence; this update does not claim Linux support or Library acceptance.
