import json
import os
import subprocess
import sys
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
for p in [os.path.join(REPO_ROOT, "runtime", "python"), os.path.join(REPO_ROOT, "src")]:
    if p not in sys.path:
        sys.path.insert(0, p)

from omniq_core_bridge import core_bridge

class TestOmniQCore(unittest.TestCase):
    def test_01_compiled_binaries_and_libs_exist(self):
        self.assertTrue(os.path.exists(os.path.join(REPO_ROOT, "bin", "omniq-core")))
        self.assertTrue(os.path.exists(os.path.join(REPO_ROOT, "lib", "libomniq_core.so")))
        with open(os.path.join(REPO_ROOT, "bin", "omniq-core"), "rb") as f:
            self.assertEqual(f.read(4), b"\x7fELF")

    def test_02_core_bridge_status_and_shm(self):
        st = core_bridge.get_status()
        self.assertEqual(st.get("engine"), "OmniQ-Core C++20 RT")
        self.assertGreaterEqual(st.get("worker_threads", 0), 4)
        self.assertTrue(st.get("shm_active"))

    def test_03_benchmarks_and_8q_ghz_entropy(self):
        bench = core_bridge.run_benchmarks()
        self.assertAlmostEqual(bench["ghz_8q_entanglement_entropy_bits"], 1.0, places=2)
        self.assertLess(bench["gc_incremental_pause_us"], 500.0)

    def test_04_cpp20_qasm_simulator_bell_and_bloch(self):
        qasm = "OPENQASM 3.0;\nqubit[2] q;\nh q[0];\ncx q[0], q[1];\n"
        res = core_bridge.simulate_qasm(qasm, shots=1000, depol_noise=0.0)
        self.assertIsNotNone(res)
        self.assertEqual(res["num_qubits"], 2)
        self.assertAlmostEqual(res["entanglement_entropy_bits"], 1.0, places=3)
        self.assertAlmostEqual(res["bloch_vectors"][0]["purity"], 0.5, places=3)
        counts = res["counts"]
        self.assertEqual(counts.get("00", 0) + counts.get("11", 0), 1000)

    def test_05_omniq_core_cli_flags(self):
        core_bin = os.path.join(REPO_ROOT, "bin", "omniq-core")
        out = subprocess.check_output([core_bin, "--json"], text=True)
        parsed = json.loads(out)
        self.assertIn("worker_threads", parsed)

if __name__ == "__main__":
    unittest.main()
