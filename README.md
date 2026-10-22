# Knob On Bridge

The VCV Rack companion for Knob On on iPhone and iPad. Map touch controls to Rack parameters, with high-resolution values and two-way feedback. The iOS app is being prepared for its first App Store release.

- [Setup and support](https://azuic.github.io/knob-on/support.html)
- [Privacy policy](https://azuic.github.io/knob-on/privacy.html)
- Support: [hi@zui.ooo](mailto:hi@zui.ooo)

## Download

Get the Apple silicon Mac package and a ready-mapped test patch from [Releases](https://github.com/azuic/knob-on/releases/tag/v2.3.1). Requires Rack 2.6.6 or later. Intel Mac users can build from source; no Intel binary is supplied in this release.

Quit Rack, copy `KnobOn-2.3.1-mac-arm64.vcvplugin` into `~/Library/Application Support/Rack2/plugins-mac-arm64/`, then reopen Rack. Add **Knob On → Bridge**. In the app, tap **disconnected** and choose the name printed on the Bridge under **nearby Rack**. Both devices must use the same local network.

Hold an app dial or slider, then click a bounded parameter in Rack. The app shows its name and value. Drag vertically to adjust; changes in Rack update the app. Tap a mapped control to change its color; hold again to disconnect it. Save the patch to retain mappings.

## Build on macOS

Install Apple's command-line tools, `jq`, and `zstd`. Download the official [Rack SDK 2.6.6](https://vcvrack.com/downloads/) for your architecture and extract it into `.deps/Rack-SDK` (or pass `RACK_DIR=/absolute/path/to/Rack-SDK`).

```
make -j4
make dist
```

The package appears in `dist/`. This repository contains the companion source and public support site; the iOS project is maintained separately.

## Windows

Windows x64 basic testing has passed with Rack Free 2.6.6: automated socket and Rack allocator checks, plugin loading, physical iPhone connection by IPv4, two-way control, and saved mappings/sync after reopening a patch. User-operated checks are recorded separately from automated checks in the [validation record](docs/windows-validation.json).

Build with an MSVCRT MinGW-w64 toolchain; UCRT builds are rejected. See [Windows setup, build, and remaining checks](docs/windows.md). Windows Bonjour discovery and broader device coverage remain outstanding. The first Windows test kit was withdrawn after a reported crash; use the corrected source. The published release download remains the Mac preview. Linux remains separate work required for VCV Library eligibility.

## Connection and limitations

Bridge 2.3.1 supports sixteen independent mappings, one Bridge instance, and one controller per Rack process, on TCP port 8057. macOS uses system Bonjour discovery; Windows provides manual IPv4 connection and optional discovery through an existing system Bonjour runtime. It does not restrict mapping to particular module names. Most bounded parameters are supported; unbounded parameters are rejected. Internal CV modulation may change sound without changing the displayed parameter value.

The current protocol has no pairing authentication or encryption. Use a trusted local network and do not forward port 8057 to the internet. Pairing is an outstanding hardening task before a wider public launch. The release is a preview for the upcoming app, not a VCV Library listing.

Physical iPhone two-way dial/slider feedback and Wi-Fi reconnection were tested. Manual Learn target selection, physical iPad acceptance, and wider device testing remain release checks.

## License

The Rack companion is licensed under GPL-3.0-or-later; see [LICENSE](LICENSE). This license covers the companion source and does not license the Knob On name or branding for unrelated products. VCV Rack is a separate product.
