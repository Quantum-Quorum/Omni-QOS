import math
from typing import Sequence
import numpy as np
from ..circuit import Circuit, Statevector

class VariationalCircuit:
    def __init__(self, num_qubits: int = 2, num_layers: int = 2):
        self.num_qubits = int(num_qubits)
        self.num_layers = int(num_layers)
        self.weights = np.random.default_rng(42).uniform(-0.5, 0.5, size=(self.num_layers, self.num_qubits, 2))

    def build(self, features: Sequence[float], weights: np.ndarray = None) -> Circuit:
        w = self.weights if weights is None else weights
        c = Circuit(self.num_qubits)
        for q in range(self.num_qubits):
            x_val = float(features[q % len(features)]) if len(features) > 0 else 0.0
            c.ry(q, x_val * math.pi)
        for l in range(self.num_layers):
            for q in range(self.num_qubits):
                c.ry(q, float(w[l, q, 0])).rz(q, float(w[l, q, 1]))
            for q in range(self.num_qubits - 1):
                c.cx(q, q + 1)
        return c

    def forward(self, features: Sequence[float], weights: np.ndarray = None) -> float:
        sv = self.build(features, weights).execute()
        assert isinstance(sv, Statevector)
        return sv.measure_expectation(0, "Z")
