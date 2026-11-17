from typing import Union, List, Optional
from .core import QPU, QPUJob

class OmniQBackend:
    def __init__(self, qpu_instance: Optional[QPU] = None):
        self._qpu = qpu_instance or QPU()
        self._top = self._qpu.get_topology()
        self.name = f"omniq_{self._top.device_name.lower().replace('-', '_')}"
        self.description = f"OmniQ-OS Accelerator: {self._top.device_name} ({self._top.total_qubits} Qubits)"

    @property
    def num_qubits(self) -> int:
        return self._top.total_qubits

    def run(self, run_input, shots: int = 1000, **kwargs) -> 'OmniQJob':
        qasm_str = ""
        num_q = 2

        if hasattr(run_input, "qasm"):
            try:
                qasm_str = run_input.qasm()
            except Exception:
                pass

        if not qasm_str:
            try:
                import qiskit.qasm3
                qasm_str = qiskit.qasm3.dumps(run_input)
            except Exception:
                qasm_str = str(run_input)

        if hasattr(run_input, "num_qubits"):
            num_q = run_input.num_qubits

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

