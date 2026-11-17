#!/usr/bin/env python3

import sys
import os

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "runtime", "python")))

from omniqos.core import QPU, QPUOperatingState, QPUTechnology
from omniqos.qiskit import OmniQBackend
from omniqos.cirq import OmniQSampler
from omniqos.pennylane import OmniQPennyLaneDevice
from omniqos.braket import OmniQBraketDevice
from omniqos.debugger_bridge import OmniQDebuggerBridge

def test_core_qpu():
    print("[TEST 1] Core QPU Python Interface...")
    with QPU() as qpu:
        top = qpu.get_topology()
        print(f"       • Device: {top.device_name}")
        print(f"       • State: {top.operating_state.name}")
        print(f"       • Mixing Chamber Temp: {top.mixing_chamber_temp_mK:.3f} mK")
        print(f"       • Total Qubits: {top.total_qubits}, Couplers: {top.total_couplers}")
        assert top.total_qubits == 127
        assert top.operating_state == QPUOperatingState.SIMULATOR

        cal = qpu.get_calibration(0, 4)
        print(f"       • Qubit 0 Freq: {cal[0].frequency_ghz:.4f} GHz, T1: {cal[0].t1_us:.1f} us")
        assert len(cal) == 4
        assert cal[0].t1_us > 100

        part = qpu.allocate_partition(num_qubits=2, guard_hops=1)
        print(f"       • Partition #{part.partition_id} allocated: Qubits={part.qubits}, Guards={part.guard_qubits}")
        assert len(part.qubits) == 2
        assert len(part.guard_qubits) > 0

        qasm = "OPENQASM 3.0;\nqubit[2] q;\nh q[0];\ncx q[0], q[1];\n"
        job = part.submit(qasm, shots=1000)
        print(f"       • Bell State Counts: {job.counts} (Execution: {job.execution_time_ns} ns)")
        assert "00" in job.counts
        assert "11" in job.counts
        assert job.counts["00"] + job.counts["11"] > 950

        part.free()
    print("       [PASS] Core QPU test succeeded.\n")

def test_qiskit_adapter():
    print("[TEST 2] Qiskit Backend Adapter...")
    backend = OmniQBackend()
    print(f"       • Backend Name: {backend.name} ({backend.num_qubits} Qubits)")
    job = backend.run("OPENQASM 3.0;\nqubit[2] q;\nh q[0];\ncx q[0], q[1];\n", shots=1000)
    result = job.result()
    counts = result.get_counts()
    print(f"       • Qiskit Result Counts: {counts}")
    assert "00" in counts and "11" in counts
    print("       [PASS] Qiskit adapter succeeded.\n")

def test_cirq_adapter():
    print("[TEST 3] Cirq Sampler Adapter...")
    sampler = OmniQSampler()
    res = sampler.run(None, repetitions=1000)
    counts = res.histogram()
    print(f"       • Cirq Histogram: {counts}")
    assert "00" in counts and "11" in counts
    print("       [PASS] Cirq adapter succeeded.\n")

def test_pennylane_adapter():
    print("[TEST 4] PennyLane Device Adapter...")
    dev = OmniQPennyLaneDevice(wires=2, shots=1000)
    counts = dev.execute(None)
    dev.close()
    print(f"       • PennyLane Counts: {counts}")
    assert "00" in counts and "11" in counts
    print("       [PASS] PennyLane adapter succeeded.\n")

def test_braket_adapter():
    print("[TEST 5] Amazon Braket Device Adapter...")
    bdev = OmniQBraketDevice()
    task = bdev.run("OPENQASM 3.0;\nqubit[2] q;\nh q[0];\ncx q[0], q[1];\n", shots=1000)
    result = task.result()
    print(f"       • Braket Task Counts: {result.measurement_counts}")
    assert "00" in result.measurement_counts and "11" in result.measurement_counts
    print("       [PASS] Amazon Braket adapter succeeded.\n")

def test_debugger_bridge():
    print("[TEST 6] OmniQ GUI Debugger Session Export...")
    qasm = "OPENQASM 3.0;\nqubit[2] q;\nh q[0];\ncx q[0], q[1];\n"
    exported = OmniQDebuggerBridge.launch(qasm, {"00": 512, "11": 488}, non_blocking=True)
    print(f"       • Debugger Session Exported (Native binary detected: {exported})")
    print("       [PASS] Debugger bridge succeeded.\n")

if __name__ == "__main__":
    print("=================================================================")
    print(" OmniQ-OS Python SDK & Multi-Framework Adapter Test Suite       ")
    print("=================================================================\n")
    test_core_qpu()
    test_qiskit_adapter()
    test_cirq_adapter()
    test_pennylane_adapter()
    test_braket_adapter()
    test_debugger_bridge()
    print("=================================================================")
    print(" ALL PYTHON SDK & FRAMEWORK ADAPTER TESTS PASSED!               ")
    print("=================================================================")

