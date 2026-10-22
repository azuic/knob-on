#!/usr/bin/env python3
"""Production plugin in real Rack under Xvfb; VCO/VCF and fixture, no audio."""
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import shutil
import socket
import subprocess
import tarfile
import tempfile
import time

if platform.system() != "Linux" or platform.machine() != "x86_64":
    raise SystemExit("Run inside isolated Linux x64; this is not a Mac cross-build test.")

# Refuse an occupied production port before creating a profile or connecting.
# Container invocation must use --network none and publish no ports.
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as preflight:
    preflight.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        preflight.bind(("0.0.0.0", 8057))
    except OSError as error:
        raise SystemExit(f"Port 8057 is occupied; refusing to contact an existing Rack: {error}")

ROOT = Path(__file__).resolve().parents[1]
APP = Path(os.environ.get("RACK_LINUX_APP_DIR", "/opt/knobon/Rack2Free")).resolve()
SDK = Path(os.environ["RACK_LINUX_SDK"]).resolve()
manifest = json.loads((ROOT / "plugin.json").read_text())
package = ROOT / "build/linux" / f"{manifest['slug']}-{manifest['version']}-lin-x64.vcvplugin"
output = Path(tempfile.mkdtemp(prefix="app-smoke-", dir=ROOT / "build/linux"))
profile = output / "user"
plugins = profile / "plugins-lin-x64"
plugins.mkdir(parents=True)
shutil.copy2(package, plugins)
probe = plugins / "KnobOnLinuxProbe"
(probe / "res").mkdir(parents=True)
shutil.copy2(ROOT / "tests/linux/probe-plugin.json", probe / "plugin.json")
shutil.copy2(ROOT / "res/Bridge.svg", probe / "res/Bridge.svg")
subprocess.run([os.environ.get("CXX", "g++"), "-std=c++11", "-march=nehalem", "-fPIC", "-shared",
                "-fno-gnu-unique", "-static-libstdc++", "-static-libgcc",
                f"-I{SDK}/include", f"-I{SDK}/dep/include", str(ROOT / "tests/linux/Probe.cpp"),
                f"-L{SDK}", "-lRack", "-Wl,-rpath=/tmp/Rack2", "-o", str(probe / "plugin.so")], check=True)

def archive_patch(path, patch):
    raw = io.BytesIO()
    data = json.dumps(patch).encode()
    with tarfile.open(fileobj=raw, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        entry = tarfile.TarInfo("patch.json")
        entry.size = len(data)
        archive.addfile(entry, io.BytesIO(data))
    path.write_bytes(subprocess.run(["zstd", "-q", "-c"], input=raw.getvalue(), check=True, stdout=subprocess.PIPE).stdout)

patch = output / "mapped-test.vcv"
targets = [{"moduleId": 1001, "paramId": i} for i in range(16)]
targets[0] = {"moduleId": 2001, "paramId": 2}  # Actual Fundamental VCO Frequency.
targets[8] = {"moduleId": 2002, "paramId": 0}  # Actual Fundamental VCF Cutoff.
archive_patch(patch, {"version": "2.6.6", "modules": [
    {"id": 1001, "plugin": "KnobOnLinuxProbe", "model": "Probe", "version": "2.0.0", "pos": [0, 0], "params": []},
    {"id": 2001, "plugin": "Fundamental", "model": "VCO", "version": "2.6.4", "pos": [20, 0], "params": []},
    {"id": 2002, "plugin": "Fundamental", "model": "VCF", "version": "2.6.4", "pos": [30, 0], "params": []},
    {"id": 1002, "plugin": "KnobOn", "model": "Bridge", "version": manifest["version"], "pos": [10, 0],
     "params": [], "data": {"controls": targets}}
], "cables": []})
env = dict(os.environ, DISPLAY=":99", LIBGL_ALWAYS_SOFTWARE="1")
xlog = (output / "xvfb.log").open("w")
xserver = subprocess.Popen(["Xvfb", ":99", "-screen", "0", "1280x960x24", "-nolisten", "tcp"], stdout=xlog, stderr=xlog)
process = None
command_id = 0
phases = []

def start(patch_path, label):
    # A newly constructed probe must never replay the previous process's close.
    for name in ("linux-probe-command.json", "linux-probe-result.json"):
        (profile / name).unlink(missing_ok=True)
    log = (output / f"{label}.log").open("w")
    return subprocess.Popen([str(APP / "Rack"), "--system", str(APP), "--user", str(profile), str(patch_path)],
                            cwd=APP, env=env, stdout=log, stderr=log)

def probe_command(**fields):
    global command_id
    command_id += 1
    temporary = profile / "linux-probe-command.tmp"
    temporary.write_text(json.dumps({"id": command_id, **fields}))
    temporary.replace(profile / "linux-probe-command.json")
    result = profile / "linux-probe-result.json"
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if result.exists():
            report = json.loads(result.read_text())
            if report["id"] == command_id:
                return report
        if process.poll() is not None:
            raise AssertionError(f"Rack exited {process.returncode}; see {output}")
        time.sleep(0.02)
    raise AssertionError(f"Rack probe timed out; see {output}")

def connect():
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise AssertionError(f"Rack exited {process.returncode}; see {output}")
        try:
            client = socket.create_connection(("127.0.0.1", 8057), timeout=1)
            client.settimeout(10)
            return client, client.makefile("rb")
        except ConnectionRefusedError:
            time.sleep(0.1)
    raise AssertionError(f"Rack did not listen; see {output}")

def send(client, **message):
    client.sendall((json.dumps({"version": 1, **message}) + "\n").encode())

def bank(reader, predicate=lambda controls: True):
    for _ in range(200):
        message = json.loads(reader.readline())
        if message.get("type") == "bank" and predicate(message["controls"]):
            return message["controls"]
    raise AssertionError("Expected bank state did not arrive")

def close_rack():
    probe_command(close=True)
    assert process.wait(timeout=20) == 0, "Rack did not shut down cleanly"

try:
    deadline = time.monotonic() + 10
    while not Path("/tmp/.X11-unix/X99").exists():
        assert time.monotonic() < deadline and xserver.poll() is None, "Xvfb failed"
        time.sleep(0.05)
    process = start(patch, "first-run")
    client, reader = connect()
    with client, reader:
        send(client, type="hello", controls=16)
        controls = bank(reader, lambda c: len(c) == 16 and all(s["mapped"] for s in c))
        assert controls[1]["name"] == "Stepped" and controls[2]["name"] == "Test 2", "Unexpected Rack fixture"
        phases.append({"phase": "production-plugin-loaded", "controls": len(controls)})
        for index in range(16):
            value = 0.9 if index == 1 else (index + 1) / 20
            send(client, type="set", control=index, mapping=controls[index]["mapping"], value=value, seq=1)
        controls = bank(reader, lambda c: all(s["ack"] == 1 for s in c))
        observed = probe_command(targets=targets)["values"]
        for index, value in enumerate(observed):
            assert abs(value - (1 if index == 1 else (index + 1) / 20)) < 1e-6
        phases.append({"phase": "sixteen-writes-independent-engine-read", "values": observed})
        probe_command(targets=[{**targets[0], "value": 0.3212345}, {**targets[8], "value": 0.7654321}])
        controls = bank(reader, lambda c: abs(c[0]["value"] - 0.3212345) < 1e-6 and abs(c[8]["value"] - 0.7654321) < 1e-6)
        assert all(s["ack"] == 1 for s in controls)
        phases.append({"phase": "independent-engine-feedback-no-extra-write-ack", "controls": controls})
        probe_command(save=True)
    time.sleep(0.1)
    client, reader = connect()
    with client, reader:
        send(client, type="hello", controls=16)
        controls = bank(reader, lambda c: abs(c[0]["value"] - 0.3212345) < 1e-6)
        phases.append({"phase": "socket-reconnect-current-values", "controls": len(controls)})
    close_rack()
    saved = profile / "probe-saved.vcv"
    assert saved.exists()
    process = start(saved, "saved-patch-reload")
    client, reader = connect()
    with client, reader:
        send(client, type="hello", controls=16)
        controls = bank(reader, lambda c: len(c) == 16 and all(s["mapped"] for s in c))
        assert abs(controls[0]["value"] - 0.3212345) < 1e-6
        assert abs(controls[8]["value"] - 0.7654321) < 1e-6
        phases.append({"phase": "application-restart-saved-mappings-values", "controls": controls})
    close_rack()
    report = {"status": "passed", "packageSha256": hashlib.sha256(package.read_bytes()).hexdigest(),
              "scope": "Real Rack under Xvfb/software OpenGL; actual Fundamental VCO/VCF and fourteen test-only parameters",
              "phases": phases, "limitations": ["Emulated Linux x64", "No native desktop or physical phone acceptance", "Saved mappings; no mouse Learn selection", "No audio device or mDNS test"]}
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"status": "passed", "report": str(output / "report.json")}))
finally:
    if not (output / "report.json").exists():
        (output / "report.json").write_text(json.dumps({"status": "failed", "phases": phases}, indent=2) + "\n")
    if process and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
    xserver.terminate()
    xserver.wait(timeout=10)
    xlog.close()
