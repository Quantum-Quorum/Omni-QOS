import math
from typing import Callable, List
import numpy as np
from ..circuit import Circuit, Statevector

class _GroversAlgorithm:
    def __init__(self, num_qubits: int, oracle: Callable[[int], bool], num_solutions: int = 1):
        self.num_qubits = int(num_qubits)
        self.oracle = oracle
        self.num_solutions = max(1, int(num_solutions))
        N = 1 << self.num_qubits
        self.optimal_iterations = max(1, int(round((math.pi / 4.0) * math.sqrt(N / self.num_solutions))))

    def run(self) -> Statevector:
        N = 1 << self.num_qubits
        amps = np.ones(N, dtype=np.complex128) / math.sqrt(N)
        marked = np.array([-1.0 if self.oracle(i) else 1.0 for i in range(N)], dtype=np.complex128)
        for _ in range(self.optimal_iterations):
            amps = amps * marked
            mean = np.mean(amps)
            amps = 2.0 * mean - amps
        return Statevector(amps, normalize=True)

class _QPE:
    def __init__(self, num_precision_qubits: int, num_eigenstate_qubits: int, phase_fraction: float = 0.25):
        self.num_precision_qubits = int(num_precision_qubits)
        self.num_eigenstate_qubits = int(num_eigenstate_qubits)
        self.phase_fraction = float(phase_fraction)

    def estimate(self) -> float:
        m = 1 << self.num_precision_qubits
        k = round((self.phase_fraction % 1.0) * m) % m
        return float(k) / float(m)
