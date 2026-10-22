"""Exercise the compiled companion against Rack's actual Engine/ParamQuantity."""
import json
import os
import select
import socket
import subprocess
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class BridgeTests(unittest.TestCase):
    def setUp(self):
        harness = os.environ.get("KNOB_RACK_HARNESS", str(ROOT / "build/rack_harness"))
        self.process = subprocess.Popen([harness], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.socket = None
        self.buffer = b""
        self.addCleanup(self.cleanup)
        self.assertEqual(self.process.stdout.readline().strip(), "ready")
        self.connect()

    def cleanup(self):
        if self.socket:
            self.socket.close()
        self.process.communicate("quit\n", timeout=5)
        self.assertEqual(self.process.returncode, 0)

    def connect(self):
        for attempt in range(100):
            try:
                self.socket = socket.create_connection(("127.0.0.1", 18058), timeout=1)
                self.socket.settimeout(2)
                break
            except ConnectionRefusedError:
                time.sleep(0.01)
        else:
            self.fail("Companion did not listen")
        self.buffer = b""
        self.send(type="hello")
        self.initial = self.until(lambda m: m.get("type") == "state")

    def send(self, **message):
        self.socket.sendall((json.dumps({"version": 1, **message}) + "\n").encode())

    def read(self):
        while b"\n" not in self.buffer:
            chunk = self.socket.recv(65536)
            if not chunk:
                raise EOFError("Companion disconnected")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line)

    def until(self, predicate):
        for _ in range(100):
            message = self.read()
            if predicate(message):
                return message
        self.fail("Expected response never arrived")

    def rack(self, command):
        self.process.stdin.write(f"{command}\n")
        self.process.stdin.flush()

    def test_bidirectional_precision_and_no_feedback_loop(self):
        self.assertTrue(self.initial["mapped"])
        self.assertEqual(self.initial["name"], "Frequency")
        self.send(type="set", mapping=self.initial["mapping"], value=0.1234567, seq=1)
        reply = self.until(lambda m: m.get("ack") == 1)
        self.assertAlmostEqual(reply["value"], 0.1234567, places=6)
        self.rack(27)
        reply = self.until(lambda m: m.get("type") == "state" and abs(m["value"] - 0.75) < 1e-6)
        self.assertEqual(reply["ack"], 1)
        # An idle parameter sends no repeated state/echo stream.
        self.assertFalse(self.buffer)
        self.assertFalse(select.select([self.socket], [], [], 0.15)[0])

    def test_reconnect_refreshes_current_value_and_patch_reload(self):
        self.socket.close()
        self.rack(-27)
        time.sleep(0.05)
        self.connect()
        self.assertAlmostEqual(self.initial["value"], 0.25, places=6)
        token = self.initial["mapping"]
        self.rack("reload")
        reply = self.until(lambda m: m.get("type") == "state" and m["mapping"] != token)
        self.assertAlmostEqual(reply["value"], 0.25, places=6)

    def test_remapping_rejects_old_writes_and_snaps_stepped_values(self):
        self.rack("remap")
        state = self.until(lambda m: m.get("name") == "Octave")
        self.assertTrue(state["stepped"])
        self.send(type="set", mapping=self.initial["mapping"], value=0.1, seq=1)
        self.until(lambda m: m.get("type") == "error")
        self.send(type="set", mapping=state["mapping"], value=0.9, seq=2)
        self.assertEqual(self.until(lambda m: m.get("ack") == 2)["value"], 1)

    def test_fragmented_frames_heartbeat_and_invalid_messages(self):
        self.socket.sendall(b'{"type":"pi')
        self.socket.sendall(b'ng","version":1}\n')
        self.assertEqual(self.read()["type"], "pong")
        self.socket.sendall(b'[]\n')
        self.assertEqual(self.read()["type"], "error")
        self.send(type="set", mapping=self.initial["mapping"], value=2, seq=1)
        self.assertEqual(self.read()["type"], "error")
        self.send(type="sync")
        self.assertEqual(self.until(lambda m: m.get("type") == "state")["ack"], 0)

    def bank(self, predicate=lambda controls: True):
        return self.until(lambda m: m.get("type") == "bank" and predicate(m["controls"]))["controls"]

    def test_twelve_controls_independent_values_persistence_and_unmap(self):
        self.send(type="hello", controls=12)
        slots = self.bank()
        self.assertEqual([s["control"] for s in slots], list(range(12)))
        self.rack("bankmap")
        slots = self.bank(lambda c: all(s["mapped"] for s in c))
        for index, slot in enumerate(slots):
            self.send(type="set", control=index, mapping=slot["mapping"], value=0.75, seq=1)
        slots = self.bank(lambda c: all(s["ack"] == 1 for s in c))
        self.assertAlmostEqual(slots[11]["value"], 0.75)
        self.rack("value 8 0.2")
        slots = self.bank(lambda c: abs(c[8]["value"] - 0.2) < 1e-6)
        self.assertAlmostEqual(slots[0]["value"], 0.75)
        tokens = [s["mapping"] for s in slots]
        self.rack("reload")
        slots = self.bank(lambda c: c[0]["mapping"] != tokens[0])
        self.assertTrue(all(s["mapped"] for s in slots))
        self.assertAlmostEqual(slots[8]["value"], 0.2)
        token = slots[8]["mapping"]
        self.send(type="unmap", control=8, mapping=token)
        slots = self.bank(lambda c: not c[8]["mapped"])
        self.assertTrue(slots[9]["mapped"])
        self.send(type="set", control=8, mapping=token, value=0.9, seq=5)
        self.until(lambda m: m.get("type") == "error")
        self.rack("map 8 8")
        slots = self.bank(lambda c: c[8]["mapped"])
        self.assertAlmostEqual(slots[8]["value"], 0.2)

    def test_scoped_learning_cancel_and_invalid_indices(self):
        self.send(type="hello", controls=12)
        self.bank()
        self.send(type="learn", control=10)
        self.bank(lambda c: c[10]["learning"] and sum(s["learning"] for s in c) == 1)
        self.send(type="learn", control=3)
        self.bank(lambda c: c[3]["learning"] and not c[10]["learning"])
        self.send(type="cancelLearn")
        self.bank(lambda c: not any(s["learning"] for s in c))
        for index in [-1, 16, 0.5, "1"]:
            self.send(type="learn", control=index)
            self.until(lambda m: m.get("type") == "error")

    def test_sixteen_controls_keep_existing_sliders_and_add_four_dials(self):
        self.send(type="hello", controls=16)
        self.bank(lambda c: len(c) == 16)
        self.rack("bankmap")
        slots = self.bank(lambda c: len(c) == 16 and all(s["mapped"] for s in c))
        for index in [8, 11, 12, 15]:
            self.send(type="set", control=index, mapping=slots[index]["mapping"], value=index / 20, seq=1)
        slots = self.bank(lambda c: all(c[i]["ack"] == 1 for i in [8, 11, 12, 15]))
        self.assertAlmostEqual(slots[8]["value"], 0.4, places=6)
        self.assertAlmostEqual(slots[12]["value"], 0.6, places=6)
        self.assertAlmostEqual(slots[15]["value"], 0.75, places=6)
        token = slots[15]["mapping"]
        self.rack("reload")
        slots = self.bank(lambda c: c[15]["mapping"] != token)
        self.assertTrue(all(s["mapped"] for s in slots))
        self.assertAlmostEqual(slots[15]["value"], 0.75, places=6)
        self.send(type="learn", control=15)
        self.bank(lambda c: c[15]["learning"])
        self.send(type="cancelLearn")
        self.bank(lambda c: not c[15]["learning"])
        self.send(type="unmap", control=15, mapping=slots[15]["mapping"])
        self.bank(lambda c: not c[15]["mapped"] and c[8]["mapped"])

    def test_oversized_frame_disconnects_without_killing_server(self):
        self.socket.sendall(b"x" * 70000)
        try:
            self.assertEqual(self.socket.recv(1), b"")
        except ConnectionResetError:
            pass  # Closing an oversized frame may reset unread TCP data.
        self.socket.close()
        self.connect()
        self.assertTrue(self.initial["mapped"])

    def test_second_controller_does_not_replace_first(self):
        with socket.create_connection(("127.0.0.1", 18058), timeout=2) as other:
            other.settimeout(2)
            try:
                self.assertEqual(other.recv(1), b"")
            except ConnectionResetError:
                pass
        self.send(type="ping")
        self.assertEqual(self.until(lambda m: m.get("type") == "pong")["type"], "pong")

    def test_controller_close_keeps_listener_available(self):
        self.socket.close()
        time.sleep(0.05)
        self.connect()
        self.send(type="ping")
        self.assertEqual(self.until(lambda m: m.get("type") == "pong")["type"], "pong")


if __name__ == "__main__":
    unittest.main(verbosity=2)
