# SPDX-License-Identifier: Apache-2.0
"""Unit tests for C++20 7-Modality Quantum Secret API (:9443), HMAC-SHA256, Audit Chain & PQC Audit."""

import ctypes
import hashlib
import hmac
import json
import os
import socket
import subprocess
import sys
import time
import unittest
import urllib.error
import urllib.request

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
for p in [os.path.join(REPO_ROOT, "runtime", "python"), os.path.join(REPO_ROOT, "src")]:
    if p not in sys.path:
        sys.path.insert(0, p)

from omniq_core_bridge import core_bridge


class TestPQCAndSecretAPI(unittest.TestCase):
    def test_01_secret_c_abi_auth_guard_and_modalities(self):
        key = core_bridge.ensure_secret_key()
        self.assertTrue(key.startswith("omniq_sk_live_"))

        bad = core_bridge.get_secret_telemetry("invalid_key_123", "all")
        self.assertEqual(bad.get("error_code"), -401)

        good = core_bridge.get_secret_telemetry(key, "all")
        self.assertEqual(good.get("magic"), "OMNIQSEC")
        self.assertEqual(good.get("frame_size_bytes"), 3744)
        self.assertEqual(len(good.get("frame_sha256", "")), 64)
        for mod in ["cryo", "rfsoc", "readout", "qmmu", "qec", "coherence", "tricompute"]:
            self.assertIn(mod, good)
        self.assertIn("ML-KEM-768", good["tricompute"]["pqc_kem"])
        self.assertIn("ML-DSA-65", good["tricompute"]["pqc_sig"])

    def test_02_tamper_evident_sha256_audit_chain(self):
        entries = ctypes.c_int(0)
        rc = core_bridge.secret_lib.omniq_secret_verify_audit_chain(ctypes.byref(entries))
        self.assertEqual(rc, 0)
        self.assertGreaterEqual(entries.value, 2)

    def test_03_live_http_daemon_hmac_and_binary_3744_frame(self):
        # Find an open ephemeral port
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(("127.0.0.1", 0))
            port = s.getsockname()[1]

        srv_bin = os.path.join(REPO_ROOT, "bin", "omniq-secret-api")
        proc = subprocess.Popen([srv_bin, "--daemon", "--port", str(port)])
        try:
            time.sleep(0.35)
            # Public health endpoint
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/api/v1/secret/health", timeout=2) as r:
                health = json.loads(r.read().decode("utf-8"))
                self.assertEqual(health["frame_bytes"], 3744)

            # Unauthenticated request MUST return HTTP 401
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                urllib.request.urlopen(f"http://127.0.0.1:{port}/api/v1/secret/telemetry", timeout=2)
            self.assertEqual(ctx.exception.code, 401)

            # HMAC-SHA256 authenticated binary frame request
            key = core_bridge.ensure_secret_key()
            ts = str(int(time.time()))
            path = "/api/v1/secret/telemetry/binary"
            sig = hmac.new(key.encode("utf-8"), f"{ts}:{path}".encode("utf-8"), hashlib.sha256).hexdigest()
            req = urllib.request.Request(
                f"http://127.0.0.1:{port}{path}",
                headers={"X-OMNIQ-Signature": sig, "X-OMNIQ-Timestamp": ts},
            )
            with urllib.request.urlopen(req, timeout=2) as r:
                raw = r.read()
                self.assertEqual(len(raw), 3744)
                # Little-endian 0x4F4D4E4951534543ULL -> b"CESQINMO"
                self.assertEqual(raw[:8], b"CESQINMO")
        finally:
            proc.terminate()
            proc.wait(timeout=3)


if __name__ == "__main__":
    unittest.main()
