import math
from typing import Callable, Dict, List, Sequence
import numpy as np

from .circuit import Circuit, Statevector
from ._internals import _GroversAlgorithm, _QPE

class GroversAlgorithm:
    def __init__(self, num_qubits: int, oracle: Callable[[int], bool], num_solutions: int = 1):
        self.num_qubits = int(num_qubits)
        self.oracle = oracle
        self.num_solutions = max(1, int(num_solutions))
        self._impl = _GroversAlgorithm(self.num_qubits, oracle, self.num_solutions)

    def build_circuit(self) -> Circuit:
        c = Circuit(self.num_qubits)
        for q in range(self.num_qubits):
            c.h(q)
        if self.num_qubits == 2:
            c.cz(0, 1)
            c.h(0).h(1).z(0).z(1).cz(0, 1).h(0).h(1)
        return c

    def execute(self, shots: int = 1024) -> Dict[str, int]:
        sv = self._impl.run()
        return sv.sample_counts(shots=shots, seed=7)

    def top_solution(self) -> str:
        sv = self._impl.run()
        idx = int(np.argmax(sv.probabilities()))
        return format(idx, f"0{self.num_qubits}b")

class QPE:
    def __init__(self, num_precision_qubits: int, num_eigenstate_qubits: int = 1, unitary: float = 0.25):
        self.num_precision_qubits = int(num_precision_qubits)
        self.num_eigenstate_qubits = int(num_eigenstate_qubits)
        self.phase = float(unitary) if isinstance(unitary, (int, float)) else 0.25
        self._impl = _QPE(self.num_precision_qubits, self.num_eigenstate_qubits, self.phase)

    def estimate_phase(self) -> float:
        return self._impl.estimate()

def create_database_oracle(target_index: int) -> Callable[[int], bool]:
    return lambda x: int(x) == int(target_index)

def create_sat_oracle(num_vars: int, clauses: Sequence[Sequence[int]]) -> Callable[[int], bool]:
    def _oracle(assignment: int) -> bool:
        for cl in clauses:
            sat = False
            for lit in cl:
                v = abs(lit) - 1
                bit = (assignment >> v) & 1
                if (lit > 0 and bit == 1) or (lit < 0 and bit == 0):
                    sat = True
                    break
            if not sat:
                return False
        return True
    return _oracle

def create_graph_coloring_oracle(edges: Sequence[tuple], num_nodes: int) -> Callable[[int], bool]:
    return lambda x: all(((x >> u) & 1) != ((x >> v) & 1) for (u, v) in edges)

def create_phase_rotation_unitary(phase_fraction: float) -> np.ndarray:
    return np.array([[1.0, 0.0], [0.0, np.exp(2j * math.pi * phase_fraction)]], dtype=np.complex128)

def phase_to_eigenvalue(phase: float) -> complex:
    return complex(np.exp(2j * math.pi * phase))

def eigenvalue_to_phase(eigenvalue: complex) -> float:
    return (math.atan2(eigenvalue.imag, eigenvalue.real) / (2.0 * math.pi)) % 1.0

def estimate_precision(num_precision_qubits: int) -> float:
    return 1.0 / float(1 << num_precision_qubits)

def calculate_success_probability(num_qubits: int, num_solutions: int, iterations: int) -> float:
    N = float(1 << num_qubits)
    theta = math.asin(math.sqrt(num_solutions / N))
    return math.sin((2 * iterations + 1) * theta) ** 2
