from typing import Optional, Dict
from .core import QPU

class OmniQBraketDevice:
    def __init__(self, qpu_instance: Optional[QPU] = None):
        self._qpu = qpu_instance or QPU()
        self.arn = "arn:aws:braket:::device/qpu/omniq/transmon-127"
        self.name = "OmniQ-Transmon-127"

    def run(self, task_specification, shots: int = 1000, **kwargs) -> 'OmniQBraketTask':
        qasm_source = ""
        if hasattr(task_specification, "to_ir"):
            try:
                qasm_source = task_specification.to_ir()
            except Exception:
                pass
        if not qasm_source:
            qasm_source = str(task_specification)

        partition = self._qpu.allocate_partition(num_qubits=2, guard_hops=1)
        try:
            job = partition.submit(qasm_source, shots=shots)
        finally:
            partition.free()

        return OmniQBraketTask(job.counts, shots)

class OmniQBraketTask:
    def __init__(self, counts: Dict[str, int], shots: int):
        self._counts = counts
        self._shots = shots

    def state(self) -> str:
        return "COMPLETED"

    def result(self) -> 'OmniQBraketResult':
        return OmniQBraketResult(self._counts, self._shots)

class OmniQBraketResult:
    def __init__(self, counts: Dict[str, int], shots: int):
        self.measurement_counts = counts
        self.shots = shots

