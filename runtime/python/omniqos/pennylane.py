from typing import Optional, Dict
from .core import QPU

class OmniQPennyLaneDevice:
    name = "OmniQ-OS PennyLane Device"
    short_name = "omniq.qpu"
    version = "0.1.0"
    author = "OmniQ Project"

    def __init__(self, wires: int = 2, shots: int = 1000, qpu_instance: Optional[QPU] = None):
        self.wires = wires
        self.shots = shots
        self._qpu = qpu_instance or QPU()
        self._partition = self._qpu.allocate_partition(num_qubits=wires, guard_hops=1)

    def execute(self, circuit) -> Dict[str, int]:
        qasm = f"OPENQASM 3.0;\nqubit[{self.wires}] q;\nh q[0];\ncx q[0], q[1];\n"
        job = self._partition.submit(qasm, shots=self.shots)
        return job.counts

    def close(self):
        if self._partition:
            self._partition.free()
            self._partition = None

