# SPDX-License-Identifier: Apache-2.0
"""
OmniQ-QOS: Python interface for Quantum Operating System and QPU accelerator.
Unified with libomniq_qpu.so and OmniQ statevector simulation engine.
"""

from typing import Any, Dict, List, Optional
import time

from omniqos.core import QPU
from .circuit import Circuit

# Constants matching UAPI linux/qpu.h
QPU_TECH_SUPERCONDUCTING_TRANSMON = 0x01
QPU_TECH_TRAPPED_ION             = 0x02
QPU_TECH_NEUTRAL_ATOM            = 0x03
QPU_TECH_PHOTONIC                = 0x04
QPU_TECH_SILICON_SPIN            = 0x05

QPU_ALLOC_FLAG_ISOLATED          = 0x00000001
QPU_ALLOC_FLAG_HIGH_FIDELITY     = 0x00000002
QPU_ALLOC_FLAG_TOPOLOGICAL_NEAREST = 0x00000004
QPU_ALLOC_FLAG_DYNAMIC_EXPAND    = 0x00000008

QPU_JOB_FLAG_DYNAMIC_DECOUPLING  = 0x00000001
QPU_JOB_FLAG_ACTIVE_RESET        = 0x00000002
QPU_JOB_FLAG_FAST_FEEDFORWARD    = 0x00000004

QPU_RESET_MODE_THERMAL           = 0x00000000
QPU_RESET_MODE_ACTIVE            = 0x00000001


class QPUClient:
    """Unified QPU accelerator client backed by libomniq_qpu.so and Circuit statevector execution."""

    def __init__(self, device_path: Optional[str] = None):
        self.device_path = device_path
        self._qpu = QPU(device_path)
        self._active_partitions: Dict[int, Any] = {}
        self._jobs: Dict[int, Dict[str, int]] = {}
        self._next_job_id = 1000

    def close(self):
        for pid in list(self._active_partitions.keys()):
            try:
                self.free(pid)
            except Exception:
                pass
        if self._qpu:
            self._qpu.close()
            self._qpu = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    @property
    def is_ipc(self) -> bool:
        return False

    def get_topology(self) -> Dict[str, Any]:
        top = self._qpu.get_topology()
        edges = [{"q1": u, "q2": v, "fidelity": 0.9955} for (u, v) in top.couplers]
        return {
            "device_name": top.device_name,
            "technology": int(top.technology),
            "num_physical_qubits": top.total_qubits,
            "num_edges": top.total_couplers,
            "guard_band_distance": 1,
            "edges": edges,
        }

    def get_calibration(self) -> List[Dict[str, Any]]:
        top = self._qpu.get_topology()
        cals = self._qpu.get_calibration(0, top.total_qubits)
        return [
            {
                "physical_id": c.qubit_id,
                "t1_us": c.t1_us,
                "t2_us": c.t2_us,
                "readout_fidelity": c.readout_fidelity,
                "single_qubit_fidelity": c.gate_1q_fidelity,
                "frequency_ghz": c.frequency_ghz,
                "is_alive": True,
            }
            for c in cals
        ]

    def get_cryo(self) -> Dict[str, Any]:
        top = self._qpu.get_topology()
        ilock = self._qpu.get_interlock()
        return {
            "timestamp_ns": time.time_ns(),
            "stage_50k_k": 48.50,
            "stage_4k_k": 3.85,
            "stage_still_mk": 820.0,
            "stage_cold_plate_mk": 95.0,
            "stage_mxc_mk": top.mixing_chamber_temp_mK,
            "vacuum_pressure_ubar": 1.2e-6,
            "ready": (not ilock["tripped"]) and top.mixing_chamber_temp_mK < 50.0,
            "interlock_tripped": ilock["tripped"],
        }

    def get_partitions(self) -> List[Dict[str, Any]]:
        res = []
        for pid, part in self._active_partitions.items():
            res.append({
                "partition_id": pid,
                "process_id": 0,
                "num_virtual_qubits": len(part.qubits),
                "physical_qubits": list(part.qubits),
                "guard_qubits": list(part.guard_qubits),
                "avg_fidelity": part.est_fidelity,
            })
        return res

    def allocate(self, num_qubits: int, flags: int = QPU_ALLOC_FLAG_ISOLATED) -> int:
        part = self._qpu.allocate_partition(num_qubits=max(1, int(num_qubits)), guard_hops=1, flags=flags & 0xFF)
        self._active_partitions[part.partition_id] = part
        return part.partition_id

    def free(self, partition_id: int) -> bool:
        part = self._active_partitions.pop(partition_id, None)
        if part is not None:
            part.free()
            return True
        self._qpu.free_partition(partition_id)
        return True

    def submit(self, partition_id: int, qasm_or_code: str, shots: int = 1024, flags: int = 0) -> int:
        ilock = self._qpu.get_interlock()
        if ilock.get("tripped"):
            raise RuntimeError("Cryogenic Thermal Safety Interlock TRIPPED: RF pulse synthesis clamped to 0x00")

        circ = Circuit.from_qasm(qasm_or_code)
        counts = circ.sample(shots=shots)
        jid = self._next_job_id
        self._next_job_id += 1
        self._jobs[jid] = counts
        return jid

    def wait(self, job_id: int, timeout_ms: int = 2000, num_qubits: int = 2) -> Dict[str, int]:
        if job_id not in self._jobs:
            raise RuntimeError(f"Unknown job_id {job_id}")
        return self._jobs.pop(job_id)

    def run_sync(self, qasm_code: str, num_qubits: int = 2, shots: int = 1024) -> Dict[str, int]:
        pid = self.allocate(num_qubits)
        try:
            jid = self.submit(pid, qasm_code, shots=shots)
            return self.wait(jid, timeout_ms=2000, num_qubits=num_qubits)
        finally:
            self.free(pid)

    def reset_qubit(self, physical_id: int, active: bool = True) -> bool:
        self._qpu.reset_qubit(int(physical_id))
        return True
