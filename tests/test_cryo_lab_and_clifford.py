#!/usr/bin/env python3

from __future__ import annotations

import os
import subprocess
import sys
import time
import unittest

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
for p in [os.path.join(REPO_ROOT, "runtime", "python"), os.path.join(REPO_ROOT, "src")]:
    if p not in sys.path:
        sys.path.insert(0, p)

from omniq.circuit import Circuit
from omniq.lab import LabRackManager, QickProgramCompiler
from omniq.stabilizer import CliffordSimulator
from omniq_core_bridge import OmniqCoreBridge

class TestCryoLabAndCliffordV2(unittest.TestCase):
    def test_01_qick_tproc_v2_assembly_and_drag_waveforms(self):
        circ = Circuit(3).ry(0, 1.0472).h(1).cx(1, 2).cx(0, 1).h(0).measure(0).measure(1).cx(1, 2).cz(0, 2)
        prog = QickProgramCompiler().compile(circ)
        self.assertAlmostEqual(prog["f_fabric_mhz"], 430.08, places=2)
        self.assertLess(prog["syndrome_feedback_ns"], 500.0)
        self.assertGreater(prog["instruction_count"], 15)
        self.assertIn("condj", prog["assembly"])
        self.assertIn("pulse", prog["assembly"])
        self.assertTrue(len(prog["waveforms"]) >= 2)
        wf0 = prog["waveforms"][0]
        self.assertEqual(len(wf0["I_dac16"]), 64)
        self.assertEqual(len(wf0["Q_dac16"]), 64)

    def test_02_live_wire_protocols_scpi_bluefors_modbus_qick_and_quench(self):
        twin_bin = os.path.join(REPO_ROOT, "bin", "omniq-lab-twin")
        offset = 3400
        proc = subprocess.Popen(
            [sys.executable, twin_bin, "--port-offset", str(offset)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            mgr = LabRackManager(port_offset=offset, timeout=3.0)
            ready = False
            for _ in range(30):
                try:
                    if "LSCI" in mgr.lakeshore.identify():
                        ready = True
                        break
                except Exception:
                    time.sleep(0.08)
            self.assertTrue(ready, "omniq-lab-twin SCPI listener failed to come online")

            idn_ls = mgr.send_raw_scpi("lakeshore", "*IDN?")
            idn_lo = mgr.send_raw_scpi("lo", "*IDN?")
            self.assertIn("MODEL372", idn_ls)
            self.assertIn("SGS100A", idn_lo)

            snap = mgr.poll_all(enforce_interlock=True)
            self.assertLess(snap["stages"]["mxc_mK"], 25.0)
            self.assertGreater(snap["stages"]["mxc_ruo2_ohms"], 20000.0)
            self.assertFalse(snap["interlock"]["tripped"])

            mgr.plc.set_valve(4, False)
            st_closed = mgr.plc.get_manifold_state()
            self.assertFalse(st_closed["valves"][3])
            mgr.plc.set_valve(4, True)
            st_open = mgr.plc.get_manifold_state()
            self.assertTrue(st_open["valves"][3])

            bell = Circuit(2).h(0).cx(0, 1)
            qres = mgr.dispatch_qick_circuit(bell, shots=512)
            counts = qres["counts"]
            self.assertGreater(counts.get("00", 0) + counts.get("11", 0), 460)

            mgr.trip_safety_interlock("UNITTEST_QUENCH_CHECK")
            self.assertFalse(mgr.microwave_lo.get_rf_output())
            self.assertFalse(mgr.plc.get_manifold_state()["rf_hemt_bias"])
            qres_clamped = mgr.dispatch_qick_circuit(bell, shots=128)
            self.assertFalse(qres_clamped.get("ok", True))
            self.assertIn("CLAMPED", str(qres_clamped.get("error", "")))

            mgr.reset_safety_interlock()
            self.assertTrue(mgr.microwave_lo.get_rf_output())
            qres2 = mgr.dispatch_qick_circuit(bell, shots=128)
            self.assertEqual(sum(qres2["counts"].values()), 128)
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except Exception:
                proc.kill()

    def test_03_cpp20_clifford_127q_512q_and_surface_code_d5(self):
        sim127 = CliffordSimulator(127)
        sim127.h(0)
        for q in range(126):
            sim127.cx(q, q + 1)
        sim127.measure_all()
        r127 = sim127.run(shots=128)
        self.assertEqual(r127["num_qubits"], 127)
        self.assertEqual(r127["backend"], "cpp20_chp_tableau")
        for bitstr in r127["counts"]:
            self.assertIn(bitstr, ("0" * 127, "1" * 127))

        qasm512 = "OPENQASM 3.0; qubit[512] q; h q[0]; " + " ".join(
            f"cx q[{i}], q[{i+1}];" for i in range(511)
        )
        r512 = CliffordSimulator.simulate_qasm(qasm512, shots=64)
        self.assertEqual(r512["num_qubits"], 512)

        sc_qasm = CliffordSimulator.surface_code_memory_qasm(distance=5, rounds=3)
        rsc = CliffordSimulator.simulate_qasm(sc_qasm, shots=64)
        self.assertEqual(rsc["num_qubits"], 49)
        self.assertGreater(rsc["deterministic_measurements"], 40)

    def test_04_avx2_openmp_statevector_and_real_rt_jitter_histogram(self):
        bridge = OmniqCoreBridge()
        qasm16 = "OPENQASM 3.0; qubit[16] q; h q[0]; " + " ".join(
            f"cx q[{i}], q[{i+1}];" for i in range(15)
        )
        sv16 = bridge.simulate_qasm(qasm16, shots=256)
        self.assertIsNotNone(sv16)
        assert sv16 is not None
        self.assertEqual(sv16["num_qubits"], 16)
        self.assertEqual(sv16["hilbert_dim"], 65536)

        rt = bridge.measure_rt_jitter(samples=50, interval_us=100)
        self.assertIsNotNone(rt)
        assert rt is not None
        self.assertGreaterEqual(rt["max_ns"], rt["p99_ns"])
        self.assertGreaterEqual(rt["p99_ns"], rt["p50_ns"])
        self.assertGreaterEqual(rt["p50_ns"], rt["min_ns"])
        self.assertEqual(len(rt["histogram"]), 8)

    def test_05_real_nist_fips203_mlkem768_lattice_kem(self):
        bridge = OmniqCoreBridge()
        kem = bridge.mlkem768_handshake()
        self.assertIsNotNone(kem)
        assert kem is not None
        self.assertTrue(kem["shared_secret_match"])
        self.assertEqual(kem["pk_bytes"], 1184)
        self.assertEqual(kem["ct_bytes"], 1088)
        self.assertIn("ML-KEM-768", kem["algorithm"])

    def test_06_kernel_qpu_c_driver_executes_real_qasm_and_no_fake_uname(self):
        from omniqos.core import QPU

        self.assertFalse(os.path.exists(os.path.join(REPO_ROOT, "bin", "uname")))

        with QPU() as qpu:
            dma = qpu.test_dma_ring(4096)
            self.assertGreater(dma["measured_ring_ns"], 0)

            p2 = qpu.allocate_partition(num_qubits=2, guard_hops=1)
            try:
                job_x = p2.submit("OPENQASM 3.0; qubit[2] q; x q[1];", shots=400)
                self.assertGreater(job_x.counts.get("10", 0), 360)
                self.assertGreater(job_x.execution_time_ns, 0)
            finally:
                p2.free()

            p3 = qpu.allocate_partition(num_qubits=3, guard_hops=1)
            try:
                job_ccx = p3.submit(
                    "OPENQASM 3.0; qubit[3] q; h q[0]; h q[1]; ccx q[0], q[1], q[2];",
                    shots=800,
                )
                self.assertGreater(job_ccx.counts.get("111", 0), 130)
                self.assertLess(job_ccx.counts.get("011", 0), 40)
            finally:
                p3.free()

if __name__ == "__main__":
    unittest.main()
