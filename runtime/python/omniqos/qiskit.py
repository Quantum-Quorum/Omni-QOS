# SPDX-License-Identifier: Apache-2.0
"""
OmniQ-OS: Qiskit Backend Adapter
Enables native execution of Qiskit QuantumCircuit on /dev/accel/qpu0
"""

from typing import Union, List, Optional
from .core import QPU, QPUJob

class OmniQBackend:
    """
    Qiskit Provider Backend targeting the OmniQ-OS Real-Time Kernel Accelerator.
    """
    def __init__(self, qpu_instance: Optional[QPU] = None):
        self._qpu = qpu_instance or QPU()
        self._top = self._qpu.get_topology()
        self.name = f"omniq_{self._top.device_name.lower().replace('-', '_')}"
        self.description = f"OmniQ-OS Accelerator: {self._top.device_name} ({self._top.total_qubits} Qubits)"

    @property
    def num_qubits(self) -> int:
        return self._top.total_qubits

    def run(self, run_input, shots: int = 1000, **kwargs) -> 'OmniQJob':
        """
        Executes a Qiskit QuantumCircuit or list of circuits.
        """
        # Convert Qiskit circuit to OpenQASM 3.0
        qasm_str = ""
        num_q = 2

        if hasattr(run_input, "qasm"):
            try:
                # Qiskit OpenQASM 2 export fallback
                qasm_str = run_input.qasm()
            except Exception:
                pass

        if not qasm_str:
            try:
                # Qiskit qasm3 exporter
                import qiskit.qasm3
                qasm_str = qiskit.qasm3.dumps(run_input)
            except Exception:
                # Direct string or fallback
                qasm_str = str(run_input)

        if hasattr(run_input, "num_qubits"):
            num_q = run_input.num_qubits

        # Allocate spatial partition on QPU
        partition = self._qpu.allocate_partition(num_qubits=num_q, guard_hops=1)
        try:
            job = partition.submit(qasm_str, shots=shots)
        finally:
            partition.free()

        return OmniQJob(self, job)

class OmniQJob:
    def __init__(self, backend: OmniQBackend, qpu_job: QPUJob):
        self._backend = backend
        self._job = qpu_job
        self._job_id = str(qpu_job.job_id)

    def job_id(self) -> str:
        return self._job_id

    def result(self) -> 'OmniQResult':
        return OmniQResult(self._job)

    def status(self):
        return "DONE"

class OmniQResult:
    def __init__(self, qpu_job: QPUJob):
        self._job = qpu_job

    def get_counts(self) -> dict:
        return self._job.counts

    @property
    def execution_time_ns(self) -> int:
        return self._job.execution_time_ns
